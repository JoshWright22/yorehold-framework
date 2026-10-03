#include "yorehold/framework/net/Transport.h"

#include <map>
#include <stdexcept>

namespace yh
{

struct LoopbackHub::State
{
    struct Endpoint
    {
        std::deque<NetEvent> events;
        bool connected = true;
    };
    std::shared_ptr<Endpoint> host;
    std::map<PeerId, std::shared_ptr<Endpoint>> clients;
    PeerId nextClient = 1;
};

namespace
{

using Endpoint = LoopbackHub::State::Endpoint;

constexpr PeerId hostPeer = 1; // how clients address the host

// Both sides of a dropped connection get a Disconnected event, including the side that dropped it,
// so sessions handle every departure in one place.
class LoopbackTransport : public Transport
{
public:
    LoopbackTransport(std::shared_ptr<LoopbackHub::State> state, std::shared_ptr<Endpoint> own, PeerId self)
        : state_(std::move(state)), own_(std::move(own)), self_(self) {}

    ~LoopbackTransport() override
    {
        if (self_ == 0)
        {
            for (auto& [id, client] : state_->clients) close(*client, hostPeer);
            state_->clients.clear();
            state_->host.reset();
        }
        else if (own_->connected)
        {
            state_->clients.erase(self_);
            if (state_->host) state_->host->events.push_back({NetEvent::Type::Disconnected, self_, {}});
        }
    }

    void send(PeerId peer, std::string_view message) override
    {
        if (message.size() > maxMessageBytes) throw std::length_error("Network message too large");
        if (self_ == 0)
        {
            const auto client = state_->clients.find(peer);
            if (client != state_->clients.end()) client->second->events.push_back({NetEvent::Type::Message, hostPeer, std::string(message)});
        }
        else if (peer == hostPeer && own_->connected && state_->host)
            state_->host->events.push_back({NetEvent::Type::Message, self_, std::string(message)});
    }

    std::optional<NetEvent> poll() override
    {
        if (own_->events.empty()) return std::nullopt;
        NetEvent event = std::move(own_->events.front());
        own_->events.pop_front();
        return event;
    }

    void disconnect(PeerId peer) override
    {
        if (self_ == 0)
        {
            const auto client = state_->clients.find(peer);
            if (client == state_->clients.end()) return;
            close(*client->second, hostPeer);
            state_->clients.erase(client);
            own_->events.push_back({NetEvent::Type::Disconnected, peer, {}});
        }
        else if (peer == hostPeer && own_->connected)
        {
            state_->clients.erase(self_);
            close(*own_, hostPeer);
            if (state_->host) state_->host->events.push_back({NetEvent::Type::Disconnected, self_, {}});
        }
    }

private:
    static void close(Endpoint& endpoint, PeerId peer)
    {
        endpoint.connected = false;
        endpoint.events.push_back({NetEvent::Type::Disconnected, peer, {}});
    }

    std::shared_ptr<LoopbackHub::State> state_;
    std::shared_ptr<Endpoint> own_;
    PeerId self_;
};

}

LoopbackHub::LoopbackHub() : state_(std::make_shared<State>()) {}
LoopbackHub::~LoopbackHub() = default;

std::unique_ptr<Transport> LoopbackHub::host()
{
    if (state_->host) throw std::logic_error("Loopback hub already has a host");
    state_->host = std::make_shared<Endpoint>();
    return std::make_unique<LoopbackTransport>(state_, state_->host, 0);
}

std::unique_ptr<Transport> LoopbackHub::connect()
{
    const PeerId id = state_->nextClient++;
    auto endpoint = std::make_shared<Endpoint>();
    if (!state_->host)
    {
        endpoint->connected = false;
        endpoint->events.push_back({NetEvent::Type::Disconnected, hostPeer, {}});
        return std::make_unique<LoopbackTransport>(state_, endpoint, id);
    }
    endpoint->events.push_back({NetEvent::Type::Connected, hostPeer, {}});
    state_->host->events.push_back({NetEvent::Type::Connected, id, {}});
    state_->clients[id] = endpoint;
    return std::make_unique<LoopbackTransport>(state_, endpoint, id);
}

}
