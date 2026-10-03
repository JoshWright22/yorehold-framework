#pragma once

#include "yorehold/framework/net/Transport.h"

#include <functional>
#include <map>

namespace yh
{

// The host is player 0; clients get increasing ids that are never reused within a session.
using PlayerId = int;

// An accepted action, applied in the same order by the host and every client.
struct NetCommand
{
    uint64_t sequence = 0;
    PlayerId player = 0;
    std::string type;
    std::string data;
};

struct SessionSettings
{
    double heartbeatSeconds = 2;  // send a ping when nothing else was sent for this long
    double timeoutSeconds = 15;   // drop peers silent for this long
};

// Host-authoritative sessions. Players send *intents* ("move token 3 to 4,5"); the host's rules
// accept or reject them and turn accepted ones into *commands* with a sequence number. Everyone,
// host included, changes game state only by applying commands, so all copies stay identical.
// Joiners receive a snapshot of the host's state, then every command after it.
class SessionHost
{
public:
    struct Rules
    {
        // Return the command data to broadcast (the host may fill in rolls), or nothing to reject with `reason`.
        std::function<std::optional<std::string>(PlayerId, std::string_view type, std::string_view data, std::string& reason)> validate;
        std::function<void(const NetCommand&)> apply;
        // Full game state for a joining player.
        std::function<std::string()> snapshot;
        // Optional: a hash of game state after each command; clients compare theirs to catch desyncs.
        std::function<uint64_t()> checksum;
        // Optional: return false (with a reason) to refuse a joiner, e.g. a full table.
        std::function<bool(std::string_view name, std::string& reason)> admit;
        std::function<void(PlayerId, std::string_view name)> joined;
        std::function<void(PlayerId)> left;
    };

    SessionHost(std::unique_ptr<Transport> transport, std::string game, std::string version, Rules rules, SessionSettings settings = {});
    ~SessionHost();

    // The host's own intents go through the same rules.
    bool submit(std::string_view type, std::string_view data, std::string* reason = nullptr);
    // Polls the transport and handles joins, intents and timeouts. Call once per frame.
    void update(double deltaSeconds);
    void kick(PlayerId player, std::string_view reason);
    std::vector<PlayerId> players() const;
    std::string_view playerName(PlayerId player) const;
    uint64_t sequence() const { return sequence_; }

private:
    struct Peer
    {
        PlayerId player = -1; // -1 until the hello is accepted
        std::string name;
        double silent = 0, quiet = 0;
    };
    void receive(PeerId from, Peer& peer, std::string_view message);
    void accept(PlayerId player, std::string_view type, std::string data);
    void sendTo(PeerId to, Peer& peer, const std::string& message);
    void drop(PeerId peer, std::string_view reason);

    std::unique_ptr<Transport> transport_;
    std::string game_, version_;
    Rules rules_;
    SessionSettings settings_;
    std::map<PeerId, Peer> peers_;
    uint64_t sequence_ = 0;
    PlayerId nextPlayer_ = 1;
    bool applying_ = false;
};

class SessionClient
{
public:
    struct Handlers
    {
        // Replace local game state with the host's snapshot; commands follow.
        std::function<void(PlayerId self, std::string_view snapshot)> welcomed;
        std::function<void(const NetCommand&)> apply;
        std::function<void(uint64_t intent, std::string_view reason)> rejected;
        // Called once when the session ends for any reason (refused, kicked, host gone, desync, timeout).
        std::function<void(std::string_view reason)> disconnected;
        // Optional: must match the host's Rules::checksum after each command.
        std::function<uint64_t()> checksum;
    };

    SessionClient(std::unique_ptr<Transport> transport, std::string game, std::string version, std::string name, Handlers handlers, SessionSettings settings = {});
    ~SessionClient();

    // Returns an id echoed by `rejected`, or 0 when not in a session yet.
    uint64_t submit(std::string_view type, std::string_view data);
    void update(double deltaSeconds);
    void leave();
    bool joined() const { return player_ >= 0; }
    bool ended() const { return ended_; }
    PlayerId player() const { return player_; }
    uint64_t sequence() const { return sequence_; }

private:
    void receive(std::string_view message);
    void end(std::string_view reason);
    void sendHost(const std::string& message);

    std::unique_ptr<Transport> transport_;
    std::string game_, version_, name_;
    Handlers handlers_;
    SessionSettings settings_;
    PeerId host_ = 0;
    PlayerId player_ = -1;
    uint64_t sequence_ = 0;
    uint64_t nextIntent_ = 1;
    double silent_ = 0, quiet_ = 0;
    bool ended_ = false;
};

}
