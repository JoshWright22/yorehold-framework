#include "yorehold/framework/net/Transport.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
// Browsers can't open raw sockets; every call fails cleanly.
namespace yh
{
struct TcpTransport::Peer {};
TcpTransport::TcpTransport() = default;
TcpTransport::~TcpTransport() = default;
std::unique_ptr<TcpTransport> TcpTransport::listen(uint16_t, std::string* error, bool) { if (error) *error = "TCP is not available in the browser"; return nullptr; }
std::unique_ptr<TcpTransport> TcpTransport::connect(std::string_view, uint16_t, std::string* error) { if (error) *error = "TCP is not available in the browser"; return nullptr; }
void TcpTransport::send(PeerId, std::string_view) {}
std::optional<NetEvent> TcpTransport::poll() { return std::nullopt; }
void TcpTransport::disconnect(PeerId) {}
void TcpTransport::pump() {}
void TcpTransport::flush(Peer&) {}
void TcpTransport::drop(Peer&) {}
}
#else

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle invalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle invalidSocket = -1;
#endif

namespace yh
{

namespace
{

#if defined(_WIN32)
bool socketsReady()
{
    static const bool ready = [] { WSADATA data; return WSAStartup(MAKEWORD(2, 2), &data) == 0; }();
    return ready;
}
void closeSocket(SocketHandle s) { closesocket(s); }
bool wouldBlock() { const int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
bool setNonBlocking(SocketHandle s) { u_long on = 1; return ioctlsocket(s, FIONBIO, &on) == 0; }
constexpr int sendFlags = 0;
#else
bool socketsReady() { return true; }
void closeSocket(SocketHandle s) { ::close(s); }
bool wouldBlock() { return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS; }
bool setNonBlocking(SocketHandle s) { const int flags = fcntl(s, F_GETFL, 0); return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0; }
#if defined(MSG_NOSIGNAL)
constexpr int sendFlags = MSG_NOSIGNAL;
#else
constexpr int sendFlags = 0;
#endif
#endif

void tune(SocketHandle s)
{
    // Game messages are small and latency-sensitive.
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
#if defined(SO_NOSIGPIPE)
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
}

void fail(std::string* error, const char* what) { if (error) *error = what; }

}

struct TcpTransport::Peer
{
    SocketHandle socket = invalidSocket;
    PeerId id = 0;
    bool connecting = false;
    bool dead = false;
    std::string inbox;
    std::string outbox;
    size_t sent = 0;
};

TcpTransport::TcpTransport() = default;

TcpTransport::~TcpTransport()
{
    for (auto& peer : peers_) if (!peer->dead) closeSocket(peer->socket);
    if (listener_ != -1) closeSocket(static_cast<SocketHandle>(listener_));
}

std::unique_ptr<TcpTransport> TcpTransport::listen(uint16_t port, std::string* error, bool localOnly)
{
    if (!socketsReady()) { fail(error, "Networking is unavailable"); return nullptr; }
    const SocketHandle s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == invalidSocket) { fail(error, "Could not create a socket"); return nullptr; }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(localOnly ? INADDR_LOOPBACK : INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&address), sizeof address) != 0 || ::listen(s, 16) != 0 || !setNonBlocking(s))
    {
        closeSocket(s);
        fail(error, "Could not listen on that port (is it in use?)");
        return nullptr;
    }
    socklen_t length = sizeof address;
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    std::unique_ptr<TcpTransport> transport(new TcpTransport());
    transport->listener_ = static_cast<intptr_t>(s);
    transport->port_ = ntohs(address.sin_port);
    return transport;
}

std::unique_ptr<TcpTransport> TcpTransport::connect(std::string_view host, uint16_t port, std::string* error)
{
    if (!socketsReady()) { fail(error, "Networking is unavailable"); return nullptr; }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string name(host), service = std::to_string(port);
    if (getaddrinfo(name.c_str(), service.c_str(), &hints, &found) != 0 || !found) { fail(error, "Could not find that host"); return nullptr; }
    SocketHandle s = invalidSocket;
    bool started = false;
    for (addrinfo* a = found; a && !started; a = a->ai_next)
    {
        s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == invalidSocket) continue;
        if (setNonBlocking(s) && (::connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0 || wouldBlock())) started = true;
        else { closeSocket(s); s = invalidSocket; }
    }
    freeaddrinfo(found);
    if (!started) { fail(error, "Could not connect"); return nullptr; }
    tune(s);
    std::unique_ptr<TcpTransport> transport(new TcpTransport());
    auto peer = std::make_unique<Peer>();
    peer->socket = s;
    peer->id = transport->nextPeer_++;
    peer->connecting = true;
    transport->peers_.push_back(std::move(peer));
    transport->port_ = port;
    return transport;
}

void TcpTransport::send(PeerId id, std::string_view message)
{
    if (message.size() > maxMessageBytes) throw std::length_error("Network message too large");
    const auto peer = std::find_if(peers_.begin(), peers_.end(), [&](const auto& p) { return p->id == id && !p->dead; });
    if (peer == peers_.end()) return;
    const auto size = static_cast<uint32_t>(message.size());
    const unsigned char header[4] = {static_cast<unsigned char>(size), static_cast<unsigned char>(size >> 8),
        static_cast<unsigned char>(size >> 16), static_cast<unsigned char>(size >> 24)};
    (*peer)->outbox.append(reinterpret_cast<const char*>(header), 4);
    (*peer)->outbox.append(message);
    if (!(*peer)->connecting) flush(**peer);
}

std::optional<NetEvent> TcpTransport::poll()
{
    if (events_.empty()) pump();
    if (events_.empty()) return std::nullopt;
    NetEvent event = std::move(events_.front());
    events_.pop_front();
    return event;
}

void TcpTransport::disconnect(PeerId id)
{
    for (auto& peer : peers_) if (peer->id == id && !peer->dead) drop(*peer);
    std::erase_if(peers_, [](const auto& p) { return p->dead; });
}

void TcpTransport::drop(Peer& peer)
{
    if (peer.dead) return;
    closeSocket(peer.socket);
    peer.dead = true;
    events_.push_back({NetEvent::Type::Disconnected, peer.id, {}});
}

void TcpTransport::flush(Peer& peer)
{
    while (!peer.dead && peer.sent < peer.outbox.size())
    {
        const auto chunk = static_cast<int>(std::min<size_t>(peer.outbox.size() - peer.sent, 1 << 20));
        const auto written = ::send(peer.socket, peer.outbox.data() + peer.sent, chunk, sendFlags);
        if (written > 0) { peer.sent += static_cast<size_t>(written); continue; }
        if (written < 0 && wouldBlock()) return;
        drop(peer);
    }
    if (peer.sent == peer.outbox.size()) { peer.outbox.clear(); peer.sent = 0; }
}

void TcpTransport::pump()
{
    if (listener_ != -1)
    {
        for (;;)
        {
            const SocketHandle s = ::accept(static_cast<SocketHandle>(listener_), nullptr, nullptr);
            if (s == invalidSocket) break;
            if (!setNonBlocking(s)) { closeSocket(s); continue; }
            tune(s);
            auto peer = std::make_unique<Peer>();
            peer->socket = s;
            peer->id = nextPeer_++;
            events_.push_back({NetEvent::Type::Connected, peer->id, {}});
            peers_.push_back(std::move(peer));
        }
    }
    char buffer[16384];
    for (auto& owned : peers_)
    {
        Peer& peer = *owned;
        if (peer.connecting)
        {
            fd_set writable, failed;
            FD_ZERO(&writable); FD_ZERO(&failed);
            FD_SET(peer.socket, &writable); FD_SET(peer.socket, &failed);
            timeval now{};
            if (select(static_cast<int>(peer.socket) + 1, nullptr, &writable, &failed, &now) <= 0) continue;
            int code = 0;
            socklen_t length = sizeof code;
            getsockopt(peer.socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&code), &length);
            if (code != 0 || FD_ISSET(peer.socket, &failed)) { drop(peer); continue; }
            peer.connecting = false;
            events_.push_back({NetEvent::Type::Connected, peer.id, {}});
        }
        flush(peer);
        bool closed = false;
        while (!peer.dead)
        {
            const auto received = ::recv(peer.socket, buffer, sizeof buffer, 0);
            if (received > 0) { peer.inbox.append(buffer, static_cast<size_t>(received)); continue; }
            closed = !(received < 0 && wouldBlock());
            break;
        }
        // Deliver whatever arrived before the peer closed, then the disconnect.
        size_t offset = 0;
        while (!peer.dead && peer.inbox.size() - offset >= 4)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*>(peer.inbox.data() + offset);
            const size_t size = bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<size_t>(bytes[3]) << 24;
            if (size > maxMessageBytes) { drop(peer); break; }
            if (peer.inbox.size() - offset - 4 < size) break;
            events_.push_back({NetEvent::Type::Message, peer.id, peer.inbox.substr(offset + 4, size)});
            offset += 4 + size;
        }
        peer.inbox.erase(0, offset);
        if (closed) drop(peer);
    }
    std::erase_if(peers_, [](const auto& p) { return p->dead; });
}

}
#endif
