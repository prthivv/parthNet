#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_POLL_EVENTS 10
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

int listen_sock_fd,epoll_fd;
struct epoll_event events[MAX_POLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0;

void remove_route(int index){
    int conn_fd=route_table[index][0];
    int upstream_fd=route_table[index][1];

    close(conn_fd);
    close(upstream_fd);

    route_table[index][0]=route_table[route_table_size-1][0];
    route_table[index][1]=route_table[route_table_size-1][1];
    route_table_size--;
}

int create_loop(){
    //Setting up epoll
    int epoll_fd=epoll_create1(0);
    return epoll_fd;
}

void loop_attach(int epoll_fd, int fd, int events){
    struct epoll_event event;
    event.events=events;
    event.data.fd=fd;
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,fd,&event);
}

int connect_upstream(){
    int upstream_sock_fd=socket(AF_INET,SOCK_STREAM,0);

    struct sockaddr_in upstream_addr;
    upstream_addr.sin_addr.s_addr=inet_addr("127.0.0.1");
    upstream_addr.sin_family=AF_INET;
    upstream_addr.sin_port=htons(UPSTREAM_PORT);



    if(connect(upstream_sock_fd,(struct sockaddr *)&upstream_addr,sizeof(upstream_addr))!=0){
        printf("[ERROR] Failed to connect to upstream server\n");
        exit(1);
    }
    else{
        printf("[INFO] Connected to upstream server\n");
    }
    return upstream_sock_fd;
    
}

void accept_connection(int listen_sock_fd){
    //Setting up client address struct and accepting connection
    struct sockaddr_in client_addr;
    socklen_t client_addr_len=sizeof(client_addr);

    int conn_sock_fd=accept(listen_sock_fd, (struct sockaddr *)&client_addr,&client_addr_len);
    printf("[INFO] Client connected to server\n");
    
    loop_attach(epoll_fd,conn_sock_fd,EPOLLIN);

    int upstream_sock_fd=connect_upstream();

    loop_attach(epoll_fd,upstream_sock_fd,EPOLLIN);

    route_table[route_table_size][0]=conn_sock_fd;
    route_table[route_table_size][1]=upstream_sock_fd;
    route_table_size+=1;
    
}

void handle_upstream(int upstream_sock_fd){
    char buff[BUFF_SIZE];

    memset(buff,0,BUFF_SIZE);

    ssize_t read_n = recv(upstream_sock_fd,buff,sizeof(buff),0);

    printf("[SERVER MESSAGE] %s",buff);

    int conn_sock=-1,idx=-1;
    for(int i=0;i<route_table_size;i++){
        if(route_table[i][1]==upstream_sock_fd){
            conn_sock=route_table[i][0];
            idx=i;
            break;
        }
    }
    if(conn_sock==-1)
        return;
    if(read_n<=0){
        remove_route(idx);
        return;
    }
    int bytes_written=0;

    int message_len=read_n;

    while(bytes_written<message_len){
        int n=send(conn_sock,buff+bytes_written,message_len-bytes_written,0);
        bytes_written+=n;
    }
}

void handle_client(int conn_sock_fd){
    char buff[BUFF_SIZE];

    memset(buff,0,BUFF_SIZE);

    ssize_t read_n = recv(conn_sock_fd,buff,sizeof(buff),0);

    printf("[CLIENT MESSAGE] %s",buff);

    int upstream_sock=-1,idx=-1;

    for(int i=0;i<route_table_size;i++){
        if(route_table[i][0]==conn_sock_fd){
            upstream_sock=route_table[i][1];
            idx=i;
            break;
        }
    }
    if(upstream_sock==-1)
        return;
    if(read_n<=0){
        remove_route(idx);
        return;
    }
    int bytes_written=0;

    int message_len=read_n;

    while(bytes_written<message_len){
        int n=send(upstream_sock,buff+bytes_written,message_len-bytes_written,0);
        bytes_written+=n;
    }

}

int create_server(){
    int listen_sock_fd=socket(AF_INET, SOCK_STREAM, 0);

    // Allow reuse of address 
    int enable =1;
    setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
    
    //Setting up server address struct to use for bind 
    struct sockaddr_in server_addr;

    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    //Binding socket to the port and address
    bind(listen_sock_fd,(struct sockaddr *)&server_addr,sizeof(server_addr));

    //Starting to listen
    listen(listen_sock_fd,MAX_ACCEPT_BACKLOG);
    printf("[INFO] Server listening on port %d\n", PORT);

    return listen_sock_fd;
}

void loop_run(int epoll_fd){
    while(1){
        printf("[DEBUG] Epoll wait\n");

        int n_read_fds=epoll_wait(epoll_fd,events,MAX_POLL_EVENTS,-1);

        for(int i=0;i<n_read_fds;i++){
            int curr_fd=events[i].data.fd;

            if(curr_fd==listen_sock_fd){
                accept_connection(listen_sock_fd);
            }
            else{
                for(int j=0;j<route_table_size;j++){
                    if(route_table[j][0]==curr_fd){
                        handle_client(curr_fd);
                        break;
                    }
                    else if(route_table[j][1]==curr_fd){
                        handle_upstream(curr_fd);
                        break;
                    }
                }
            }
        }
    }
}

int main(){
    listen_sock_fd=create_server();
    epoll_fd=create_loop();

    loop_attach(epoll_fd,listen_sock_fd,EPOLLIN);

    loop_run(epoll_fd);
}