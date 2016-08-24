#include "server.h"

int main(int argc, char **argv) {
    if (argc == 0) {
        start_server("127.0.0.1");
    }
    else start_server(argv[1]);
    return 0;
}
