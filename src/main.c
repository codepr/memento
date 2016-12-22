#include <stdio.h>
#include "server.h"
#include "cluster.h"

int main(int argc, char **argv) {
    char *seed = (char *) malloc(12);
    if (argc == 1) {
        start_server("127.0.0.1", PORT, 0);
    } else if (argc == 2) {
        start_server(argv[1], PORT, 0);
    } else if (argc == 3) {
        start_server(argv[1], argv[2], 0);
    } else {
        start_server(argv[1], argv[2], 0);
    }
    return 0;
}
