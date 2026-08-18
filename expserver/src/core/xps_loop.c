#include "xps_loop.h"

loop_event_t *loop_event_create(u_int fd,void *ptr,xps_handler_t read_cb){
    assert(ptr!=NULL);

    loop_event_t *event=malloc(sizeof(loop_event_t));
    if(event==NULL){
        logger(LOG_ERROR,"event_create()","malloc() failed for 'event'");
        return NULL;
    }

    event->fd=fd;
    event->ptr=ptr;
    event->read_cb=read_cb;

    logger(LOG_DEBUG,"event_create()","created event");

    return event;
}

void loop_event_destroy(loop_event_t *event){
    assert(event!=NULL);

    free(event);

    logger(LOG_DEBUG,"event_destroy()","destroyed event");
}

xps_loop_t *xps_loop_create(xps_core_t *core){
    assert(core!=NULL);

    int epoll_fd=epoll_create1(0);

    if(epoll_fd<0){
        logger(LOG_ERROR,"xps_loop_create","epoll_create1() failed");
        perror("Error message");
        return NULL;
    }

    xps_loop_t * loop=malloc(sizeof(xps_loop_t));
    if(loop==NULL){
        logger(LOG_ERROR,"xps_looop_create","malloc() failed for loop");
        close(epoll_fd);
        return NULL;
    }

    loop->core=core;
    loop->epoll_fd=epoll_fd;
    vec_init(&(loop->events));
    loop->n_null_events=0;

    logger(LOG_DEBUG,"xps_loop_create()","created loop");

    return loop;
}

void xps_loop_destroy(xps_loop_t *loop){
    assert(loop!=NULL);

    for(int i=0;i<(loop->events).length;i++){
        loop_event_t *event=loop->events.data[i];
        if(event!=NULL)
            loop_event_destroy(event); 
    }
    vec_deinit(&(loop->events));

    close(loop->epoll_fd);

    free(loop);

    logger(LOG_DEBUG,"xps_loop_destroy","destroyed loop");
}

int xps_loop_attach(xps_loop_t *loop, u_int fd,int event_flags,void *ptr, xps_handler_t read_cb){
    assert(loop!=NULL);
    assert(ptr!=NULL);

    loop_event_t *event=loop_event_create(fd,ptr,read_cb);

    if(event==NULL){
        logger(LOG_ERROR,"xps_loop_attach()","loop_event_create() failed");
        return E_FAIL;
    }

    struct epoll_event epoll_event;
    epoll_event.events=event_flags;
    epoll_event.data.ptr=event;

    if(epoll_ctl(loop->epoll_fd,EPOLL_CTL_ADD,fd,&epoll_event)<0){
        logger(LOG_ERROR,"xps_loop_attach","epoll_ctl() failed");
        perror("Error message");
        loop_event_destroy(event);
        return E_FAIL;
    }

    vec_push(&(loop->events),event);

    logger(LOG_DEBUG,"xps_loop_attach()","attached fd %d to loop",fd);

    return OK;
}

int xps_loop_detach(xps_loop_t *loop,u_int fd){

    assert(loop!=NULL);

    if(epoll_ctl(loop->epoll_fd,EPOLL_CTL_DEL,fd,NULL)<0){
        logger(LOG_ERROR, "xps_loop_detach()", "epoll_ctl() failed to detach fd %d", fd);
        perror("Error message");
        return E_FAIL;
    }
    loop_event_t *curr;
    for(int i=0;i<loop->events.length;i++){
        curr=loop->events.data[i];
        if(curr!=NULL&&curr->fd==fd){
            loop_event_destroy(curr);
            loop->events.data[i]=NULL;
            (loop->n_null_events)++;
            logger(LOG_DEBUG,"xps_loop_detach","detached fd %d from loop",fd);
            return OK;
        }
    }

    
    logger(LOG_ERROR,"xps_loop_detach","unable to find specified event");
    return E_FAIL;
}

void xps_loop_run(xps_loop_t *loop){
    assert(loop!=NULL);

    while(1){
        logger(LOG_DEBUG,"xps_loop_run()","epoll wait");

        int n_events=epoll_wait(loop->epoll_fd,loop->epoll_events,MAX_EPOLL_EVENTS,-1);

        logger(LOG_DEBUG,"xps_loop_run()","epoll wait over");

        logger(LOG_DEBUG,"xps_loop_run()","handling %d events",n_events);

        for(int i=0;i<n_events;i++){
            logger(LOG_DEBUG,"xps_loop_run()","handling event no. %d",i+1);

            struct epoll_event curr_epoll_event=loop->epoll_events[i];
            loop_event_t *curr_event=curr_epoll_event.data.ptr;

            int curr_event_idx=-1;
            for(int i=0;i<loop->events.length;i++){
                if(curr_event==loop->events.data[i]){
                    curr_event_idx=i;
                    break;
                }
            }
            if(curr_event_idx==-1){
                logger(LOG_DEBUG,"handle_epoll_events()","event not found. skipping");
                continue;
            }

            if(curr_epoll_event.events&EPOLLIN){
                logger(LOG_DEBUG,"handle_epoll_events()","EVENT / read");
                if(curr_event->read_cb!=NULL){
                    curr_event->read_cb(curr_event->ptr);
                }
            }
        }
    }
}