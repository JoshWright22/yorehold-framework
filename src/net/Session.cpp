#include "yorehold/framework/net/Session.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace yh
{

namespace
{
using nlohmann::json;
const std::string ping = json{{"t", "ping"}}.dump();
std::string bye(std::string_view reason) { return json{{"t", "bye"}, {"reason", reason}}.dump(); }
}

SessionHost::SessionHost(std::unique_ptr<Transport> transport, std::string game, std::string version, Rules rules, SessionSettings settings)
    : transport_(std::move(transport)), game_(std::move(game)), version_(std::move(version)), rules_(std::move(rules)), settings_(settings)
{
    if (!transport_ || !rules_.validate || !rules_.apply || !rules_.snapshot) throw std::invalid_argument("Session host needs a transport, validate, apply and snapshot");
}

SessionHost::~SessionHost()
{
    for (auto& [id, peer] : peers_) transport_->send(id, bye("Host closed the session"));
}

bool SessionHost::submit(std::string_view type, std::string_view data, std::string* reason)
{
    std::string why;
    auto accepted = rules_.validate(0, type, data, why);
    if (!accepted) { if (reason) *reason = why; return false; }
    accept(0, type, std::move(*accepted));
    return true;
}

void SessionHost::accept(PlayerId player, std::string_view type, std::string data)
{
    // Submitting from inside apply would apply the nested command first on the host but second on clients.
    if (applying_) throw std::logic_error("Cannot submit while applying a command");
    NetCommand command{++sequence_, player, std::string(type), std::move(data)};
    applying_ = true;
    struct Guard { bool& flag; ~Guard() { flag = false; } } guard{applying_};
    rules_.apply(command);
    json message{{"t", "cmd"}, {"seq", command.sequence}, {"player", player}, {"type", command.type}, {"data", command.data}};
    if (rules_.checksum) message["sum"] = rules_.checksum();
    const std::string text = message.dump();
    for (auto& [id, peer] : peers_) if (peer.player >= 0) sendTo(id, peer, text);
}

void SessionHost::update(double deltaSeconds)
{
    while (auto event = transport_->poll())
    {
        switch (event->type)
        {
        case NetEvent::Type::Connected: peers_[event->peer] = {}; break;
        case NetEvent::Type::Disconnected:
            if (const auto peer = peers_.find(event->peer); peer != peers_.end())
            {
                const PlayerId player = peer->second.player;
                peers_.erase(peer);
                if (player >= 0 && rules_.left) rules_.left(player);
            }
            break;
        case NetEvent::Type::Message:
            if (const auto peer = peers_.find(event->peer); peer != peers_.end())
            {
                peer->second.silent = 0;
                receive(event->peer, peer->second, event->data);
            }
            break;
        }
    }
    std::vector<PeerId> silent;
    for (auto& [id, peer] : peers_)
    {
        peer.silent += deltaSeconds;
        peer.quiet += deltaSeconds;
        if (peer.silent > settings_.timeoutSeconds) silent.push_back(id);
        else if (peer.quiet > settings_.heartbeatSeconds) sendTo(id, peer, ping);
    }
    for (PeerId id : silent) drop(id, "Timed out");
}

void SessionHost::receive(PeerId from, Peer& peer, std::string_view text)
{
    const json message = json::parse(text, nullptr, false);
    if (!message.is_object()) { drop(from, "Bad message"); return; }
    const std::string kind = message.value("t", "");
    try
    {
        if (kind == "hello" && peer.player < 0)
        {
            if (message.value("game", "") != game_ || message.value("version", "") != version_)
            {
                drop(from, "Version mismatch: the host runs " + game_ + " " + version_);
                return;
            }
            peer.name = message.value("name", "");
            std::string reason;
            if (rules_.admit && !rules_.admit(peer.name, reason)) { drop(from, reason.empty() ? "Refused" : reason); return; }
            peer.player = nextPlayer_++;
            sendTo(from, peer, json{{"t", "welcome"}, {"player", peer.player}, {"seq", sequence_}, {"state", rules_.snapshot()}}.dump());
            if (rules_.joined) rules_.joined(peer.player, peer.name);
        }
        else if (kind == "intent" && peer.player >= 0)
        {
            const auto id = message.at("id").get<uint64_t>();
            const auto type = message.at("type").get<std::string>();
            std::string reason;
            auto accepted = rules_.validate(peer.player, type, message.at("data").get<std::string>(), reason);
            if (accepted) accept(peer.player, type, std::move(*accepted));
            else sendTo(from, peer, json{{"t", "reject"}, {"id", id}, {"reason", reason}}.dump());
        }
        else if (kind == "bye") transport_->disconnect(from);
        else if (kind != "ping") drop(from, "Unexpected message");
    }
    catch (const json::exception&) { drop(from, "Bad message"); }
}

void SessionHost::sendTo(PeerId to, Peer& peer, const std::string& message)
{
    peer.quiet = 0;
    transport_->send(to, message);
}

void SessionHost::drop(PeerId peer, std::string_view reason)
{
    transport_->send(peer, bye(reason));
    transport_->disconnect(peer); // the Disconnected event reports the departure
}

void SessionHost::kick(PlayerId player, std::string_view reason)
{
    for (const auto& [id, peer] : peers_)
        if (peer.player == player) { drop(id, reason); return; }
}

std::vector<PlayerId> SessionHost::players() const
{
    std::vector<PlayerId> result{0};
    for (const auto& [id, peer] : peers_) if (peer.player >= 0) result.push_back(peer.player);
    return result;
}

std::string_view SessionHost::playerName(PlayerId player) const
{
    for (const auto& [id, peer] : peers_) if (peer.player == player) return peer.name;
    return {};
}

SessionClient::SessionClient(std::unique_ptr<Transport> transport, std::string game, std::string version, std::string name, Handlers handlers, SessionSettings settings)
    : transport_(std::move(transport)), game_(std::move(game)), version_(std::move(version)), name_(std::move(name)), handlers_(std::move(handlers)), settings_(settings)
{
    if (!transport_ || !handlers_.welcomed || !handlers_.apply) throw std::invalid_argument("Session client needs a transport, welcomed and apply");
}

SessionClient::~SessionClient()
{
    if (!ended_ && host_ != 0) transport_->send(host_, bye("Left"));
}

uint64_t SessionClient::submit(std::string_view type, std::string_view data)
{
    if (!joined() || ended_) return 0;
    const uint64_t id = nextIntent_++;
    sendHost(json{{"t", "intent"}, {"id", id}, {"type", type}, {"data", data}}.dump());
    return id;
}

void SessionClient::update(double deltaSeconds)
{
    while (auto event = transport_->poll())
    {
        if (ended_) continue;
        switch (event->type)
        {
        case NetEvent::Type::Connected:
            host_ = event->peer;
            sendHost(json{{"t", "hello"}, {"game", game_}, {"version", version_}, {"name", name_}}.dump());
            break;
        case NetEvent::Type::Disconnected: end(host_ == 0 ? "Could not connect" : "Lost connection to the host"); break;
        case NetEvent::Type::Message: silent_ = 0; receive(event->data); break;
        }
    }
    if (ended_ || host_ == 0) return;
    silent_ += deltaSeconds;
    quiet_ += deltaSeconds;
    if (silent_ > settings_.timeoutSeconds) end("The host stopped responding");
    else if (quiet_ > settings_.heartbeatSeconds) sendHost(ping);
}

void SessionClient::receive(std::string_view text)
{
    const json message = json::parse(text, nullptr, false);
    if (!message.is_object()) { end("Bad message from the host"); return; }
    try
    {
        const std::string kind = message.value("t", "");
        if (kind == "welcome" && !joined())
        {
            player_ = message.at("player").get<PlayerId>();
            sequence_ = message.at("seq").get<uint64_t>();
            handlers_.welcomed(player_, message.at("state").get<std::string>());
        }
        else if (kind == "cmd" && joined())
        {
            NetCommand command{message.at("seq").get<uint64_t>(), message.at("player").get<PlayerId>(),
                message.at("type").get<std::string>(), message.at("data").get<std::string>()};
            if (command.sequence != sequence_ + 1) { end("Out of sync with the host"); return; }
            sequence_ = command.sequence;
            handlers_.apply(command);
            if (message.contains("sum") && handlers_.checksum && handlers_.checksum() != message.at("sum").get<uint64_t>())
                end("Out of sync with the host");
        }
        else if (kind == "reject") { if (handlers_.rejected) handlers_.rejected(message.at("id").get<uint64_t>(), message.value("reason", "")); }
        else if (kind == "bye") end(message.value("reason", "The host closed the session"));
    }
    catch (const json::exception&) { end("Bad message from the host"); }
}

void SessionClient::sendHost(const std::string& message)
{
    quiet_ = 0;
    transport_->send(host_, message);
}

void SessionClient::leave()
{
    if (ended_) return;
    if (host_ != 0) sendHost(bye("Left"));
    end("Left the session");
}

void SessionClient::end(std::string_view reason)
{
    if (ended_) return;
    ended_ = true;
    if (host_ != 0) transport_->disconnect(host_);
    if (handlers_.disconnected) handlers_.disconnected(reason);
}

}
