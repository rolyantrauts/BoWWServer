#include "BoWWServer.h"
#include <iostream>
#include <cstring>

void print_usage(const char* prog_name) {
    std::cout << "\n======================================================\n"
              << " BoWWServer - Edge Smart Speaker Master Node\n"
              << "======================================================\n"
              << "Description:\n"
              << "  The BoWWServer coordinates multiple edge clients on the local\n"
              << "  network. It handles 200ms network arbitration to seamlessly\n"
              << "  determine the closest smart speaker, buffers incoming audio,\n"
              << "  runs the Silero VAD engine to detect when the user stops\n"
              << "  speaking, and outputs clean WAV files ready for STT pipelines.\n\n"
              << "Usage: " << prog_name << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  -d, --debug    Enable Debug Mode (Live VAD probabilities and peak volume)\n"
              << "  -h, --help     Show this help message and exit\n\n";
}

int main(int argc, char* argv[]) {
    bool debug = false;

    // Simple argument parsing loop
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0; // Exit cleanly after showing help
        } 
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } 
        else {
            std::cerr << "[!] Unknown argument: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1; // Exit with error code
        }
    }

    // Start the server
    boww::BoWWServer server(debug);
    server.Run(9002);
    return 0;
}
