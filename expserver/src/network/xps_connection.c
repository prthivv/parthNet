#include "xps_connection.h"

void connection_loop_read_handler(void *ptr);
void connection_loop_write_handler(void *ptr);
void connection_loop_close_handler(void *ptr);
void connection_read_handler(void *ptr);
void connection_write_handler(void *ptr);

xps_connection_t *xps_connection_create(xps_core_t *core,int sock_fd){
    xps_connection_t *connection=(xps_connection_t *)malloc(sizeof(xps_connection_t));

    if(connection==NULL){
        logger(LOG_ERROR,"xps_connection_create()","malloc() failed for 'connection'");
        return NULL;
    }

    connection->core=core;
    connection->sock_fd=sock_fd;
    connection->listener=NULL;
    connection->remote_ip=get_remote_ip(sock_fd);
    connection->write_buff_list=xps_buffer_list_create();
    connection->read_ready=false;
    connection->write_ready=false;
    connection->recv_handler=connection_read_handler;
    connection->send_handler=connection_write_handler;

    if (xps_loop_attach(core->loop, sock_fd, EPOLLIN | EPOLLOUT | EPOLLET, connection, connection_loop_read_handler,connection_loop_write_handler,connection_loop_close_handler) != OK) {
        logger(LOG_ERROR, "xps_connection_create()", "xps_loop_attach() failed");
        close(sock_fd);
        free(connection->remote_ip);
        free(connection);
        return NULL;
    }

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

    xps_buffer_list_destroy(connection->write_buff_list);

    free(connection);

    logger(LOG_DEBUG, "xps_connection_destroy()", "destroyed connection");
}

void connection_read_handler(void *ptr){
    assert(ptr!=NULL);

    xps_connection_t *connection=ptr;

    char buff[DEFAULT_BUFFER_SIZE];

    memset(buff,0,DEFAULT_BUFFER_SIZE);

    long read_n=recv(connection->sock_fd,buff,DEFAULT_BUFFER_SIZE,0);

    if(read_n < 0) {

        if(errno ==EAGAIN || errno == EWOULDBLOCK){
            connection->read_ready=false;
            return;
        }
        else{
            logger(LOG_ERROR, "xps_connection_read_handler()", "recv() failed");
            perror("Error message");
            xps_connection_destroy(connection);
            return;
        }
    }

    if(read_n == 0) {
        logger(LOG_INFO, "connection_read_handler()", "peer closed connection");
        xps_connection_destroy(connection);
        return;
    }

    buff[read_n]='\0';

    //printf("[CLIENT MESSAGE] %s",buff);

    for(int start=0,end=read_n-2;start<end;start++,end--){
        char temp=buff[start];
        buff[start]=buff[end];
        buff[end]=temp;
    }

    xps_buffer_t *buffer=xps_buffer_create(read_n,read_n,NULL);

    if(buffer==NULL){
        logger(LOG_ERROR,"connection_loop_read_handler()","xps_buffer_create() failed");
        xps_connection_destroy(connection);
        return;
    }

    memcpy(buffer->data,buff,read_n);

    xps_buffer_list_append(connection->write_buff_list,buffer);

}

void connection_write_handler(void *ptr){
    assert(ptr!=NULL);

    xps_connection_t *connection=ptr;

    if(connection->write_buff_list->len==0)
        return;

    xps_buffer_t *buff=xps_buffer_list_read(connection->write_buff_list,connection->write_buff_list->len);

    
    if(buff==NULL){
        logger(LOG_ERROR,"connection_loop_write_handler()","xps_buffer_list_read() failed");
        xps_connection_destroy(connection);
        return;
    }

    if(buff->len==0){
        xps_buffer_destroy(buff);
        return;
    }

    long write_n=send(connection->sock_fd,buff->data,buff->len,0);
    
    if(write_n<0){
        if(errno==EAGAIN||errno==EWOULDBLOCK){
            connection->write_ready=false;
            xps_buffer_destroy(buff);
            return;
        }
        else{
            xps_buffer_destroy(buff);
            xps_connection_destroy(connection);
            return;
        }
    }
    xps_buffer_list_clear(connection->write_buff_list,write_n);
    xps_buffer_destroy(buff);
    return;
}

void connection_loop_read_handler(void *ptr){
    assert(ptr!=NULL);
    xps_connection_t *connection=ptr;

    connection->read_ready=true;
}

void connection_loop_write_handler(void *ptr){
    assert(ptr!=NULL);

    xps_connection_t *connection=ptr;

    connection->write_ready=true; 
}

void connection_loop_close_handler(void *ptr){
    assert(ptr!=NULL);

    xps_connection_t *connection=ptr;

    xps_connection_destroy(connection);
}