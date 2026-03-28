#include "ClientSession.h"
#include "BoWWServer.h" 
#include <chrono>

namespace boww {

    ClientSession::ClientSession(websocketpp::connection_hdl hdl, BoWWServer* server)
        : hdl_(hdl), server_(server) {
        last_voice_ts_ = std::chrono::steady_clock::now();
    }

    void ClientSession::AssignTempID(const std::string& temp_id) {
        temp_id_ = temp_id;
    }

    void ClientSession::SetGUID(const std::string& guid, const std::string& group) {
        guid_ = guid;
        group_ = group;
        authenticated_ = true;
    }

    std::string ClientSession::GetID() const {
        return authenticated_ ? guid_ : temp_id_;
    }

    std::string ClientSession::GetGroup() const {
        return group_;
    }

    bool ClientSession::IsAuthenticated() const {
        return authenticated_;
    }

    websocketpp::connection_hdl ClientSession::GetHandle() const {
        return hdl_;
    }

    void ClientSession::InitVADState(std::shared_ptr<VADSessionState> state) {
        vad_state_ = state;
        UpdateLastVoiceTime();
    }

    std::shared_ptr<VADSessionState> ClientSession::GetVADState() const {
        return vad_state_;
    }

    void ClientSession::UpdateLastVoiceTime() {
        last_voice_ts_ = std::chrono::steady_clock::now();
    }

    long ClientSession::GetTimeSinceLastVoiceMs() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_voice_ts_).count();
    }

    // --- ADD THIS FUNCTION ---
    void ClientSession::SendStartSignal() {
        if (server_) {
            server_->SendJSON(hdl_, {{"type", Protocol::MSG_START}});
        }
    }

    void ClientSession::SendStopSignal() {
        if (server_) {
            server_->SendJSON(hdl_, {{"type", Protocol::MSG_STOP}});
        }
    }
}
