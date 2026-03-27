#pragma once
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <string>
#include <memory>
#include "BoWWServerDefs.h"
#include "VADEngine.h"

namespace boww {

    class BoWWServer; 

    class ClientSession {
    public:
        ClientSession(websocketpp::connection_hdl hdl, BoWWServer* server);

        void AssignTempID(const std::string& temp_id);
        void SetGUID(const std::string& guid, const std::string& group);
        
        std::string GetID() const;
        std::string GetGroup() const;
        bool IsAuthenticated() const;
        websocketpp::connection_hdl GetHandle() const;

        void InitVADState(std::shared_ptr<VADSessionState> state);
        std::shared_ptr<VADSessionState> GetVADState() const;
        
        void UpdateLastVoiceTime();
        long GetTimeSinceLastVoiceMs() const;

        // --- ADD THIS LINE ---
        void SendStartSignal(); 
        void SendStopSignal();

    private:
        websocketpp::connection_hdl hdl_;
        BoWWServer* server_;
        std::string temp_id_;
        std::string guid_;
        std::string group_;
        bool authenticated_ = false;

        std::shared_ptr<VADSessionState> vad_state_;
        std::chrono::steady_clock::time_point last_voice_ts_;
    };
}
