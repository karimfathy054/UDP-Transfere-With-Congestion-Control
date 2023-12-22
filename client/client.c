#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define PACKET_SIZE 508
#define ACK_SIZE 8
#define DATA_SIZE 500

typedef struct{
    uint16_t checksum;
    uint16_t len;
    uint32_t seq_no;
    char data[500];
}packet;

typedef struct{
    uint16_t checksum;
    uint16_t len;
    uint32_t seq_no;
}ack_packet;

packet create_packet(int msg_len,int seq_no,char* data){
    packet p ;
    p.checksum = 0;
    p.seq_no=seq_no;
    memset(p.data,0,DATA_SIZE);
    memcpy(p.data,data,msg_len);
    p.len = msg_len;
    return p;
}

ack_packet create_ack_packet(int seq_no){
    ack_packet p;
    p.checksum=0;
    p.seq_no=seq_no;
    p.len = 0;
    return p;
}
void send_ack(int socket_fd,ack_packet *ack,struct sockaddr_in server_addr){//TODO: add propablility to lose the ack
    sendto(socket_fd,ack,ACK_SIZE,0,(struct sockaddr *)&server_addr,sizeof(server_addr));
}
void recv_pack(int socket_fd,packet *p,struct sockaddr_in server_addr){
    socklen_t addr_len = sizeof(server_addr);
    recvfrom(socket_fd,p,PACKET_SIZE,0,(struct sockaddr *)&server_addr,&addr_len);
}
void recv_GBN(int socket_fd,struct sockaddr_in server_address,char* file_name){
    socklen_t addr_len = sizeof(server_address);
    FILE *file = fopen(file_name,"w");
    int expecting_seqno=0;
    int last_ack=-1;
    while(true){
        packet *data_packet = malloc(PACKET_SIZE);
        memset(data_packet,0,PACKET_SIZE);
        int recvd = recvfrom(socket_fd,data_packet,PACKET_SIZE,0,(struct sockaddr *)&server_address,&addr_len);
        if(recvd<0){
            perror("connection lost");
        }
        if(data_packet->len==0){
            break;
        }
        if(data_packet->seq_no == expecting_seqno){
            fwrite(data_packet->data,1,data_packet->len,file);
            ack_packet ack = create_ack_packet(expecting_seqno);
            int status = sendto(socket_fd,&ack,ACK_SIZE,0,(struct sockaddr *)&server_address,addr_len);
            expecting_seqno++;
            last_ack=data_packet->seq_no;
            free(data_packet);
        }
        else{
            ack_packet ack = create_ack_packet(last_ack);
            int status = sendto(socket_fd,&ack,ACK_SIZE,0,(struct sockaddr *)&server_address,addr_len);
        }
    }
    fclose(file);
}


int main(int argc, char const *argv[])
{
    //take args
    // if(argc < 3){
    //     perror("invalid args");
    //     exit(EXIT_FAILURE);
    // }
    // char *server_address = argv[1];
    // int server_port = atoi(argv[2]);
    // char *file_name = argv[3];

    char *server_address = "127.0.0.1";
    int server_port = 8081;
    char *file_name = "Alice.txt";
    //create socket
    int socket_fd = socket(AF_INET,SOCK_DGRAM,0);
    if(socket_fd<0){
        perror("can't create socket");
        exit(EXIT_FAILURE);
    }
    printf("[+] client created socket %d",socket_fd);
    //connect to server
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(server_port);
    inet_pton(AF_INET,server_address,&server.sin_addr);

    //send file name request
    packet init = create_packet(strlen(file_name),0,file_name);
    int status = sendto(socket_fd,&init,PACKET_SIZE,0,(struct sockaddr *)&server,sizeof(server));
    socklen_t addr_len = sizeof(server);
    ack_packet *ack = malloc(ACK_SIZE);
    int recvd = recvfrom(socket_fd,ack,ACK_SIZE,0,(struct sockaddr *)&server,&addr_len); 
    int file_len = ack->len;
    free(ack);
    if(file_len = 0){
        printf("[+] server does not have the file\n");
    }
    else{
        //recieve file
        recv_GBN(socket_fd,server,"Alice.txt");
    }
    //close socket
    close(socket_fd);
    return 0;
}




