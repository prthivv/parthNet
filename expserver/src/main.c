#include "xps.h"

int epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
vec_void_t listeners;
vec_void_t connections;

int xps_loop_create(){
    //Setting up epoll
    int epoll_fd=epoll_create1(0);
    return epoll_fd;
}

void xps_loop_attach(int epoll_fd,int fd,int events){
    struct epoll_event event;
    event.events=events;
    event.data.fd=fd;
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event);
}

void xps_loop_detach(int epoll_fd,int fd){
    if(epoll_ctl(epoll_fd,EPOLL_CTL_DEL,fd,NULL)<0){
        logger(LOG_ERROR,"xps_loop_detach","epoll_ctl() failed to detach fd %d",fd);
        perror("Error message");
    }
}

void xps_loop_run(int epoll_fd){
    while(1){
        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait");
        int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
        logger(LOG_DEBUG, "xps_loop_run()", "epoll wait over");

        for(int i=0;i<n_ready_fds;i++){
            int curr_fd=events[i].data.fd;

            xps_listener_t *listener=NULL;

            for(int i=0;i<listeners.length;i++){
                xps_listener_t *curr=listeners.data[i];
                if(curr!=NULL&&curr->sock_fd==curr_fd){
                    listener=curr;
                    break;
                }
            }
            if(listener){
                xps_listener_connection_handler(listener);
                continue;
            }

            xps_connection_t *connection=NULL;

            for(int i=0;i<connections.length;i++){
                xps_connection_t *curr=connections.data[i];
                if(curr!=NULL&&curr->sock_fd==curr_fd){
                    connection=curr;
                    break;
                }
            }
            if(connection)
                xps_connection_read_handler(connection);
        }
    }
}

int main(){
    epoll_fd=xps_loop_create();

    vec_init(&listeners);
    vec_init(&connections);

    for(int port=8001;port<=8004;port++){
        xps_listener_t *listener=xps_listener_create(epoll_fd,"0.0.0.0",port);
        logger(LOG_INFO, "main()", "Server listening on port %u", port);
    }

    xps_loop_run(epoll_fd);
}