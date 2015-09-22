#ifndef __PACKET_H_
#define __PACKET_H_

#define BUFFER_SIZE 1000  // same as packet size
#define DATA_SIZE 996
#define ACK_SIZE 8

typedef struct ACK
{
  int32_t file_n;
  int32_t window_size;
} ACK;

typedef struct packet
{
  int32_t packet_n;
  char data[DATA_SIZE];
} packet;

#endif
