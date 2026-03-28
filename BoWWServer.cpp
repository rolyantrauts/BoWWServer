#include "BoWWServer.h"
#include <iostream>
#include <sstream>
#include <fstream>

namespace boww {

    BoWWServer::BoWWServer(std::string config_dir, std::string model_path, bool debug) 
        : config_dir_(config_dir), vad_engine_(debug), debug_mode_(debug) {
        
        endpoint_.clear_access_channels(websocketpp::log::alevel::all);
        endpoint_.clear_error_channels(websocketpp::log::elevel::all);
        endpoint_.init_asio();
        endpoint_.set_reuse_addr(true);

        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        endpoint_.set_open_handler(bind(&BoWWServer::OnOpen, this, _1));
        endpoint_.set_close_handler(bind(&BoWWServer::OnClose, this, _1));
        endpoint_.set_message_handler(bind(&BoWWServer::OnMessage, this, _1, _2));

        config_manager_.OnGroupConfigChanged = [this](const GroupConfig& config) {
            std::lock_guard<std::mutex> lock(group_mutex_);
            if (groups_.find(config.name) == groups_.end()) {
                groups_[config.name] = std::make_shared<GroupController>(
                    config, vad_engine_, config_manager_.GetServerConfig(), alsa_manager_, debug_mode_
                );
            }
            std::cout << "[Server] Group Config Updated: " << config.name << "\n";
        };

        config_manager_.OnClientOnboarded = [this](const std::string& temp_id, const std::string& guid, const std::string& group) {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            for (auto& pair : sessions_) {
                if (pair.second->GetID() == temp_id) {
                    SendJSON(pair.first, {
                        {"type", Protocol::MSG_ASSIGN_ID},
                        {"id", guid}
                    });
                    std::cout << "[Server] Sent Onboarding ID (" << guid << ") to TempID: " << temp_id << "\n";
                    break;
                }
            }
        };

        if (!vad_engine_.Initialize(model_path)) {
            std::cerr << "[Server] WARNING: VAD Model load failed. Check path: " << model_path << std::endl;
        }
        
        if (!mdns_service_.Start("BoWW-Server", 9002)) {
            std::cerr << "[Server] Failed to start mDNS." << std::endl;
        }
    }

    BoWWServer::~BoWWServer() {
        running_ = false;
        mdns_service_.Stop();
        if (ticker_thread_.joinable()) ticker_thread_.join();
        endpoint_.stop();
    }

    void BoWWServer::Run(uint16_t port) {
        std::string yaml_path = config_dir_ + "clients.yaml";
        config_manager_.LoadConfig(yaml_path);
        config_manager_.StartWatching();

        running_ = true;
        ticker_thread_ = std::thread([this]() {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                std::lock_guard<std::mutex> lock(group_mutex_);
                for (auto& pair : groups_) {
                    pair.second->OnTick();
                }
            }
        });

        endpoint_.listen(port);
        endpoint_.start_accept();
        std::cout << "[Server] BoWW Server v1.0 running on port " << port << "\n";
        endpoint_.run();
    }

    void BoWWServer::OnOpen(ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        auto session = std::make_shared<ClientSession>(hdl, this);
        sessions_[hdl] = session;
        std::cout << "[Server] New WebSocket connection established. Waiting for identity...\n";
    }

    void BoWWServer::OnClose(ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        if (sessions_.count(hdl)) {
            std::string id = sessions_[hdl]->GetID();
            std::cout << "[Server] Disconnect: " << (id.empty() ? "Unknown Client" : id) << "\n";
            sessions_.erase(hdl);
        }
    }

    void BoWWServer::SendJSON(ConnectionHdl hdl, const nlohmann::json& j) {
        try {
            endpoint_.send(hdl, j.dump(), websocketpp::frame::opcode::text);
        } catch (...) {}
    }

    void BoWWServer::OnMessage(ConnectionHdl hdl, ServerType::message_ptr msg) {
        std::shared_ptr<ClientSession> session;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            if (sessions_.count(hdl)) session = sessions_[hdl];
        }
        if (!session) return;

        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
            try {
                auto j = nlohmann::json::parse(msg->get_payload());
                if (!j.contains("type")) return;
                std::string type = j["type"];

                if (type == Protocol::MSG_HELLO && j.contains("guid")) {
                    std::string guid = j["guid"];
                    ClientInfo info;
                    if (config_manager_.IsGUIDValid(guid, info)) {
                        session->SetGUID(guid, info.group_name);
                        std::cout << "[Session] Authenticated GUID: " << guid << " in Group: " << info.group_name << "\n";
                        
                        // --- NEW: Dynamic Pre-roll Handshake! ---
                        float preroll = 2.0f;
                        {
                            std::lock_guard<std::mutex> lock(group_mutex_);
                            if (groups_.count(info.group_name)) {
                                preroll = groups_[info.group_name]->GetConfig().preroll_seconds;
                            }
                        }
                        SendJSON(hdl, {
                            {"type", Protocol::MSG_HELLO_ACK},
                            {"preroll_seconds", preroll}
                        });
                        
                    } else {
                        std::cout << "[Session] Client sent invalid GUID: " << guid << "\n";
                    }
                } 
                else if (type == Protocol::MSG_ENROLL) {
                    
                    std::stringstream ss;
                    ss << std::uppercase << std::hex << reinterpret_cast<uintptr_t>(hdl.lock().get());
                    std::string temp_id = ss.str();

                    session->AssignTempID(temp_id);

                    SendJSON(hdl, {
                        {"type", Protocol::MSG_ASSIGN_TEMP_ID},
                        {"id", temp_id}
                    });
                    
                    std::cout << "[Server] Client requested enrollment. Assigned TempID: " << temp_id << "\n";
                    
                    std::string connecting_file = config_dir_ + "connecting_clients.txt";
                    std::ofstream outfile(connecting_file, std::ios_base::app);
                    if (outfile.is_open()) {
                        outfile << temp_id << "\n";
                    }
                }
                else if (type == Protocol::MSG_CONFIDENCE || type == Protocol::MSG_CONF_REC) {
                    if (!session->IsAuthenticated()) return;
                    float score = 0.0f;
                    if (j.contains("score")) score = j["score"]; 
                    
                    std::lock_guard<std::mutex> lock(group_mutex_);
                    auto group_it = groups_.find(session->GetGroup());
                    if (group_it != groups_.end()) {
                        group_it->second->HandleConfidenceScore(session, score);
                    }
                }
            } catch (...) {}
        } 
        else if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
            if (!session->IsAuthenticated()) return;
            
            const std::string& payload = msg->get_payload();
            std::vector<int16_t> pcm_data(payload.size() / sizeof(int16_t));
            std::memcpy(pcm_data.data(), payload.data(), payload.size());

            std::lock_guard<std::mutex> lock(group_mutex_);
            auto group_it = groups_.find(session->GetGroup());
            if (group_it != groups_.end()) {
                group_it->second->HandleAudioStream(session, pcm_data);
            }
        }
    }
}
