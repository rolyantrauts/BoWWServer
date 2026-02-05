#include "BoWWServer.h"
#include <iostream>
#include <cstring>

int main(int argc, char* argv[]) {
    bool debug = false;
    if (argc > 1 && strcmp(argv[1], "--debug") == 0) {
        debug = true;
    }

    boww::BoWWServer server(debug);
    server.Run(9002);
    return 0;
}
