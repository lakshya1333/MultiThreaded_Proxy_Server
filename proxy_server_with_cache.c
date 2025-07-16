#include "proxy_parse.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#define MAX_CLIENTS 10
#define MAX_BYTES 4096

typedef struct cache_element cache_element;

struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* ele;
};



cache_element* find(char* url);
int add_cache_element(char* data,int size,char* url);
void remove_cache_element();

int port_number = 8080;
int proxy_socketId;

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

cache_element* head;
int cache_size;

void *thread_fn(void *socketNew){
    sem_wait(&semaphore); //sem_wait do -1 and checks if it is negative it waits otherwise moves forward
    int p;
    sem_getvalue(&semaphore,&p);
    printf("Semaphore Value is %d\n",p);
    int *t = (int *)socketNew;
    int socket = *t; //Dereferencing the pointer to get the value
    int bytes_send_client,len;

    char *buffer = (char *)calloc(MAX_BYTES,sizeof(char)); //We are allocating a chunk of memory (dynamically) to store the data received from the client.
    bzero(buffer,MAX_BYTES);
    bytes_send_client = recv(socket,buffer,MAX_BYTES,0); //It recieves data from client via socket

    while(bytes_send_client > 0){
        len = strlen(buffer);
        if(strstr(buffer,"\r\n\r\n") == NULL){ //"\r\n\r\n" is the end-of-header marker in an HTTP request.
            bytes_send_client = recv(socket,buffer+len,MAX_BYTES-len,0); 
        }else{
            break;
        }
    }

    char *tempReq = (char *)malloc(strlen(buffer)*sizeof(char)+1);
    for(int i = 0;i<strlen(buffer);i++){
        tempReq[i] = buffer[i];
    }

    struct cache_element* temp = find(tempReq);
    if(temp != NULL){
        int size = temp->len/sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while(pos < size){
            bzero(response,MAX_BYTES);
            for(int i = 0;i<MAX_BYTES;i++){
                response[i] = temp->data[i];
                pos++;
            }
            send(socket,response,MAX_BYTES,0);
        }
        printf("Data Retrieved from the cache\n");
        printf("%s\n\n",response);
    }else if(bytes_send_client > 0){
        len = strlen(buffer);
        ParsedRequest *request = ParsedRequest_create();
        if(ParsedRequest_parse(request,buffer,len)<0){
            printf("Parsing Failed.\n");
        }else{
            bzero(buffer,MAX_BYTES);
            if(!strcmp(request->method,"GET")){
                if(request->host && request->path && checkHTTPversion(request->version) == 1){
                    bytes_send_client = handle_request(socket,request,tempReq);
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket,500);
                    }
                }else{
                    sendErrorMessage(socket,500);
                }
            }
        }
    }

}

int main(int argc,char* argv[]){
    int client_socketId, client_len;
    struct sockaddr_in server_addr, client_addr;
    sem_init(&semaphore,0,MAX_CLIENTS);
    pthread_mutex_init(&lock,NULL);
    if(argv == 2){
        port_number = atoi(argv[1]);
    }else{
        printf("Too few arguments\n");
        exit(1);
    }


    printf("Starting proxy server at port: %d\n",port_number);
    proxy_socketId = socket(AF_INET,SOCK_STREAM,0); //Creating a TCP socket over IPv4.
    if (proxy_socketId < 0) {
        perror("Socket creation failed\n");
        exit(1);
    }
    int reuse = 1;
    if(setsockopt(proxy_socketId,SOL_SOCKET,SO_REUSEADDR,(const char*)&reuse,sizeof(reuse)<0)){
        perror("Setsockopt failed\n");
    } 
    bzero((char *)& server_addr,sizeof(server_addr));
    server_addr.sin_family = AF_INET; //IPv4
    server_addr.sin_port = htons(port_number); //convert to the port which can be understand by the network
    server_addr.sin_addr.s_addr = INADDR_ANY;
    if(bind(proxy_socketId,(struct sockaddr*)&server_addr,sizeof(server_addr)<0)){
        perror("Port is not available");
        exit(1);
    }
    printf("Binding on port %d\n",port_number);
    int listen_status = listen(proxy_socketId,MAX_CLIENTS);
    if(listen_status < 0){
        perror("Error in listening");
        exit(1);
    }
    int i = 0;
    int Connected_SocketId[MAX_CLIENTS]; //For checking how many are connected to my socket

    while(1){
        bzero((char *)&client_addr,sizeof(client_addr)); //clears the garbage value
        client_len = sizeof(client_addr);
        client_socketId = accept(proxy_socketId,(struct sockaddr *)&client_addr,(socklen_t*)&client_len);
        if(client_socketId < 0){
            printf("Not able to connect");
            exit(1);
        }else{
            Connected_SocketId[i] = client_socketId;
        }

        struct sockaddr_in * client_pt = (struct sockaddr_in *)& client_addr;
        struct in_addr ip_addr = client_pt->sin_addr; //sin_addr holds the ip address of the connected client but this is in a binary format which humans cant understand
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET,&ip_addr,str,INET6_ADDRSTRLEN); //convert not readable ip address to human readable ip address
        printf('Client is connected to port %d and ip address %s\n',ntohs(client_addr.sin_port),str);
        pthread_create(&tid[i],NULL,thread_fn,(void *)&Connected_SocketId[i]);
        i++;
    } 
    close(proxy_socketId);
    return 0;
}




