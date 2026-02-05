#pragma once
#include <string>
#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/simple-watch.h>
#include <avahi-common/error.h>

namespace boww {

    class MDNSService {
    public:
        MDNSService();
        ~MDNSService();

        bool Start(const std::string& hostname, uint16_t port);
        void Stop();

    private:
        AvahiSimplePoll* simple_poll_ = nullptr;
        AvahiClient* client_ = nullptr;
        AvahiEntryGroup* group_ = nullptr;
        
        std::string hostname_;
        uint16_t port_;

        static void EntryGroupCallback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata);
        static void ClientCallback(AvahiClient* c, AvahiClientState state, void* userdata);
        void CreateService(AvahiClient* c);
    };
}
