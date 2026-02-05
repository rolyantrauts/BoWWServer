#pragma once

#include <string>
#include <memory>
#include <websocketpp/common/connection_hdl.hpp>
#include <nlohmann/json.hpp>

#include "BoWWServerDefs.h"
#include "VADEngine.h"

namespace boww {

    class BoWWServer; 

    class ClientSession : public std::enable_shared_from_this<ClientSession> {
    public:
        ClientSession(websocketpp::connection_hdl connection_handle, BoWWServer* server);

        // Identity
        void AssignTempID(const std::string& temp_id);
        void SetGUID(const std::string& guid, const std::string& group);
        
        std::string GetID() const; 
        bool IsAuthenticated() const;
        std::string GetGroup() const;

        // VAD
        void InitVADState(std::shared_ptr<VADSessionState> state);
        std::shared_ptr<VADSessionState> GetVADState();
        void UpdateLastVoiceTime();
        long GetTimeSinceLastVoiceMs(); 

        // Comms
        void SendJSON(const nlohmann::json& j);
        void SendStopSignal();
        websocketpp::connection_hdl GetHandle() const { return connection_handle_; }

    private:
        websocketpp::connection_hdl connection_handle_; 
        std::string temp_id_;
        std::string guid_;
        std::string group_name_;

        std::shared_ptr<VADSessionState> vad_state_{nullptr};
        std::chrono::steady_clock::time_point last_voice_ts_;

        BoWWServer* server_context_;
    };
}
