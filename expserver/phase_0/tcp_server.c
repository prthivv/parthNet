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

void stringreverse(char *str){
    for(int start=0,end=strlen(str)-2;start<end;start++,end--){
        char temp=str[start];
        str[start]=str[end];
        str[end]=temp;
    }
}

int main(){
    // Creating TCP socket
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

    //Setting up epoll
    int epoll_fd=epoll_create1(0);

    struct epoll_event event, events[MAX_POLL_EVENTS];

    event.events=EPOLLIN;
    event.data.fd=listen_sock_fd;
    epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_sock_fd,&event);

    
    while(1){
        printf("[DEBUG] Epoll wait\n");
        int n_read_fds=epoll_wait(epoll_fd,events,MAX_POLL_EVENTS,-1);
        
        for(int i=0;i<n_read_fds;i++){
            int curr_fd=events[i].data.fd;

            if(curr_fd==listen_sock_fd){
                //Setting up client address struct and accepting connection
                struct sockaddr_in client_addr;
                socklen_t client_addr_len=sizeof(client_addr);

                int conn_sock_fd=accept(listen_sock_fd, (struct sockaddr *)&client_addr,&client_addr_len);
                printf("[INFO] Client connected to server\n");

                struct epoll_event conn_event;
                conn_event.events=EPOLLIN;
                conn_event.data.fd=conn_sock_fd;
                epoll_ctl(epoll_fd,EPOLL_CTL_ADD,conn_sock_fd,&conn_event);
            }
            else{
                char buff[BUFF_SIZE];

                memset(buff,0,BUFF_SIZE);

                ssize_t read_n = recv(curr_fd,buff,sizeof(buff),0);

                if(read_n<0){
                    printf("[INFO] Error occured. Closing connection\n");
                    close(curr_fd);
                    continue;
                }
                else if(read_n==0){
                    printf("[INFO] Client Disconnected. Closing connection\n");
                    close(curr_fd);
                    continue;
                }
                printf("[CLIENT MESSAGE] %s",buff);

                stringreverse(buff);

                send(curr_fd,buff,read_n,0);
            }
        }
            
    }
}