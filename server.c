#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include "packet.h"

#define BACKLOG 5
#define WINDOW_MAX_SIZE 8

#define DEBUG {printf("%d ", __LINE__); fflush(stdout);}

void error(char *msg)
{
  perror(msg);
  exit(1);
}

int is_connected(int sockfd)
{
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLHUP | POLLNVAL;
  if (poll(&pfd, 1, 0) < 0)
    error("Error : polling");
  return !(pfd.revents & (POLLHUP | POLLNVAL));
}

int is_readable(int sockfd)
{
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLIN;
  if (poll(&pfd, 1, 0) < 0)
    error("Error : polling");
  return (pfd.revents & POLLIN);
}

int main (int argc, char **argv) {

  // Check arguments
  if (argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int server_sockfd, client_sockfd, res;
  int file_n, window_size, port = atoi(argv[1]);

  packet serv_packet;
  ACK ack_packet;

  // define server address
  struct sockaddr_in client_addr, server_addr;
  bzero((char *) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY; // automatically find server ip
  server_addr.sin_port = htons(port); // change byte order for network

  // create socket
  server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_sockfd < 0)
    error("Error : opening socket");

  // bind socket
  if (bind(server_sockfd, (struct sockaddr*) &server_addr, 
        sizeof(server_addr)) < 0)
    error("Error : binding socket");
  printf("Binding socket...\n");

  listen(server_sockfd, BACKLOG);

  socklen_t client_len = sizeof(client_addr);
  client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr,
      &client_len);
  if (client_sockfd < 0)
    error("Error : accepting client");
  printf("Accepted client\n");

  while (1){
    // accept client 
    if (!is_readable(client_sockfd)) continue;

    res = read(client_sockfd, (char *)&ack_packet, sizeof(ack_packet));
    if (res < 0)
      error("Error : reading socket");    
    file_n = ack_packet.file_n;
    window_size = ack_packet.window_size;
    if (file_n < 0)
      break;
    
    // open file to transfer
    char file_name[64];
    sprintf(file_name, "data/TransferMe%d0.mp4", file_n);
    FILE *fs = fopen(file_name, "r");
    int32_t sendN = 0, max_sendN = window_size - 1, min_sendN = 0;
    int read_size, total_size = 0, prev_progress, curr_progress;
    bzero((char *) &serv_packet, sizeof(serv_packet));
    bzero((char *) &ack_packet, sizeof(ack_packet));

    // transfer file 
    while ((read_size = fread((char *) &(serv_packet.data), 
          sizeof(char), sizeof(serv_packet.data), fs)) > 0)
    {
      while (1)
      {
        if (is_readable(client_sockfd))
        {
          res = read(client_sockfd, (char *)&ack_packet, sizeof(ack_packet));
          if (res < 0)
            error("Error : reading socket");
          max_sendN++;
          min_sendN++;
        }
        
        if (sendN <= max_sendN)
        {
          serv_packet.packet_n = sendN;
          res = write(client_sockfd, (char *) &serv_packet, sizeof(serv_packet));
          if (res < 0)
            error("Error : writing to socket");
          prev_progress = total_size / 1048576 ;
          curr_progress = (total_size + read_size) / 1048576;
          if (prev_progress != curr_progress)
            printf("%d MB transmitted\n", curr_progress);
          total_size += read_size;
          sendN++;
          break;
        }
      }
    }
    // wait for ack
    while (min_sendN < sendN)
    {
      res = read(client_sockfd, (char *)&ack_packet, sizeof(ack_packet));
      if (res < 0)
        error("Error : reading socket");
      min_sendN++;
    }
    
    serv_packet.packet_n = -1;
    res = write(client_sockfd, (char *) &serv_packet, sizeof(serv_packet));
    if (res < 0)
      error("Error : writing to socket");
    printf("transmission ended\n");
    fclose(fs);
  }
  printf("close server...\n");
  if (is_connected(client_sockfd))
    close(client_sockfd);
  close(server_sockfd);
  return 0;
}
