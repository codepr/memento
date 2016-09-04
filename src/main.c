#include "server.h"

int main(int argc, char **argv) {
    if (argc == 0) {
        start_server("127.0.0.1", PORT);
    } else if (argc == 1) {
        start_server(argv[1], PORT);
    } else {
        start_server(argv[1], argv[2]);
    }
    return 0;
}
