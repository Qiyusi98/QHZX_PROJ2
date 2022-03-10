#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

unsigned int crc32b(char *message, long message_len) {
  int i, j;
    unsigned int byte, crc, mask;

    i = 0;
    crc = 0xFFFFFFFF;
    // while (message[i] != 0) {
    while (i < message_len) {
        if (message[i] == 0)
        {
            i = i + 1;
            continue;
        }
        byte = message[i];            // Get next byte.
        crc = crc ^ byte;
        for (j = 7; j >= 0; j--) {    // Do eight times.
            mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
        i = i + 1;
   }
   return ~crc;
}

int main(int argc, char **argv) {

  /* check number of command line parameters */
  if (argc != 5) {
    printf(
        "Invalid command line input, should have four parameters \
            in the format of -r <recv host>:<recv port> -f <subdir>/<filename>.");
    exit(1);
  }

  /* command line input */
  char *receiver_info_input = argv[2];
  char *file_info_input = argv[4];

  char *host;
  unsigned short port;
  char *subdir;
  char *filename;

  /* sendfile socket */
  int sockfd;
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;

  /* variables for identifying the server */
  struct sockaddr_in sin;
  socklen_t addr_len = sizeof(struct sockaddr_in);
  
  /* packet info */
  const int DATA_LEN = 25000;
  const int FILE_LEN = 60;
  const int PACKET_LEN = 2;
  const int CRC_LEN = 4;
  int total_size = DATA_LEN + FILE_LEN + PACKET_LEN + CRC_LEN;

  short RECEIVE_SIZE = 3;
  char *send_buffer;
  char *receive_buffer;
  char send_id;
  char receive_id;
  unsigned int crc;
  int send_count;
  int count;
  int receive_count;

  /* handle file */
  FILE *fptr;
  unsigned long file_len_total;
  char file_path[sizeof(file_info_input)];
  char file_path_padding[FILE_LEN];
  char *file_data;
  short bytes_read;

  /* split terminal input to get receiver and file information */
  char *receiver_info = strtok(receiver_info_input, ":");
  if (receiver_info == NULL) {
    printf(
        "Invalid command line input, should contain host and port\
              and in the format of host:port.");
    exit(1);
  }
  host = receiver_info;
  receiver_info = strtok(NULL, ":");
  if (receiver_info == NULL) {
    printf(
        "Invalid command line input, should contain host and port\
              and in the format of host:port.");
    exit(1);
  }
  port = atoi(receiver_info);

  strcpy(file_path, file_info_input);
  char *file_info = strtok(file_info_input, "/");
  if (file_info == NULL) {
    printf(
        "Invalid command line input, should contain subdirectory and filename\
              and in the format of subdir/filename.");
    exit(1);
  }
  subdir = file_info;
  file_info = strtok(NULL, "/");
  if (file_info == NULL) {
    printf(
        "Invalid command line input, should contain subdirectory and filename\
              and in the format of subdir/filename.");
    exit(1);
  }
  filename = file_info;
  printf("Input from terminal %s, %hu, %s, %s\n", host, port, subdir, filename);

  /* create a socket */
  if ((sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    perror("opening UDP socket error");
    abort();
  }
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
    perror("setting UDP socket option error");
    abort();
  }

  /* fill in the receiver's address */
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = inet_addr(host);
  sin.sin_port = htons(port);

  /* initialize variables */
  send_id = (char)0;
  strncpy(file_path_padding, file_path, FILE_LEN);
  send_buffer = malloc(total_size);
  receive_buffer = malloc(RECEIVE_SIZE);

  /* operate send file */
  fptr = fopen(file_path, "rb");
  if (!fptr) {
    printf("open file error");
    abort();
  }
  // fseek(fptr, 0L, SEEK_END);
  // file_len_total = ftell(fptr);
  // fseek(fptr, 0L, SEEK_SET);
  // printf("file length is %lu\n", file_len_total);

  file_data = (char *)malloc(total_size * sizeof(char));
  while ((bytes_read = fread(file_data, 1, DATA_LEN, fptr)) > 0) {
    memset(send_buffer, 0, total_size);

    // TODO packet design may change
    memset(send_buffer, 0, 1);
    memset(send_buffer + 1, send_id, 1);
    *(short *)(send_buffer + 2) = htons(bytes_read);
    memcpy(send_buffer + 4, file_path_padding, FILE_LEN);
    memcpy(send_buffer + 64, file_data, DATA_LEN);

    memset(file_data, 0, DATA_LEN);

    crc = crc32b(send_buffer, DATA_LEN + FILE_LEN + PACKET_LEN);
    memcpy(send_buffer + (DATA_LEN + FILE_LEN + PACKET_LEN), &crc, CRC_LEN);

    while (1) {
      send_count = 0;
      while (send_count < total_size) {
        count = sendto(sockfd, (const char *)send_buffer, total_size, 0, (const struct sockaddr *)&sin, sizeof(sin));
        if (count <= 0) {
          perror("send socket error");
          abort();
        }
        send_count += count;
      }

      receive_count = recvfrom(sockfd, receive_buffer, RECEIVE_SIZE, MSG_WAITALL, (struct sockaddr *)&sin, &addr_len);
      if (receive_count <= 0) {
        perror("receive ack packet from sendfile error");
      } else {
        receive_id = receive_buffer[1];
        if ((char)receive_id == (char)send_id) {

          // TODO change to sequence number
          if (send_id == 1) {
            send_id = (char)0;
          } else if (send_id == 0) {
            send_id = (char)1;
          }
          break;
        }
      }
    }
  }

  printf("[completed]\n");
  close(sockfd);
  return 0;
}

