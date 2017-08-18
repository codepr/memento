#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include "networking.h"

#define CMD_BUFSIZE 1024
#define VERSION "0.0.1"

void banner(void);
void help(void);
void command_loop(int, char *, char *);
char *read_command(void);

int main(int argc, char **argv) {
    int sock_fd, port;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;

    /* check command line arguments */
    if (argc != 3) {
        fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    port = atoi(argv[2]);

    /* socket: create the socket */
    /* sock_fd = socket(AF_INET, SOCK_STREAM, 0); */
    /* if (sock_fd < 0) */
    /*     perror("ERROR opening socket"); */
    /*  */
    /* #<{(| gethostbyname: get the server's DNS entry |)}># */
    /* server = gethostbyname(hostname); */
    /* if (server == NULL) { */
    /*     fprintf(stderr,"ERROR, no such host as %s\n", hostname); */
    /*     exit(0); */
    /* } */
    /*  */
    /* #<{(| build the server's address |)}># */
    /* bzero((char *) &serveraddr, sizeof(serveraddr)); */
    /* serveraddr.sin_family = AF_INET; */
    /* bcopy((char *)server->h_addr, */
    /*       (char *)&serveraddr.sin_addr.s_addr, server->h_length); */
    /* serveraddr.sin_port = htons(port); */
    /*  */
    /* #<{(| connect: create a connection with the server |)}># */
    /* if (connect(sock_fd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) { */
    /*     perror("ERROR connecting"); */
    /*     exit(0); */
    /* } */

    char c_port[4];
    sprintf(c_port, "%d", port);
    banner();
    sock_fd = connectto(hostname, c_port);
    command_loop(sock_fd, hostname, c_port);
    return EXIT_SUCCESS;
}

// loop for accepting, parsing and sending commands to the memento server
// instance, reading response
void command_loop(int fd, char *hostname, char *port) {
    char *line, buf[CMD_BUFSIZE];
    int status = 1;
    // while condition: status > 0
    while (status > 0) {
        printf("%s:%s> ", hostname, port);
        line = read_command();
        char *supp_command = strdup(line);
        char *comm = strtok(supp_command, " ");
        if (strlen(line) > 1) {
            char token[4];
            strncpy(token, line, 4);
            if (strncasecmp(comm, "KEYS", 4) == 0 ||
                    strncasecmp(comm, "VALUES", 6) == 0 ||
                    strncasecmp(comm, "KEYSPACE", 8) == 0) {
                fcntl(fd, F_SETFL, O_NONBLOCK);
                status = send(fd, line, strlen(line), 0);
                /* usleep(100000); */
                if (status < 0)
                    perror("ERROR writing to socket");
                while(recv(fd, buf, CMD_BUFSIZE, 0) > 0) {
                    printf("%s", buf);
                    bzero(buf, CMD_BUFSIZE);
                }
                /* fcntl(fd, F_SETFL, O_BLOCK); */
            } else if (token[0] == '?' ||
                    token[0] == 'h' ||
                    token[0] == 'H' ||
                    strncasecmp(token, "HELP", 4) == 0) {
                help();
                bzero(buf, CMD_BUFSIZE);
            } else {
                // write to server
                status = send(fd, line, strlen(line), 0);
                if (status < 0)
                    perror("ERROR writing to socket");
                /* print the server's reply */
                bzero(buf, CMD_BUFSIZE);
                status = recv(fd, buf, CMD_BUFSIZE, 0);
                if (status < 0)
                    perror("ERROR reading from socket");
                printf("%s", buf);
            }
            free(supp_command);
            free(line);
        }
    }
}
// read a single string containing command
char *read_command(void) {
    char *line = NULL;
    size_t bufsize = 0;
    getline(&line, &bufsize, stdin);
    return line;
}
// print initial banner
void banner(void) {
    printf("\n");
    printf("Memento CLI v %s\n", VERSION);
    printf("Type h or ? or help to print command list\n\n");
}
// print help
void help(void) {
    printf("\n");
    printf("SET key value               Sets <key> to <value>\n");
    printf("GET key                     Get the value identified by <key>\n");
    printf("DEL key key2 .. keyN        Delete values identified by <key>..<keyN>\n");
    printf("INC key qty                 Increment by <qty> the value idenfied by <key>, if\n");
    printf("                            no <qty> is specified increment by 1\n");
    printf("DEC key qty                 Decrement by <qty> the value idenfied by <key>, if\n");
    printf("                            no <qty> is specified decrement by 1\n");
    printf("INCF                        key qty Increment by float <qty> the value identified\n");
    printf("                            by <key>, if no <qty> is specified increment by 1.0\n");
    printf("DECF                        key qty Decrement by <qty> the value identified by <key>,\n");
    printf("                            if no <qty> is specified decrement by 1.0\n");
    printf("GETP key                    Get all information of a key-value pair represented by\n");
    printf("                            <key>, like key, value, creation time and expire time\n");
    printf("APPEND key value            Append <value> to <key>\n");
    printf("PREPEND key value           Prepend <value> to <key>\n");
    printf("FLUSH                       Delete all maps stored inside partitions\n");
    printf("QUIT/EXIT                   Close connection\n");
    printf("\n");
}
