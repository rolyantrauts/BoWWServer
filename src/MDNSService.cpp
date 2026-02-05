#include "MDNSService.h"
#include <iostream>
#include <thread>

namespace boww {

    MDNSService::MDNSService() {}
    MDNSService::~MDNSService() { Stop(); }

    void MDNSService::EntryGroupCallback(AvahiEntryGroup* g, AvahiEntryGroupState state, void* userdata) {
        if (state == AVAHI_ENTRY_GROUP_ESTABLISHED) {
            std::cout << "[mDNS] Service established." << std::endl;
        }
    }

    void MDNSService::ClientCallback(AvahiClient* c, AvahiClientState state, void* userdata) {
        MDNSService* self = static_cast<MDNSService*>(userdata);
        if (state == AVAHI_CLIENT_S_RUNNING) {
            self->CreateService(c);
        }
    }

    void MDNSService::CreateService(AvahiClient* c) {
        if (!group_) {
            group_ = avahi_entry_group_new(c, EntryGroupCallback, this);
        }
        
        if (avahi_entry_group_is_empty(group_)) {
            std::cout << "[mDNS] Advertising: " << hostname_ << "._boww._tcp on port " << port_ << std::endl;
            avahi_entry_group_add_service(group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC, 
                (AvahiPublishFlags)0, hostname_.c_str(), "_boww._tcp", NULL, NULL, port_, NULL);
            avahi_entry_group_commit(group_);
        }
    }

    bool MDNSService::Start(const std::string& hostname, uint16_t port) {
        hostname_ = hostname;
        port_ = port;

        simple_poll_ = avahi_simple_poll_new();
        if (!simple_poll_) return false;

        int error;
        client_ = avahi_client_new(avahi_simple_poll_get(simple_poll_), (AvahiClientFlags)0, ClientCallback, this, &error);
        if (!client_) return false;

        // Run poll loop in background thread
        std::thread([this]() {
            avahi_simple_poll_loop(simple_poll_);
        }).detach();

        return true;
    }

    void MDNSService::Stop() {
        if (simple_poll_) avahi_simple_poll_quit(simple_poll_);
        // Clean up memory would go here, but omitted for simple shutdown
    }
}
