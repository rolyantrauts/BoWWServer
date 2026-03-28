#include "ConfigManager.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace boww {

    void SafeRemoveOnboardTempId(const std::string& filepath) {
        std::ifstream in(filepath);
        if (!in.is_open()) return;

        std::vector<std::string> lines;
        std::string line;
        bool modified = false;

        while (std::getline(in, line)) {
            if (line.find("onboard_temp_id") != std::string::npos) {
                modified = true;
                continue; 
            }
            lines.push_back(line);
        }
        in.close();

        if (modified) {
            std::ofstream out(filepath);
            for (const auto& l : lines) {
                out << l << "\n";
            }
            std::cout << "[Config] Safely removed 'onboard_temp_id' from clients.yaml\n";
        }
    }

    bool ConfigManager::LoadConfig(const std::string& path) {
        config_path_ = path;
        return ParseYaml();
    }

    void ConfigManager::StartWatching() {
        std::thread([this]() {
            std::filesystem::file_time_type last_write;
            bool file_known_to_exist = false;

            try {
                if (std::filesystem::exists(config_path_)) {
                    last_write = std::filesystem::last_write_time(config_path_);
                    file_known_to_exist = true;
                }
            } catch (...) {}

            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                try {
                    if (std::filesystem::exists(config_path_)) {
                        auto current_write = std::filesystem::last_write_time(config_path_);
                        
                        if (!file_known_to_exist || current_write > last_write) {
                            std::cout << "[Config] Change detected. Reloading..." << std::endl;
                            if (ParseYaml()) {
                                last_write = current_write;
                                file_known_to_exist = true;
                            }
                        }
                    } else {
                        if (file_known_to_exist) {
                            std::cerr << "[Config] WARNING: " << config_path_ << " is missing!\n";
                            file_known_to_exist = false;
                        }
                    }
                } catch (const std::exception& e) {}
            }
        }).detach();
    }

    bool ConfigManager::IsGUIDValid(const std::string& guid, ClientInfo& out_info) {
        if (valid_clients_.count(guid)) {
            out_info = valid_clients_[guid];
            return true;
        }
        return false;
    }

    bool ConfigManager::ParseYaml() {
        try {
            YAML::Node config = YAML::LoadFile(config_path_);
            
            if (config["server"]) {
                if (config["server"]["temp_dir"]) server_config_.temp_dir = config["server"]["temp_dir"].as<std::string>();
                if (config["server"]["dest_dir"]) server_config_.dest_dir = config["server"]["dest_dir"].as<std::string>();
            }
            
            if (!server_config_.temp_dir.empty() && server_config_.temp_dir.back() != '/') server_config_.temp_dir += "/";
            if (!server_config_.dest_dir.empty() && server_config_.dest_dir.back() != '/') server_config_.dest_dir += "/";

            std::filesystem::create_directories(server_config_.temp_dir);
            std::filesystem::create_directories(server_config_.dest_dir);
            
            if (config["groups"]) {
                for (const auto& node : config["groups"]) {
                    GroupConfig gc;
                    gc.name = node["name"].as<std::string>();
                    
                    if (node["sample_rate"]) gc.sample_rate = node["sample_rate"].as<int>();
                    if (node["channels"]) gc.channels = node["channels"].as<int>();
                    if (node["agc"]) gc.use_agc = node["agc"].as<bool>();
                    if (node["vad_threshold"]) gc.vad_threshold = node["vad_threshold"].as<float>();
                    if (node["arbitration_timeout_ms"]) gc.arbitration_timeout_ms = node["arbitration_timeout_ms"].as<int>();
                    if (node["vad_no_voice_ms"]) gc.vad_no_voice_ms = node["vad_no_voice_ms"].as<int>();
                    if (node["preroll_seconds"]) gc.preroll_seconds = node["preroll_seconds"].as<float>(); // <-- NEW

                    if (node["output"]) {
                        std::string output = node["output"].as<std::string>();
                        if (output == "file") gc.output_type = OutputType::FILE;
                        else if (output == "alsa") {
                            gc.output_type = OutputType::ALSA;
                            if (node["device"]) gc.output_target = node["device"].as<std::string>();
                        }
                    }

                    if (OnGroupConfigChanged) OnGroupConfigChanged(gc);
                    groups_[gc.name] = gc;
                }
            }

            if (config["clients"]) {
                std::map<std::string, ClientInfo> new_clients;
                for (const auto& node : config["clients"]) {
                    ClientInfo info;
                    info.guid = node["guid"].as<std::string>();
                    info.group_name = node["group"].as<std::string>();
                    new_clients[info.guid] = info;

                    if (node["onboard_temp_id"]) {
                        std::string temp_id = node["onboard_temp_id"].as<std::string>();
                        if (!temp_id.empty() && OnClientOnboarded) {
                            std::cout << "[Config] Found Onboarding Request for TempID: " << temp_id << std::endl;
                            OnClientOnboarded(temp_id, info.guid, info.group_name);
                            SafeRemoveOnboardTempId(config_path_);
                        }
                    }
                }
                valid_clients_ = new_clients;
            }

            std::cout << "[Config] Global Temp: " << server_config_.temp_dir << " | Dest: " << server_config_.dest_dir << "\n";
            std::cout << "[Config] Loaded " << groups_.size() << " groups and " << valid_clients_.size() << " clients.\n";
            return true;

        } catch (const YAML::Exception& e) {
            std::cerr << "[Config] YAML Parsing Failed: " << e.what() << std::endl;
            return false;
        }
    }
}
