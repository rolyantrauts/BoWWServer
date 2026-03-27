#include "ConfigManager.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <yaml-cpp/yaml.h>
#include <thread>
#include <chrono>
#include <filesystem>

namespace boww {

    // --- NEW: Safe Text Replacer to preserve YAML comments ---
    void SafeRemoveOnboardTempId(const std::string& filepath) {
        std::ifstream in(filepath);
        if (!in.is_open()) return;

        std::vector<std::string> lines;
        std::string line;
        bool modified = false;

        while (std::getline(in, line)) {
            // If the line contains "onboard_temp_id", skip it (effectively deleting it)
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
            auto last_write = std::filesystem::last_write_time(config_path_);
            while (true) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                try {
                    auto current_write = std::filesystem::last_write_time(config_path_);
                    if (current_write > last_write) {
                        std::cout << "[Config] Change detected. Reloading..." << std::endl;
                        if (ParseYaml()) {
                            last_write = current_write;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "[Config] Watcher Error: " << e.what() << std::endl;
                }
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

                    // --- UPDATED: Trigger Onboard and Safe Delete ---
                    if (node["onboard_temp_id"]) {
                        std::string temp_id = node["onboard_temp_id"].as<std::string>();
                        if (!temp_id.empty() && OnClientOnboarded) {
                            std::cout << "[Config] Found Onboarding Request for TempID: " << temp_id << std::endl;
                            OnClientOnboarded(temp_id, info.guid, info.group_name);
                            
                            // Erase the line so we don't trigger this again on next reboot
                            SafeRemoveOnboardTempId(config_path_);
                        }
                    }
                }
                valid_clients_ = new_clients;
            }

            std::cout << "[Config] Loaded " << groups_.size() << " groups and " 
                      << valid_clients_.size() << " clients." << std::endl;
            return true;

        } catch (const YAML::Exception& e) {
            std::cerr << "[Config] YAML Parsing Failed: " << e.what() << std::endl;
            return false;
        }
    }
}
