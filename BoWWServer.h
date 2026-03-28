#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <map>
#include <mutex>
#include <thread>
#include <set>
#include <string>

#include "BoWWServerDefs.h"
#include "ConfigManager.h"
#include "VADEngine.h"
#include "GroupController.h"
#include "ClientSession.h"
#include "MDNSService.h"
#include "AlsaSinkManager.h" // <-- NEW

namespace boww {

    using ServerType = websocketpp::server<websocketpp::config::asio>;
    using ConnectionHdl = websocketpp::connection_hdl;

    class BoWWServer {
    public:
        BoWWServer(std::string config_dir, std::string model_path, bool debug_mode = false);
        ~BoWWServer();

        void Run(uint16_t port);

        void OnOpen(ConnectionHdl hdl);
        void OnClose(ConnectionHdl hdl);
        void OnMessage(ConnectionHdl hdl, ServerType::message_ptr msg);
        void SendJSON(ConnectionHdl hdl, const nlohmann::json& j);

    private:
        ServerType endpoint_;
        ConfigManager config_manager_;
        VADEngine vad_engine_;
        MDNSService mdns_service_;
        AlsaSinkManager alsa_manager_; // <-- NEW
        
        std::string config_dir_; 
        bool debug_mode_; 
        
        std::map<std::string, std::shared_ptr<GroupController>> groups_;
        std::mutex group_mutex_; 
        
        struct HdlComparator {
            bool operator()(const ConnectionHdl& a, const ConnectionHdl& b) const {
                return std::owner_less<ConnectionHdl>()(a, b);
            }
        };
        std::map<ConnectionHdl, std::shared_ptr<ClientSession>, HdlComparator> sessions_;
        std::mutex sessions_mutex_;

        std::thread ticker_thread_;
        bool running_ = false;

        void TickerLoop();
    };
}
