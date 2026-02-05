#pragma once
#include <string>
#include <map>
#include <functional>
#include "BoWWServerDefs.h"

namespace boww {

    class ConfigManager {
    public:
        bool LoadConfig(const std::string& path);
        void StartWatching();

        std::function<void(std::string temp_id, std::string new_guid, std::string group)> OnClientOnboarded;
        std::function<void(GroupConfig new_config)> OnGroupConfigChanged;

        bool IsGUIDValid(const std::string& guid, ClientInfo& out_info);

    private:
        std::string config_path_;
        std::map<std::string, GroupConfig> groups_;
        std::map<std::string, ClientInfo> valid_clients_;
        
        bool ParseYaml();
    };
}
