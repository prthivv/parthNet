#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8080
#define BUFF_SIZE 10000

typedef struct {
    char message[BUFF_SIZE];
    struct sockaddr_in client_addr;
    int sockfd;
    socklen_t addr_len;
} client_data_t;

void stringreverse(char *str){
    for(int start=0,end=strlen(str)-2;start<end;start++,end--){
        char temp=str[start];
        str[start]=str[end];
        str[end]=temp;
    }
}

void *handle_client(void *arg){
    client_data_t* data=(client_data_t*)arg;

    printf("[CLIENT MESSAGE] %s",data->message);

    stringreverse(data->message);

    sendto(data->sockfd,data->message,strlen(data->message),0,(struct sockaddr*)&(data->client_addr),data->addr_len);

    free(data);
    pthread_exit(NULL);
}


int main(){

    int sockfd=socket(AF_INET,SOCK_DGRAM,0);
    char buff[BUFF_SIZE];

    struct sockaddr_in server_addr,client_addr;

    pthread_t thread_id;

    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    bind(sockfd,(struct sockaddr *)&server_addr,sizeof(server_addr));

    printf("[INFO] Server listening on port %d\n",PORT);

    while(1){
        socklen_t client_addr_len=sizeof(client_addr);
        ssize_t n = recvfrom(sockfd,buff,BUFF_SIZE,0,(struct sockaddr *)&client_addr,&client_addr_len);
        buff[n]='\0';

        client_data_t* data= (client_data_t*)malloc(sizeof(client_data_t));

        strcpy(data->message,buff);
        data->client_addr=client_addr;
        data->sockfd=sockfd;
        data->addr_len=client_addr_len;

        if(pthread_create(&thread_id,NULL,handle_client,(void*)data)!=0){
            perror("Failed to create thread");
            free(data);
        }
        pthread_detach(thread_id);
    }

    return 0;
}