#include "xps_config.h"

xps_config_listener_t *parse_listener(JSON_Object *listener_object);
xps_config_route_t *parse_route(JSON_Object *route_object);
void parse_server(JSON_Object *server_object,xps_config_server_t *server);
void parse_all_listeners(xps_config_t *config);

xps_config_t *xps_config_create(const char *config_path){
    assert(config_path != NULL);

    xps_config_t *config = malloc(sizeof(xps_config_t));

    if(config==NULL){
        logger(LOG_ERROR,"xps_config_create()","Failed to allocate memory for config");
        return NULL;
    }

    JSON_Value *config_json=json_parse_file(config_path);

    if(config_json==NULL){
        logger(LOG_ERROR,"xps_config_create()","Failed to parse config file: %s",config_path);
        return NULL;
    }

    config->config_path=config_path;
    vec_init(&(config->servers));
    vec_init(&(config->_all_listeners));
    config->_config_json=config_json;

    JSON_Object *root_object = json_value_get_object(config_json);

    config->server_name=json_object_get_string(root_object,"server_name");
    JSON_Array *servers = json_object_get_array(root_object,"servers");

    for(size_t i=0;i<json_array_get_count(servers);i++){
        xps_config_server_t *server = malloc(sizeof(xps_config_server_t));

        if(server==NULL){
            logger(LOG_ERROR,"xps_config_create()","Failed to allocate memory for sever");
            json_value_free(config_json);
            free(config);
            return NULL;
        }

        JSON_Object *server_obj=json_array_get_object(servers,i);

        parse_server(server_obj,server);
        vec_push(&config->servers,server);
    }
    parse_all_listeners(config);
    return config;
}

void xps_config_destroy(xps_config_t *config){
    assert(config!=NULL);

    vec_void_t *servers=&config->servers;

    for(int i=0;i<servers->length;i++){
        xps_config_server_t *server = (xps_config_server_t *)servers->data[i];

        if(server){
            vec_void_t *listeners = &server->listeners;
            for(int j=0;j<listeners->length;j++){
                xps_config_listener_t *listener=(xps_config_listener_t *)listeners->data[j];
                if(listener)
                    free(listener);
            }
            vec_deinit(&server->listeners);

            vec_deinit(&server->hostnames);

            vec_void_t *routes = &server->routes;

            for(int j=0;j<routes->length;j++){
                xps_config_route_t *route = (xps_config_route_t *)routes->data[i];

                if(route){
                    vec_deinit(&route->index);
                    vec_deinit(&route->upstreams);
                    free(route);
                }
            }
            vec_deinit(&server->routes);
            free(server);
        }
    }
}

xps_config_lookup_t *xps_config_lookup(xps_config_t *config,xps_http_req_t *http_req,xps_connection_t *client, int *error){
    assert(config!=NULL);
    assert(http_req!=NULL);
    assert(client!=NULL);

    *error=E_FAIL;

    char *host=http_req->host;
    char *pathname=http_req->pathname;
    const char* encoding = xps_http_get_header(&http_req->headers,"Accept-Encoding");

    int target_server_index=-1;

    vec_void_t *servers=&config->servers;

    for(int i=0;i<config->servers.length;i++){
        xps_config_server_t *server = servers->data[i];

        vec_void_t *listeners=&server->listeners;
        int has_matching_listener = false;
        for(int j=0;j<listeners->length;j++){
            xps_config_listener_t *listener = listeners->data[j];

            if(strcmp(listener->host,client->listener->host) == 0 && listener->port == client->listener->port){
                has_matching_listener=true;
                break;
            }
        }
        if(!has_matching_listener)
            continue;
        
        int has_matching_hostname=false;
        vec_void_t *hostnames=&server->hostnames;

        if(hostnames->length==0)
            has_matching_hostname=true;
        else{
            for(int j=0;j<hostnames->length;j++){
                const char *hostname = hostnames->data[j];

                if(strcmp(hostname,host)==0){
                    has_matching_hostname=true;
                    break;
                }
            }
        }

        if(has_matching_hostname){
            target_server_index=i;
            break;
        }
    }

    if(target_server_index == -1) {
        logger(LOG_ERROR, "xps_config_lookup()", "No matching server found for host: %s, listener: %s:%u", host, client->listener->host, client->listener->port);
        *error = E_NOTFOUND;  // No matching server found - 404
        return NULL;
    }

    xps_config_server_t *server=config->servers.data[target_server_index];

    xps_config_route_t *route=NULL;
    size_t best_match_len = 0;

    for(int i=0;i<server->routes.length;i++){
        xps_config_route_t *current_route = server->routes.data[i];

        size_t route_path_len = strlen(current_route->req_path);

        if(str_starts_with(pathname,current_route->req_path)){
            if(route_path_len > best_match_len){
                best_match_len=route_path_len;
                route=current_route;
            }
        }
    }

    if(route==NULL){
        *error=E_NOTFOUND;
        return NULL;
    }

    xps_config_lookup_t *lookup = malloc(sizeof(xps_config_lookup_t));

    if (lookup == NULL) {
        logger(LOG_ERROR, "xps_config_lookup()", "Failed to allocate memory for lookup");
        *error = E_FAIL;
        return NULL;
    }

    vec_init(&lookup->ip_blacklist);
    vec_init(&lookup->ip_whitelist);
    lookup->dir_path=NULL;
    lookup->file_path=NULL;
    lookup->http_status_code=0;
    lookup->redirect_url=NULL;
    lookup->upstream=NULL;

    if(strcmp(route->type,"file_serve")==0){
        lookup->type = REQ_FILE_SERVE;
    }
    else if(strcmp(route->type,"reverse_proxy")==0){
        lookup->type = REQ_REVERSE_PROXY;
    }
    else if(strcmp(route->type,"redirect")==0){
        lookup->type = REQ_REDIRECT;
    }
    else if(strcmp(route->type,"metrics")==0){
        lookup->type = REQ_METRICS;
    }
    else{
        lookup->type = REQ_INVALID;
    }

    if(lookup->type == REQ_FILE_SERVE){
        char *resource_path = path_join(route->dir_path,pathname);

        if(!is_abs_path(resource_path)){
            char *abs_path = realpath(resource_path,NULL);
            free(resource_path);
            resource_path=abs_path;
        }

        if(is_file(resource_path)){
            lookup->file_path = resource_path;
        }
        else if(is_dir(resource_path)){
            bool index_file_found = false;

            for(int i = 0 ;i<route->index.length;i++){
                char *index_file = path_join(resource_path,route->index.data[i]);

                if(is_file(index_file)){
                    lookup->file_path = index_file;
                    index_file_found=true;
                    break;
                }
                else
                    free(index_file);
            }

            if(!index_file_found){
                free(resource_path);
                *error=E_NOTFOUND;
                free(lookup);
                return NULL;
            }
        }
        else{
            free(resource_path);
            *error=E_NOTFOUND;
            free(lookup);
            return NULL;
        }
        *error = OK;
        return lookup;
    }
    if(lookup->type == REQ_REVERSE_PROXY){
        lookup->upstream = route->upstreams.data[0];
        *error=OK;
        return lookup;
    }
    if(lookup->type == REQ_REDIRECT){
        lookup->redirect_url = route->redirect_url;
        lookup->http_status_code = route->http_status_code;
        *error = OK;
        return lookup;
    }

    free(lookup);
    *error = E_FAIL;
    return NULL;
}

xps_config_listener_t *parse_listener(JSON_Object *listener_object){
    assert(listener_object!=NULL);

    const char *host = json_object_get_string(listener_object,"host");
    u_int port = (u_int)json_object_get_number(listener_object,"port");

    xps_config_listener_t *listener = malloc(sizeof(xps_config_listener_t));

    if(listener==NULL){
        logger(LOG_ERROR,"parse_listener()","Failed to allocate memory for listener");
        return NULL;
    }

    listener->host=host;
    listener->port=port;

    return listener;
}

xps_config_route_t *parse_route(JSON_Object *route_object){
    assert(route_object!=NULL);

    xps_config_route_t *route = malloc(sizeof(xps_config_route_t));
    if (route == NULL) {
        logger(LOG_ERROR, "parse_route()", "Failed to allocate memory for route");
        return NULL;
    }

    vec_init(&route->index);
    vec_init(&route->upstreams);

    route->req_path=json_object_get_string(route_object,"req_path");
    route->type=json_object_get_string(route_object,"type");
    route->dir_path=NULL;
    route->http_status_code=0;
    route->redirect_url=NULL;

    assert(route->type!=NULL);
    assert(route->req_path!=NULL);

    if(strcmp(route->type, "file_serve") == 0) {
        route->dir_path = json_object_get_string(route_object, "dir_path");
        assert(route->dir_path != NULL);

        JSON_Array *index = json_object_get_array(route_object, "index");
        for(size_t i = 0; i < json_array_get_count(index); i++) {
            vec_push(&route->index, (void*)json_array_get_string(index, i));
        }
    }
    else if (strcmp(route->type, "reverse_proxy") == 0) {
        JSON_Array *upstreams = json_object_get_array(route_object, "upstreams");
        assert(upstreams != NULL);
        for(size_t i = 0; i < json_array_get_count(upstreams); i++) {
            vec_push(&route->upstreams, (void*)json_array_get_string(upstreams, i));
        }
    }
    else if (strcmp(route->type, "redirect") == 0) {
        route->http_status_code = (u_int)json_object_get_number(route_object, "http_status_code");
        route->redirect_url = json_object_get_string(route_object, "redirect_url");

        assert(route->http_status_code != 0);
        assert(route->redirect_url != NULL);
    }

    return route;
}

void parse_server(JSON_Object *server_object,xps_config_server_t *server){
    assert(server_object != NULL);
    assert(server!=NULL);

    vec_init(&server->hostnames);
    vec_init(&server->listeners);
    vec_init(&server->routes);

    JSON_Array *listeners = json_object_get_array(server_object,"listeners");

    for(size_t i =0; i < json_array_get_count(listeners);i++){
        xps_config_listener_t *listener = parse_listener(json_array_get_object(listeners,i));

        if(listener)
            vec_push(&server->listeners,listener);
    }

    JSON_Array *hostnames = json_object_get_array(server_object,"hostnames");

    for(size_t i =0; i <json_array_get_count(hostnames);i++){
        const char *hostname = json_array_get_string(hostnames,i);
        if(hostname)
            vec_push(&server->hostnames,(void*)hostname);
    }

    JSON_Array *routes = json_object_get_array(server_object, "routes");

    for(size_t i = 0; i < json_array_get_count(routes);i++){
        xps_config_route_t *route = parse_route(json_array_get_object(routes,i));
        if(route)
            vec_push(&server->routes,route);
    }
}

void parse_all_listeners(xps_config_t *config){

    assert(config != NULL);

    for(int i=0;i<config->servers.length;i++){
        xps_config_server_t *server = config->servers.data[i];

        for(int j=0;j<server->listeners.length;j++){
            xps_config_listener_t *listener = server->listeners.data[j];

            bool already_exists = false;

            for(int k=0;k<config->_all_listeners.length;k++){
                xps_config_listener_t *existing_listener = config->_all_listeners.data[k];

                if(strcmp(existing_listener->host,listener->host) == 0 && existing_listener->port == listener->port){
                    already_exists=true;
                    break;
                }
            }
            if(!already_exists)
                vec_push(&config->_all_listeners,listener);
        }
    }
}