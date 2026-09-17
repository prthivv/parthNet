#include "xps_upstream.h"

xps_connection_t *xps_upstream_create(xps_core_t *core, const char *host, u_int port){

    assert(core!=NULL);
    assert(host!=NULL);

    u_int sockfd=socket(AF_INET,SOCK_STREAM | SOCK_NONBLOCK,0);

    if(sockfd<0){
        logger(LOG_ERROR,"xps_upstream_create()","socket() failed");
        return NULL;
    }

    struct addrinfo *addr=xps_getaddrinfo(host,port);

    if(addr==NULL){
        logger(LOG_ERROR,"xps_upstream_create()","xps_getaddrinfo() failed");
        close(sockfd);
        return NULL;
    }

    int connect_error=connect(sockfd,addr->ai_addr, addr->ai_addrlen);

    if(!(connect_error==0 || errno == EINPROGRESS)){
        logger(LOG_ERROR,"xps_upstream_create()","connect() failed");
        perror("Error message");
        close(sockfd);
        return NULL;
    }

    free(addr);

    xps_connection_t *connection=xps_connection_create(core,sockfd);

    return connection;
}