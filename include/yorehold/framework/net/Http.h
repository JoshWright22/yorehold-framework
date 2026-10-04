#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yh
{

struct HttpRequest
{
    std::string method = "GET";
    std::string url; // http:// or https://
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int timeoutMs = 10000;
};

struct HttpResponse
{
    int status = 0;    // 0 = never got an answer; see error
    std::string body;
    std::string error; // why there is no answer (bad address, refused, timed out...)
    bool ok() const { return status >= 200 && status < 300; }
};

struct Url
{
    bool secure = false;
    std::string host;
    uint16_t port = 80;
    std::string path = "/"; // with the query string
};
std::optional<Url> parseUrl(std::string_view text);

std::string base64(std::string_view bytes);

// Sends the request and waits for the answer. Only built for Windows so far; elsewhere every
// request fails with an error.
HttpResponse httpSend(const HttpRequest& request);

// Requests run in the background; their callbacks run inside poll(), on the calling thread.
// Destroying the client waits for requests still in flight (their callbacks are dropped).
class HttpClient
{
public:
    using Callback = std::function<void(const HttpResponse&)>;

    HttpClient();
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    void send(HttpRequest request, Callback callback);
    // Call once per frame.
    void poll();
    size_t pending() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
