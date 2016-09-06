#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define CMD_BUFSIZE 1024
#define VERSION "0.0.1"

void banner();
void help();
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

    /* build the server's address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr,
          (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(port);

    /* connect: create a connection with the server */
    if (connect(sock_fd, (const struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
        perror("ERROR connecting");
        exit(0);
    }

    char c_port[4];
    sprintf(c_port, "%d", port);
    banner();
    command_loop(sock_fd, hostname, c_port);
    return EXIT_SUCCESS;
}

// loop for accepting, parsing and sending commands to the shibui server
// instance, reading response
void command_loop(int fd, char *hostname, char *port) {
    char *line, buf[CMD_BUFSIZE];
    int status = 1;
    // while condition: status > 0
    while (status) {
        printf("%s:%s> ", hostname, port);
        line = read_command();
        if (strlen(line) > 1) {
            char token[4];
            strncpy(token, line, 4);
            if (strncasecmp(token, "SUB ", 4) == 0 || strncasecmp(token, "TAIL", 4) == 0) {
                // write to server
                status = write(fd, line, strlen(line));
                if (status < 0)
                    perror("ERROR writing to socket");
                while (1) {
                    /* print the server's reply */
                    bzero(buf, CMD_BUFSIZE);
                    status = read(fd, buf, CMD_BUFSIZE);
                    if (strncasecmp(buf, "NOT FOUND", 9) == 0) break;
                    printf("%s", buf);
                }
            } else if (token[0] == '?' ||
                       token[0] == 'h' ||
                       token[0] == 'H' ||
                       strncasecmp(token, "HELP", 4) == 0) {
                help();
                bzero(buf, CMD_BUFSIZE);
            } else {
                // write to server
                status = write(fd, line, strlen(line));
                if (status < 0)
                    perror("ERROR writing to socket");
                /* print the server's reply */
                bzero(buf, CMD_BUFSIZE);
                while (status > 0) {
                    status = read(fd, buf, CMD_BUFSIZE);
                    if (status < 0)
                        perror("ERROR reading from socket");
                    printf("%s", buf);
                }
            }
            free(line);
        }
    }
}
// read a single string containing command
char *read_command() {
    char *line = NULL;
    size_t bufsize = 0;
    getline(&line, &bufsize, stdin);
    return line;
}
// print initial banner
void banner() {
    printf("\n");
    printf("Shibui CLI v %s\n", VERSION);
    printf("Type h or ? or help to print command list\n\n");
}
// print help
void help() {
    printf("\n");
    printf("SET key value               Sets <key> to <value>\n");
    printf("GET key                     Get the value identified by <key>\n");
    printf("DEL key key2 .. keyN        Delete values identified by <key>..<keyN>\n");
    printf("INC key qty                 Increment by <qty> the value idenfied by <key>, if\n");
    printf("                            no <qty> is specified increment by 1\n");
    printf("DEC key qty                 Decrement by <qty> the value idenfied by <key>, if\n");
    printf("                            no <qty> is specified decrement by 1\n");
    printf("SUB key key2 .. keyN        Subscribe to <key>..<keyN>, receiving messages published\n");
    printf("UNSUB key key2 .. keyN      Unsubscribe from <key>..<keyN>\n");
    printf("PUB key value               Publish message <value> to <key> (analog to set\n");
    printf("                            but broadcasting to all subscribed members of key)\n");
    printf("GETP key                    Get all information of a key-value pair represented by\n");
    printf("                            <key>, like key, value, creation time and expire time\n");
    printf("APPEND key value            Append <value> to <key>\n");
    printf("PREPEND key value           Prepend <value> to <key>\n");
    printf("EXPIRE key ms               Set an expire time in milliseconds after that the <key>\n");
    printf("                            will be deleted, upon taking -1 as <ms> value the\n");
    printf("                            expire time will be removed\n");
    printf("KEYS                        List all keys stored into the keyspace\n");
    printf("VALUES                      List all values stored into the keyspace\n");
    printf("COUNT                       Return the number of key-value pair stored\n");
    printf("TAIL key offset             Like SUB, but with an <offset> representing how\n");
    printf("                            many messages discard starting from 0 (the very\n");
    printf("                            first chronologically)\n");
    printf("PREFSCAN key_prefix         Scan the keyspace finding all values associated to\n");
    printf("                            keys matching <key_prefix> as prefix\n");
    printf("FUZZYSCAN pattern           Scan the keyspace finding all values associated to\n");
    printf("                            keys matching <pattern> in a fuzzy search way\n");
    printf("FLUSH                       Delete all maps stored inside partitions\n");
    printf("QUIT/EXIT                   Close connection\n");
    printf("\n");
}
