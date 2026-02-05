#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <map>
#include <mutex>
#include <thread>
#include <set>

#include "BoWWServerDefs.h"
#include "ConfigManager.h"
#include "VADEngine.h"
#include "GroupController.h"
#include "ClientSession.h"
#include "MDNSService.h"

namespace boww {

    using ServerType = websocketpp::server<websocketpp::config::asio>;
    using ConnectionHdl = websocketpp::connection_hdl;

    class BoWWServer {
    public:
        BoWWServer(bool debug_mode = false);
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
        
        bool debug_mode_; 
        
        std::map<std::string, std::shared_ptr<GroupController>> groups_;
        
        struct HdlComparator {
            bool operator()(const ConnectionHdl& a, const ConnectionHdl& b) const {
                return std::owner_less<ConnectionHdl>()(a, b);
            }
        };
        std::map<ConnectionHdl, std::shared_ptr<ClientSession>, HdlComparator> sessions_;
        std::mutex sessions_mutex_;

        std::map<std::string, std::shared_ptr<ClientSession>> temp_id_map_;
        std::mutex temp_id_mutex_;

        std::thread ticker_thread_;
        bool running_ = false;

        void TickerLoop();
        void HandleTextPacket(std::shared_ptr<ClientSession> session, const std::string& payload);
        std::string GenerateTempID();
        
        void OnConfigClientOnboarded(std::string temp_id, std::string new_guid, std::string group);
        void OnConfigGroupChanged(GroupConfig config);
    };
}
