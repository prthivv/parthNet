#include "xps_connection.h"

void connection_loop_read_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core,int sock_fd){
    xps_connection_t *connection=(xps_connection_t *)malloc(sizeof(xps_connection_t));

    if(connection==NULL){
        logger(LOG_ERROR,"xps_connection_create()","malloc() failed for 'connection'");
        return NULL;
    }

    if (xps_loop_attach(core->loop, sock_fd, EPOLLIN, connection, connection_loop_read_handler) != OK) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
        close(sock_fd);
        free(connection->remote_ip);
        free(connection);
        return NULL;
    }

    connection->core=core;
    connection->sock_fd=sock_fd;
    connection->listener=NULL;
    connection->remote_ip=get_remote_ip(sock_fd);

    vec_push(&(core->connections),connection);

    logger(LOG_DEBUG, "xps_connection_create()", "created connection");
    return connection;
}

void xps_connection_destroy(xps_connection_t *connection){
    assert(connection!=NULL);

    for(int i=0;i<connection->core->connections.length;i++){
        xps_connection_t *curr=connection->core->connections.data[i];
        if(curr==connection){
            connection->core->connections.data[i]=NULL;
            connection->core->n_null_connections++;
            break;
        }
    }

    xps_loop_detach(connection->core->loop,connection->sock_fd);

    close(connection->sock_fd);

    free(connection->remote_ip);

    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_loop_read_handler(void *ptr){
    assert(ptr!=NULL);

    xps_connection_t *connection=ptr;

    char buff[DEFAULT_BUFFER_SIZE];

    memset(buff,0,DEFAULT_BUFFER_SIZE);

    long read_n=recv(connection->sock_fd,buff,DEFAULT_BUFFER_SIZE,0);

    if(read_n < 0) {
        logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
        perror("Error message");
        xps_connection_destroy(connection);
        return;
    }

    if(read_n == 0) {
        logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
        xps_connection_destroy(connection);
        return;
    }

    buff[read_n]='\0';

    printf("[CLIENT MESSAGE] %s",buff);

    for(int start=0,end=read_n-2;start<end;start++,end--){
        char temp=buff[start];
        buff[start]=buff[end];
        buff[end]=temp;
    }

    long bytes_written=0;
    long message_len=read_n;
    while(bytes_written<message_len){
        long write_n=send(connection->sock_fd,buff+bytes_written,read_n-bytes_written,0);
        if(write_n < 0) {
            logger(LOG_ERROR, "xps_connection_read_handler()", "send() failed");
            perror("Error message");
            xps_connection_destroy(connection);
            return;
        }
        bytes_written+=write_n;
    }

}