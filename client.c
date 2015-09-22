#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <netdb.h>
#include <ctype.h>
#include <errno.h>
#include "packet.h"

#define NTIMERS 32
#define DEBUG {printf("%d ", __LINE__); fflush(stdout);}

int sockfd = -1;
int acks = 0;

void error(char *msg)
{
  perror(msg);
  exit(1);
}

void handler(int signo);
timer_t set_timer(long long);

int is_connected(int sockfd)
{
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLHUP;
  if (poll(&pfd, 1, 0) < 0)
    error("Error : polling");
  return !(pfd.revents & POLLHUP);
}

long long get_time()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  long long time_in_milli = (tv.tv_sec) * 1000 + (tv.tv_usec) / 1000 ;
  return time_in_milli;
}

int main (int argc, char **argv) {

  // Check arguments 
  if (argc != 5) {
    printf("Usage: %s <hostname> <port> <window size> <delay>\n", argv[0]);
    printf("\tMandatory Arguments:\n");
    printf("\t<window size> : size of the window used at server\n");
    printf("\t<delay> : ACK delay in msec");
    exit(1);
  }
  int port = atoi(argv[2]), window_size = atoi(argv[3]), delay = atoi(argv[4]);

  // Set Handler for timers
  struct sigaction sigact;
  sigemptyset(&sigact.sa_mask);
  sigaddset(&sigact.sa_mask, SIGALRM);
  sigact.sa_handler = &handler;
  sigact.sa_flags = 0;
  if (sigaction(SIGALRM, &sigact, NULL) < 0)
    error("sigaction error");
  
  // set host addr
  struct sockaddr_in server_addr;
  struct hostent *server;
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    error("Error : opening socket");
  if ((server = gethostbyname(argv[1])) == NULL)
    error("Error : no such host");
  bzero((char *) &server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr,
      server->h_length);
  server_addr.sin_port = htons(port);

  char command[64];

  while(1)
  {
    printf("Enter command\n");
    bzero(command, sizeof(command));
    fflush(stdin);
    fgets(command, sizeof(command), stdin);
    switch (tolower(command[0]))
    {
      case 'c':
        {
          if (is_connected(sockfd))
            printf("already connected to the server\n");
          else if (connect(sockfd, (struct sockaddr *) &server_addr, 
                sizeof(server_addr)) < 0)
            error("Error : connecting");
          break;
        }
      case 'r':
        {
          int file_n = atoi(&(command[1]));
          int recv_n = 0;
          if (file_n < 1 || file_n > 3)
          {
            printf("file number must be 1, 2 or 3\n");
            break;
          }
          ACK req_packet;
          req_packet.window_size = window_size;
          req_packet.file_n = file_n;

          long long elapsed_time = get_time();

          int n = write(sockfd, (char *)&req_packet, sizeof(req_packet));
          if (n < 0)
            error("Error : writing socket");
          int recv_size, total_size = 0, prev_progress, curr_progress;
          packet recv_packet;
          while (1)
          {
            bzero((char *) &recv_packet, sizeof(recv_packet));
            recv_size = read(sockfd, (char *) &recv_packet, sizeof(recv_packet));
            if (recv_size < 0)
            {
              if (errno == EINTR) continue;
              else error("Error : reading socket");
            }

            if (recv_packet.packet_n == -1)
              break;

            recv_n++;
            recv_size -= 4;
            set_timer(delay);
            prev_progress = total_size / 1048576 ;
            curr_progress = (total_size + recv_size) / 1048576;
            if (prev_progress != curr_progress)
              printf("%d MB transmitted\n", curr_progress);
            total_size += recv_size;
          }
          elapsed_time = get_time() - elapsed_time;
          double throughput = ((double)total_size / 1024) / ((double)elapsed_time / 1000);
          printf("%d MB (%d bits) transmission finished\n", curr_progress, total_size);
          printf("Elapsed time is %d msec and throughput is %f Kbps.\n", 
            (int)elapsed_time, throughput);
          break;
        }
      case 'f':
        {
          ACK req_packet;
          req_packet.file_n = -1;
          while (1)
          {
            if(write(sockfd, (char *) &req_packet, sizeof(req_packet)) < 0)
            {
              if (errno == EINTR) continue;
              else error("Error : writing finish packet");
            } else
              break; 
          }
          close(sockfd);
          sockfd = -1;
          printf("Connection terminated\n");
          return 0;
        }
      default:
        {
          printf("unknown command\n");
          printf("C : connect to server\n");
          printf("R <n> : Request to server to transmit file number n\n");
          printf("F : finish the connection to the server\n");
        }
    }
  }
  return 0;
}

/*
 * handler()
 * Invoked by a timer.
 * Send ACK to the server
 */
void handler(int signo) {
  ACK req_packet;
  bzero((char *) &req_packet, sizeof(req_packet));
  while (1)
  {
    int n = write(sockfd, (char *)&req_packet, sizeof(req_packet));
    if (n < 0)
    {
      if (errno == EINTR) continue;
      error("Error : writing socket");
    }
    else 
      return;
  }
}

/*
 * set_timer()
 * set timer in msec
 */

timer_t set_timer(long long msec) {
  static timer_t timers[NTIMERS];
  static int initialized = 0;
  static int timer_index = 0;

  if (!initialized) 
  {
    for (timer_index = 0; timer_index < NTIMERS; timer_index++)
      if (timer_create(CLOCK_MONOTONIC, NULL, timers + timer_index))
        perror("timer_create");
    initialized = 1;
    timer_index = 0;
  }

  struct itimerspec time_spec = {.it_interval = {.tv_sec=0,.tv_nsec=0},
    .it_value = {.tv_sec=0,.tv_nsec=0}};

  int sec = msec / 1000;
  long n_sec = (msec % 1000) * 1000000;
  time_spec.it_value.tv_sec = sec;
  time_spec.it_value.tv_nsec = n_sec;

  int idx = timer_index;
  if (timer_settime(timers[timer_index], 0, &time_spec, NULL))
    perror("timer_settime");
  else 
    timer_index = (timer_index + 1) % NTIMERS;

  return timers[idx];
}
