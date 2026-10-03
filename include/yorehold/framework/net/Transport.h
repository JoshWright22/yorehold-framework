#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yh
{

using PeerId = uint32_t;

struct NetEvent
{
    enum class Type : uint8_t { Connected, Disconnected, Message };
    Type type = Type::Message;
    PeerId peer = 0;
    std::string data;
};

// Reliable, ordered messages between peers (the session layer adds meaning on top).
// Everything runs on the calling thread: poll() once per frame, then handle each event.
class Transport
{
public:
    // Large enough for a full region snapshot; bigger messages are refused.
    static constexpr size_t maxMessageBytes = 16 * 1024 * 1024;

    virtual ~Transport() = default;
    virtual void send(PeerId peer, std::string_view message) = 0;
    // Returns the next event, or nothing when the queue is empty.
    virtual std::optional<NetEvent> poll() = 0;
    virtual void disconnect(PeerId peer) = 0;
};

// In-process transports for tests, local play and running host + client in one process.
// The hub must outlive its transports' use; connections made after a transport closes fail.
class LoopbackHub
{
public:
    LoopbackHub();
    ~LoopbackHub();
    // The listening side. Only one host per hub.
    std::unique_ptr<Transport> host();
    // A new client connected to the host; its only peer has id 1.
    std::unique_ptr<Transport> connect();

    struct State;

private:
    std::shared_ptr<State> state_;
};

// TCP with length-prefixed messages and non-blocking sockets. Not available in browser builds
// (browsers only allow WebSocket/WebRTC). Clients see the server as peer 1.
class TcpTransport : public Transport
{
public:
    // `localOnly` accepts connections from this machine only (tests, same-PC play) and avoids firewall prompts.
    static std::unique_ptr<TcpTransport> listen(uint16_t port, std::string* error = nullptr, bool localOnly = false);
    // Starts connecting; a Connected or Disconnected event follows from poll(). Name lookup blocks.
    static std::unique_ptr<TcpTransport> connect(std::string_view host, uint16_t port, std::string* error = nullptr);
    ~TcpTransport() override;

    void send(PeerId peer, std::string_view message) override;
    std::optional<NetEvent> poll() override;
    void disconnect(PeerId peer) override;
    // The port actually bound (useful after listen(0)).
    uint16_t port() const { return port_; }

private:
    struct Peer;
    TcpTransport();
    void pump();
    void flush(Peer& peer);
    void drop(Peer& peer);

    intptr_t listener_ = -1;
    uint16_t port_ = 0;
    PeerId nextPeer_ = 1;
    std::vector<std::unique_ptr<Peer>> peers_;
    std::deque<NetEvent> events_;
};

}
