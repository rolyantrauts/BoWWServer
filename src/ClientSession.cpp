#include "ClientSession.h"
#include "BoWWServer.h"
#include <iostream>
#include <chrono>

namespace boww {

    ClientSession::ClientSession(websocketpp::connection_hdl connection_handle, BoWWServer* server)
        : connection_handle_(connection_handle), server_context_(server) 
    {
        last_voice_ts_ = std::chrono::steady_clock::now();
    }

    void ClientSession::AssignTempID(const std::string& temp_id) {
        temp_id_ = temp_id;
        guid_ = "";
        group_name_ = "";
    }

    void ClientSession::SetGUID(const std::string& guid, const std::string& group) {
        guid_ = guid;
        group_name_ = group;
        temp_id_ = ""; 
        std::cout << "[Session] Authenticated GUID: " << guid << " in Group: " << group << std::endl;
    }

    std::string ClientSession::GetID() const {
        return guid_.empty() ? temp_id_ : guid_;
    }

    bool ClientSession::IsAuthenticated() const {
        return !guid_.empty();
    }

    std::string ClientSession::GetGroup() const {
        return group_name_;
    }

    void ClientSession::InitVADState(std::shared_ptr<VADSessionState> state) {
        vad_state_ = state;
        UpdateLastVoiceTime();
    }

    std::shared_ptr<VADSessionState> ClientSession::GetVADState() {
        return vad_state_;
    }

    void ClientSession::UpdateLastVoiceTime() {
        last_voice_ts_ = std::chrono::steady_clock::now();
    }

    long ClientSession::GetTimeSinceLastVoiceMs() {
        auto now = std::chrono::steady_clock::now();
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_voice_ts_);
        return static_cast<long>(diff.count());
    }

    void ClientSession::SendJSON(const nlohmann::json& j) {
        if (server_context_) {
            server_context_->SendJSON(connection_handle_, j);
        }
    }

    void ClientSession::SendStopSignal() {
        nlohmann::json j;
        j["type"] = Protocol::MSG_STOP;
        SendJSON(j);
    }
}
