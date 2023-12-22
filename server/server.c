#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <math.h>

#define PACKET_SIZE 508
#define ACK_SIZE 8
#define DATA_SIZE 500

#define SLOW_START 1
#define CONGESTION_AVOIDANCE 2
#define FAST_RECOVERY 3

int dup_ack_count = 0;
int packets_on_the_fly = 0;
int cwnd = 1;
int ssthresh = 8;
int last_ack_no;
int last_sent_seq;

typedef struct
{
    uint16_t checksum;//TODO: checksum implementation
    uint16_t len;
    uint32_t seq_no;
    char data[500];
} packet;

typedef struct
{
    uint16_t checksum;//TODO: checksum implementation
    uint16_t len;
    uint32_t seq_no;
} ack_packet;

packet create_packet(int data_len, int seq_no, char *data)
{
    packet p;
    p.checksum = 0;
    p.seq_no = seq_no;
    memset(p.data, 0, DATA_SIZE);
    memcpy(p.data, data, data_len);
    p.len = data_len;
    return p;
}

ack_packet create_ack_packet(int seq_no, int len)
{
    ack_packet p;
    p.checksum = 0;
    p.seq_no = seq_no;
    p.len = len;
    return p;
}

void send_pack(int socket_fd, packet *p, struct sockaddr_in client_addr) //TODO: probability to drop packet
{
    sendto(socket_fd, p, PACKET_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
}

int recv_ack(int socket_fd, ack_packet *ack, struct sockaddr_in client_addr)
{
    socklen_t addr_len = sizeof(client_addr);
    //TODO: reorganize the implementation to be a timer for each on the fly packet
    struct timeval timeout;
    timeout.tv_sec = 300; //FIXME: return to 0 after finishing
    timeout.tv_usec = 100000;//100 ms of wait
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));//if socket doesn't recieve a response in time val determined it returns -1 on recv
    return recvfrom(socket_fd, ack, ACK_SIZE, 0, (struct sockaddr *)&client_addr, &addr_len);
}

int slow_start(int *ackd_packets, int total_packets, int socket_fd, struct sockaddr_in client_addr, packet all_packets[])
{
    // while( there is still some packets not ack)
    while (*ackd_packets < total_packets)
    {
        // if(cwnd >= ssthtresh){return to congestion avoidance}
        if (cwnd >= ssthresh)
        {
            return CONGESTION_AVOIDANCE;
        }
        // get ack
        ack_packet *ack = malloc(ACK_SIZE);
        memset(ack, 0, ACK_SIZE);
        int timeout = recv_ack(socket_fd, ack, client_addr);
        // if timeout{ssthresh = cwnd/2; cwnd=1 ; dupAckCount=0; resend missing segment};
        if (timeout == -1)
        {
            free(ack);
            ssthresh = cwnd / 2;
            cwnd = 1;
            dup_ack_count = 0;
            // send missing packet
            last_sent_seq = last_ack_no + 1;
            send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
            continue;
        }
        int ack_no = ack->seq_no;
        free(ack);
        // if duplicate ack{dup_ack++}
        if (ack_no == last_ack_no)
        {
            dup_ack_count++;
        }
        // if(duplicateCount == 3){ssthresh = cwnd/2 ; cwnd=ssthresh+3MSS ; resend missing segment ;return to fast recovery}
        if (dup_ack_count == 3)
        {
            ssthresh = cwnd / 2;
            cwnd = ssthresh + 3; 
            // send missing packet
            last_sent_seq = last_ack_no + 1;
            send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
            return FAST_RECOVERY;
        }
        // if new ack {cwnd+=1 ;dup_ack =0 ;send new packet}
        if (ack_no > last_ack_no)
        {
            *ackd_packets += ack_no - last_ack_no;
            packets_on_the_fly -= ack_no - last_ack_no;
            last_ack_no = ack_no;
            cwnd = cwnd + 1; 
            dup_ack_count = 0;
            // send next packet;
            while(packets_on_the_fly<cwnd){
                last_sent_seq++;
                send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
                packets_on_the_fly++;
            }
        }
    }
    return -1;
}
int congestion_avoidance(int *ackd_packets, int total_packets, int socket_fd, struct sockaddr_in client_addr, packet all_packets[])
{
    // while( there is still some packets not ack)
    while (*ackd_packets < total_packets)
    {
        // get ack
        ack_packet *ack = malloc(ACK_SIZE);
        memset(ack, 0, ACK_SIZE);
        int timeout = recv_ack(socket_fd, ack, client_addr);
        // if timeout{ssthresh = cwnd/2; cwnd=1 ; dupAckCount=0; resend segment;return to slow start};
        if (timeout == -1)
        {
            free(ack);
            ssthresh = cwnd / 2;
            cwnd = 1;
            dup_ack_count = 0;
            // send missing packet
            last_sent_seq = last_ack_no + 1;
            send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
            return SLOW_START;
        }
        int ack_no = ack->seq_no;
        free(ack);
        // if dup ack{dupack++}
        if (ack_no == last_ack_no)
        {
            dup_ack_count++;
        }
        // if(duplicateCount == 3){ssthresh = cwnd/2 ; cwnd=ssthresh+3MSS ; resend segment ;return to fast recovery}
        if (dup_ack_count == 3)
        {
            ssthresh = cwnd / 2;
            cwnd = ssthresh + 3; 
            // send missing packet
            last_sent_seq = last_ack_no + 1;
            send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
            return FAST_RECOVERY;
        }
        // if new Ack {cwnd = cwnd+((MSS^2)/cwnd); dupack=0;send next segment}
        if (ack_no > last_ack_no)
        {
            cwnd = cwnd ;//+ 1 * (1 / cwnd); 
            dup_ack_count = 0;
            *ackd_packets += ack_no - last_ack_no;
            packets_on_the_fly -= ack_no - last_ack_no;
            last_ack_no = ack_no;
            // send next packet;
            while(packets_on_the_fly<cwnd){
                last_sent_seq++;
                send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
                packets_on_the_fly++;
            }
        }
    }
    return -1;
}
int fast_recovery(int *ackd_packets, int total_packets, int socket_fd, struct sockaddr_in client_addr, packet all_packets[])
{
    // while( there is still some packets not ack)
    while (*ackd_packets < total_packets)
    {
        // get ack
        ack_packet *ack = malloc(ACK_SIZE);
        memset(ack, 0, ACK_SIZE);
        int timeout = recv_ack(socket_fd, ack, client_addr);
        // if timeout{ssthresh = cwnd/2; cwnd=1 ; dupAckCount=0; resend missing segment ; return to slow start};
        if (timeout == -1)
        {
            free(ack);
            ssthresh = cwnd / 2;
            cwnd = 1;
            dup_ack_count = 0;
            // send missing packet
            last_sent_seq = last_ack_no + 1;
            send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
            return SLOW_START;
        }
        int ack_no = ack->seq_no;
        free(ack);
        // if dup ack {cwnd=cwnd+mss ; send new segment}
        if (ack_no == last_ack_no)
        {
            cwnd = cwnd + 1;
            while(packets_on_the_fly<cwnd){
                last_sent_seq++;
                send_pack(socket_fd, &all_packets[last_sent_seq], client_addr);
                packets_on_the_fly++;
            }
        }
        // if new ack {cwnd=ssthredh ; dupackCount=0;return to congestion avoid}
        if (ack_no > last_ack_no)
        {
            cwnd = ssthresh;
            dup_ack_count = 0;
            return CONGESTION_AVOIDANCE;
        }
    }
    return -1;
}

void udp_transfer(int total_packets, int socket_fd, struct sockaddr_in client_addr, packet all_packets[])
{
    // send packet
    // while there is still not ack packets
    // enter one state
    // if state returns switch to next state
    send_pack(socket_fd, &all_packets[0], client_addr);
    int ackd_packets = 0;
    last_ack_no = -1;
    int state = SLOW_START;
    while (true)
    {
        switch (state)
        {
        case SLOW_START:
            state = slow_start(&ackd_packets, total_packets, socket_fd, client_addr, all_packets);
            break;
        case CONGESTION_AVOIDANCE:
            state = congestion_avoidance(&ackd_packets, total_packets, socket_fd, client_addr, all_packets);
            break;
        case FAST_RECOVERY:
            state = fast_recovery(&ackd_packets, total_packets, socket_fd, client_addr, all_packets);
            break;
        default:
            packet p = create_packet(0, 0, "end");
            send_pack(socket_fd, &p, client_addr);
            return;
        }
    }
}
void read_file_into_packets(char *file_name, packet packets[])
{
    FILE *file = fopen(file_name, "r");
    int seq_no = 0;
    while (true)
    {
        char chunk[DATA_SIZE];
        memset(chunk, 0, DATA_SIZE);
        int size = fread(chunk, 1, DATA_SIZE, file);
        if (size < 0)
        {
            perror("can't read file");
        }
        if (size == 0)
        {
            break;
        }
        packet p = create_packet(size, seq_no, chunk);
        packets[seq_no++] = p;
    }
    fclose(file);
}

void send_file(char *file_name, int socket_fd, struct sockaddr_in client_addr)
{
    // //read file length
    struct stat st;
    int exists = stat(file_name, &st);
    if (exists != 0)
    {
        printf("[+] file does not exist\n");
        ack_packet ack_file_len = create_ack_packet(0, 0);
        int status = sendto(socket_fd, &ack_file_len, ACK_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (status < 0)
        {
            perror("can't send ack");
            exit(EXIT_FAILURE);
        }
        printf("[+] ack was sent successfully\n");
        return;
    }
    int size = st.st_size;
    // //prepare file in packets
    double number_of_packetsF = (double)size / DATA_SIZE;
    int number_of_packets = 0;
    double diff = number_of_packetsF - (int)number_of_packetsF;
    if (diff > 0)
    {
        number_of_packets = (int)number_of_packetsF + 1;
    }
    else
    {
        number_of_packets = (int)number_of_packetsF;
    }
    packet all_packets[number_of_packets];
    read_file_into_packets(file_name, all_packets);
    ack_packet ack_file_len = create_ack_packet(0, number_of_packets);
    int status = sendto(socket_fd, &ack_file_len, ACK_SIZE, 0, (struct sockaddr *)&client_addr, sizeof(client_addr));
    if (status < 0)
    {
        perror("can't send file size");
        exit(EXIT_FAILURE);
    }
    printf("[+] file length sent successfully\n");
    udp_transfer(number_of_packets, socket_fd, client_addr, all_packets);
}

int main(int argc, char const *argv[])
{
    // if(argc<2){
    //     perror("invalid args");
    //     exit(EXIT_FAILURE);
    // }
    // int server_port = atoi(argv[1]);

    // create listening socket
    int listening_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (listening_socket < 0)
    {
        perror("can't create socket");
        exit(EXIT_FAILURE);
    }
    printf("[+] server created listening socket %d\n", listening_socket);

    // bind the address to that port
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8081);
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

    int status = bind(listening_socket, (struct sockaddr *)&server, sizeof(server));
    if (status < 0)
    {
        perror("can't bind address to listening socket");
        close(listening_socket);
        exit(EXIT_FAILURE);
    }
    printf("[+] socket binded\n");
    while (true)
    {
        printf("[+] server waiting for requests\n");
        struct sockaddr_in client_addr;
        socklen_t address_len = sizeof(struct sockaddr);
        packet *request = malloc(PACKET_SIZE);
        int recvd_bytes = recvfrom(listening_socket, request, PACKET_SIZE, 0, (struct sockaddr *)&client_addr, &address_len);
        if (recvd_bytes < 0)
        {
            perror("can't recv request from socket");
            exit(EXIT_FAILURE);
        }
        // get packet data
        char file_name[DATA_SIZE];
        memset(file_name, 0, DATA_SIZE);
        memcpy(file_name, request->data, request->len);
        free(request);
        printf("[+] %s is requested \n", file_name);
        // pid_t pid = fork();
        // if(pid<0){
        //     perror("can't create child process");
        //     exit(EXIT_FAILURE);
        // }
        // else if(pid == 0){
        printf("child process in sending file\n");
        int child_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (child_socket < 0)
        {
            perror("can't create child socket");
            exit(EXIT_FAILURE);
        }
        // handle connection
        send_file(file_name, child_socket, client_addr);
        // }
    }
    close(listening_socket);
    // recive request from client
    return 0;
}