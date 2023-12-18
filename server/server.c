#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <math.h>

#define PACKET_SIZE 508
#define ACK_SIZE 8
#define DATA_SIZE 500
#define GBN 1
#define SELECTIVE_REPEAT 2

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

packet create_packet(int data_len,int seq_no,char* data){
    packet p ;
    p.checksum = 0;
    p.seq_no=seq_no;
    memset(p.data,0,DATA_SIZE);
    memcpy(p.data,data,data_len);
    p.len = data_len;
    return p;
}

ack_packet create_ack_packet(int seq_no){
    ack_packet p;
    p.checksum=0;
    p.seq_no=seq_no;
    p.len = 0;
    return p;
}

void read_file_into_packets(char* file_name,packet packets[]){
    FILE *file = fopen(file_name,"r");
    int seq_no=0;
    while(true){
        char chunk[DATA_SIZE];
        memset(chunk,0,DATA_SIZE);
        int size = fread(chunk,1,DATA_SIZE,file);
        if(size <0){
            perror("can't read file");
        }
        if(size==0){
            break;
        }
        packet p = create_packet(size,seq_no,chunk);
        packets[seq_no++]=p;
    }
    fclose(file);
}

void send_GBN(int socket_fd,struct sockaddr_in client_address,char* file_name){
    socklen_t addrlen = sizeof(client_address);
    // //read file length
    struct stat st;
    stat(file_name,&st);
    int size = st.st_size;
    // //prepare file in packets
    double number_of_packetsF = (double)size/DATA_SIZE;
    int number_of_packets = 0;
    double diff = number_of_packetsF - (int) number_of_packetsF;
    if(diff>0){
        number_of_packets = (int) number_of_packetsF+1;
    }
    else{
        number_of_packets=(int) number_of_packetsF;
    }
    packet packets[number_of_packets]; 
    read_file_into_packets(file_name,packets);

    //GBN state machine
    int next_seq_no=0;
    int window_start=0;
    int last_ack =-1;
    //send bunch of packets
    while(next_seq_no < 5){
        int status = sendto(socket_fd,&packets[next_seq_no],PACKET_SIZE,0,(struct sockaddr *)&client_address,sizeof(client_address));
        if(status<0){
            perror("can't send packet");
            break;
        }
        next_seq_no++;
    }
    //recieve acks
    while(last_ack<number_of_packets-1){
        //recieve ack_packet
        //check for seqno
        ack_packet *ack = malloc(ACK_SIZE);
        int rcv = recvfrom(socket_fd,ack,ACK_SIZE,0,(struct sockaddr *)&client_address,&addrlen);
        if(rcv<0){
            printf("timeout");
            continue;
        }
        if(ack->seq_no != window_start){
            //an ack that is out of order
            continue;
        }
        free(ack);
        //all is well
            //send packet for every ack
        int sent = sendto(socket_fd,&packets[next_seq_no],PACKET_SIZE,0,(struct sockaddr *)&client_address,addrlen);
        next_seq_no++;
        last_ack = window_start;
        window_start++;
    }
    packet empty = create_packet(0,next_seq_no,"");
    int sent = sendto(socket_fd,&empty,PACKET_SIZE,0,(struct sockaddr *)&client_address,addrlen);
}

void send_selective_repeat(int socket_fd,struct sockaddr_in client_address,char* file_name){
    
}

int main(int argc, char const *argv[])
{
    // if(argc<2){
    //     perror("invalid args");
    //     exit(EXIT_FAILURE);
    // }
    // int server_port = atoi(argv[1]);
    
    //create listening socket
    int listening_socket = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(listening_socket<0){
        perror("can't create socket");
        exit(EXIT_FAILURE);
    }
    printf("[+] server created listening socket %d\n",listening_socket);

    //bind the address to that port
    struct sockaddr_in server;
    server.sin_family=AF_INET;
    server.sin_port=htons(8080);
    inet_pton(AF_INET,"127.0.0.1",&server.sin_addr);

    int status = bind(listening_socket, (struct sockaddr *)&server,sizeof(server));
    if(status<0){
        perror("can't bind address to listening socket");
        close(listening_socket);
        exit(EXIT_FAILURE);
    }
    printf("[+] socket binded\n");
    while(true){
        printf("[+] server waiting for requests\n");
        struct sockaddr_in client_addr;
        socklen_t address_len = sizeof(struct sockaddr);
        packet *request = malloc(PACKET_SIZE);
        int recvd_bytes = recvfrom(listening_socket,request,PACKET_SIZE,0,(struct sockaddr *)&client_addr,&address_len);
        if(recvd_bytes<0){
            perror("can't recv request from socket");
            exit(EXIT_FAILURE);
        }
        //checksum calculate
        //get packet data
        char file_name[DATA_SIZE];
        memset(file_name,0,DATA_SIZE);
        memcpy(file_name,request->data,request->len);
        free(request);
        printf("[+] %s is requested \n",file_name);
        pid_t pid = fork();
        if(pid<0){
            perror("can't create child process");
            exit(EXIT_FAILURE);
        }
        else if(pid == 0){
            printf("child process in sending file\n");
            int child_socket = socket(AF_INET, SOCK_DGRAM,IPPROTO_UDP);
            if(child_socket < 0){
                perror("can't create child socket");
                exit(EXIT_FAILURE);
            }
            //handle connection
            send_GBN(child_socket,client_addr,file_name);
        }
    }
    close(listening_socket);
    //recive request from client
    return 0;
}

