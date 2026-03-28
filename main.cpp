#include "BoWWServer.h"
#include <iostream>
#include <cstring>
#include <string>

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
              << "  -c, --config   Path to config dir (default: ../)\n"
              << "  -m, --model    Path to Silero VAD model (default: ../models/silero_vad.onnx)\n"
              << "  -p, --port     WebSocket listener port (default: 9002)\n"
              << "  -d, --debug    Enable Debug Mode (Live VAD probabilities and peak volume)\n"
              << "  -h, --help     Show this help message and exit\n\n";
}

int main(int argc, char* argv[]) {
    bool debug = false;
    std::string config_dir = "../";
    std::string model_path = "../models/silero_vad.onnx";
    uint16_t port = 9002;

    // Simple argument parsing loop
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0; 
        } 
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } 
        else if ((strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) && i + 1 < argc) {
            config_dir = argv[++i];
            // Enforce trailing slash for clean path building
            if (!config_dir.empty() && config_dir.back() != '/' && config_dir.back() != '\\') {
                config_dir += "/";
            }
        }
        else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) && i + 1 < argc) {
            model_path = argv[++i];
        }
        else if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else {
            std::cerr << "[!] Unknown argument: " << argv[i] << "\n";
            print_usage(argv[0]);
            return 1; 
        }
    }

    // Start the server with dynamic paths
    boww::BoWWServer server(config_dir, model_path, debug);
    server.Run(port);
    return 0;
}
