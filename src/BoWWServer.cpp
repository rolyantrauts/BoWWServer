#include "BoWWServer.h"
#include <iostream>
#include <random>
#include <sstream>
#include <fstream>
#include <cstring> 

namespace boww {

    BoWWServer::BoWWServer(bool debug_mode) 
        : vad_engine_(debug_mode), debug_mode_(debug_mode) 
    {
        // Logging
        endpoint_.clear_access_channels(websocketpp::log::alevel::all); 
        endpoint_.set_error_channels(websocketpp::log::elevel::all);    

        if (debug_mode) {
            endpoint_.set_access_channels(websocketpp::log::alevel::all);
            std::cout << "[Server] DEBUG MODE ENABLED" << std::endl;
        } else {
            endpoint_.set_access_channels(websocketpp::log::alevel::connect);
            endpoint_.set_access_channels(websocketpp::log::alevel::disconnect);
            endpoint_.set_access_channels(websocketpp::log::alevel::app); 
        }

        endpoint_.init_asio();

        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        endpoint_.set_open_handler(bind(&BoWWServer::OnOpen, this, _1));
        endpoint_.set_close_handler(bind(&BoWWServer::OnClose, this, _1));
        endpoint_.set_message_handler(bind(&BoWWServer::OnMessage, this, _1, _2));

        if (!mdns_service_.Start("BoWW-Server", 9002)) {
            std::cerr << "[Server] Failed to start mDNS." << std::endl;
        }

        if (!vad_engine_.Initialize("../models/silero_vad.onnx")) {
            std::cerr << "[Server] WARNING: VAD Model load failed." << std::endl;
        }
    }

    BoWWServer::~BoWWServer() {
        running_ = false;
        mdns_service_.Stop();
        if (ticker_thread_.joinable()) ticker_thread_.join();
        endpoint_.stop();
    }

    void BoWWServer::Run(uint16_t port) {
        config_manager_.OnClientOnboarded = [this](auto t, auto g, auto gr) { this->OnConfigClientOnboarded(t, g, gr); };
        config_manager_.OnGroupConfigChanged = [this](auto c) { this->OnConfigGroupChanged(c); };
        
        if (!config_manager_.LoadConfig("../clients.yaml")) {
            std::cerr << "[Server] Failed to load clients.yaml." << std::endl;
            return;
        }
        config_manager_.StartWatching();

        running_ = true;
        ticker_thread_ = std::thread(&BoWWServer::TickerLoop, this);

        endpoint_.listen(port);
        endpoint_.start_accept();
        
        std::cout << "[Server] BoWW Server v1.0 running on port " << port << std::endl;
        endpoint_.run();
    }

    void BoWWServer::OnOpen(ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto session = std::make_shared<ClientSession>(hdl, this);
        sessions_[hdl] = session;

        std::string temp_id = GenerateTempID();
        session->AssignTempID(temp_id);

        {
            std::lock_guard<std::mutex> tlock(temp_id_mutex_);
            temp_id_map_[temp_id] = session;
        }

        std::cout << "[Server] New Connection. Assigned TempID: " << temp_id << std::endl;
        std::ofstream outfile("../connecting_clients.txt", std::ios_base::app);
        outfile << temp_id << "\n";
    }

    void BoWWServer::OnClose(ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (sessions_.count(hdl)) {
            auto session = sessions_[hdl];
            std::cout << "[Server] Disconnect: " << session->GetID() << std::endl;
            sessions_.erase(hdl);
        }
    }

    void BoWWServer::OnMessage(ConnectionHdl hdl, ServerType::message_ptr msg) {
        std::shared_ptr<ClientSession> session;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            if (sessions_.find(hdl) == sessions_.end()) return;
            session = sessions_[hdl];
        }

        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            HandleTextPacket(session, msg->get_payload());
        }
        else if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
            if (!session->IsAuthenticated()) return; 
            std::string group_name = session->GetGroup();
            if (groups_.count(group_name)) {
                const std::string& payload = msg->get_payload();
                std::vector<int16_t> pcm_data(payload.size() / 2);
                std::memcpy(pcm_data.data(), payload.data(), payload.size());
                groups_[group_name]->HandleAudioStream(session, pcm_data);
            }
        }
    }

    void BoWWServer::HandleTextPacket(std::shared_ptr<ClientSession> session, const std::string& payload) {
        try {
            auto j = nlohmann::json::parse(payload);
            std::string type = j["type"];

            if (type == Protocol::MSG_HELLO) {
                std::string guid = j["guid"];
                ClientInfo info;
                if (config_manager_.IsGUIDValid(guid, info)) {
                    session->SetGUID(guid, info.group_name);
                } else {
                    std::cout << "[Server] Client sent invalid GUID: " << guid << std::endl;
                }
            }
            else if (type == Protocol::MSG_CONFIDENCE) {
                if (!session->IsAuthenticated()) return;
                float score = j["value"];
                std::string group = session->GetGroup();
                if (groups_.count(group)) {
                    SendJSON(session->GetHandle(), {{"type", Protocol::MSG_CONF_REC}});
                    groups_[group]->HandleConfidenceScore(session, score);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Server] JSON Parse Error: " << e.what() << std::endl;
        }
    }

    void BoWWServer::TickerLoop() {
        while (running_) {
            for (auto& [name, controller] : groups_) {
                if (controller) controller->OnTick();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void BoWWServer::OnConfigClientOnboarded(std::string temp_id, std::string new_guid, std::string group) {
        std::lock_guard<std::mutex> lock(temp_id_mutex_);
        if (temp_id_map_.count(temp_id)) {
            auto session = temp_id_map_[temp_id];
            std::cout << "[Server] Onboarding Client! " << temp_id << " -> " << new_guid << std::endl;
            SendJSON(session->GetHandle(), {{"type", Protocol::MSG_ASSIGN_ID}, {"id", new_guid}});
        }
    }
    
    void BoWWServer::OnConfigGroupChanged(GroupConfig config) {
        std::cout << "[Server] Group Config Updated: " << config.name << std::endl;
        if (groups_.find(config.name) == groups_.end()) {
            groups_[config.name] = std::make_shared<GroupController>(config, vad_engine_, debug_mode_);
        } 
    }

    void BoWWServer::SendJSON(ConnectionHdl hdl, const nlohmann::json& j) {
        try {
            endpoint_.send(hdl, j.dump(), websocketpp::frame::opcode::text);
        } catch (...) {}
    }

    std::string BoWWServer::GenerateTempID() {
        static const char hex[] = "0123456789ABCDEF";
        std::string id = "temp-";
        for (int i = 0; i < 8; ++i) id += hex[rand() % 16];
        return id;
    }
}
