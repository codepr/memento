#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define CMD_BUFSIZE 10485760

void command_loop(int, char *, char *);
char *read_command();

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
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
        perror("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(port);

    /* connect: create a connection with the server */
    if (connect(sock_fd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        perror("ERROR connecting");

    char c_port[4];
    sprintf(c_port, "%d", port);
    command_loop(sock_fd, hostname, c_port);
    return EXIT_SUCCESS;
}

void command_loop(int fd, char *hostname, char *port) {
    char *line, buf[CMD_BUFSIZE];
    int status = 1;

    while (status) {
        printf("%s:%s> ", hostname, port);
        line = read_command();
        if (strlen(line) > 1) {
            // write to server
            status = write(fd, line, strlen(line));
            if (status < 0)
                perror("ERROR writing to socket");

            char cmd[4];
            strncpy(cmd, line, 4);
            if (strcasecmp(cmd, "SUB ") == 0 || strcasecmp(cmd, "TAIL") == 0) {
                while (1) {
                    /* print the server's reply */
                    bzero(buf, CMD_BUFSIZE);
                    status = read(fd, buf, CMD_BUFSIZE);
                    printf("%s", buf);
                }
            } else {
                /* print the server's reply */
                bzero(buf, CMD_BUFSIZE);
                status = read(fd, buf, CMD_BUFSIZE);
            }
            if (status < 0)
                perror("ERROR reading from socket");
            printf("%s", buf);
            free(line);
        }
    }
}

char *read_command() {
    char *line = NULL;
    size_t bufsize = 0;
    getline(&line, &bufsize, stdin);
    return line;
}
