#include "xps_core.h"

xps_core_t *xps_core_create(){
    xps_core_t *core = malloc(sizeof(xps_core_t));

    if(core==NULL){
        logger(LOG_ERROR,"xps_core_create()","malloc() failed for 'core'");
        return NULL;
    }

    xps_loop_t *loop=xps_loop_create(core);

    if(loop==NULL){
        free(core);
        logger(LOG_ERROR,"xps_core_create()","xps_loop_create() failed");
        return NULL;
    }

    core->loop=loop;
    vec_init(&(core->listeners));
    vec_init(&(core->connections));
    core->n_null_connections=0;
    core->n_null_listeners=0;

    logger(LOG_DEBUG,"xps_core_create()","created core");

    return core;
}

void xps_core_destroy(xps_core_t *core){
    assert(core!=NULL);

    for(int i=0;i<core->connections.length;i++){
        xps_connection_t *connection=core->connections.data[i];

        if(connection!=NULL)
            xps_connection_destroy(connection);
    }
    vec_deinit(&(core->connections));

    for(int i=0;i<core->listeners.length;i++){
        xps_listener_t *listener=core->listeners.data[i];

        if(listener!=NULL)
            xps_listener_destroy(listener);
    }
    vec_deinit(&(core->listeners));

    xps_loop_destroy(core->loop);

    free(core);

    logger(LOG_DEBUG,"xps_core_destroy()","destroyed core");
}

void xps_core_start(xps_core_t *core){

    assert(core!=NULL);

    logger(LOG_DEBUG,"xps_start()","starting core");

    for(int i=8001;i<=8004;i++){
        xps_listener_t* listener=xps_listener_create(core,"0.0.0.0",i);
        logger(LOG_INFO, "xps_core_start()", "Server listening on port %u", i);
    }

    logger(LOG_DEBUG,"xps_core_start()","core started");
    xps_loop_run(core->loop);

    
}