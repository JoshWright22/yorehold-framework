#include "yorehold/framework/net/Http.h"

#include <charconv>
#include <deque>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace yh
{

std::optional<Url> parseUrl(std::string_view text)
{
    Url url;
    if (text.starts_with("https://")) { url.secure = true; url.port = 443; text.remove_prefix(8); }
    else if (text.starts_with("http://")) text.remove_prefix(7);
    else return std::nullopt;

    const size_t slash = text.find_first_of("/?");
    std::string_view authority = text.substr(0, slash);
    if (slash != std::string_view::npos)
    {
        url.path = text.substr(slash);
        if (url.path.front() == '?') url.path.insert(url.path.begin(), '/');
    }

    // [::1]:7350 keeps its brackets off the host name.
    std::string_view host = authority;
    std::string_view port;
    if (authority.starts_with('['))
    {
        const size_t close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size())
        {
            if (authority[close + 1] != ':') return std::nullopt;
            port = authority.substr(close + 2);
        }
    }
    else if (const size_t colon = authority.rfind(':'); colon != std::string_view::npos)
    {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
        if (port.empty()) return std::nullopt;
    }
    if (host.empty()) return std::nullopt;
    url.host = host;
    if (!port.empty())
    {
        unsigned value = 0;
        const auto [end, error] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (error != std::errc{} || end != port.data() + port.size() || value == 0 || value > 65535) return std::nullopt;
        url.port = static_cast<uint16_t>(value);
    }
    return url;
}

std::string base64(std::string_view bytes)
{
    static constexpr char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const size_t left = bytes.size() - i;
        const uint32_t chunk = static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << 16
            | (left > 1 ? static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8 : 0)
            | (left > 2 ? static_cast<uint32_t>(static_cast<unsigned char>(bytes[i + 2])) : 0);
        out += table[chunk >> 18 & 63];
        out += table[chunk >> 12 & 63];
        out += left > 1 ? table[chunk >> 6 & 63] : '=';
        out += left > 2 ? table[chunk & 63] : '=';
    }
    return out;
}

#if defined(_WIN32)

namespace
{

std::wstring wide(std::string_view text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

struct Handle
{
    HINTERNET handle = nullptr;
    ~Handle() { if (handle) WinHttpCloseHandle(handle); }
    explicit operator bool() const { return handle != nullptr; }
};

std::string lastError(const char* step)
{
    const DWORD code = GetLastError();
    switch (code)
    {
    case ERROR_WINHTTP_CANNOT_CONNECT: return "could not connect";
    case ERROR_WINHTTP_TIMEOUT: return "timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "server name not found";
    case ERROR_WINHTTP_CONNECTION_ERROR: return "connection lost";
    case ERROR_WINHTTP_SECURE_FAILURE: return "secure connection failed";
    default: return std::string(step) + " failed (" + std::to_string(code) + ")";
    }
}

}

HttpResponse httpSend(const HttpRequest& request)
{
    HttpResponse response;
    const std::optional<Url> url = parseUrl(request.url);
    if (!url) { response.error = "bad address"; return response; }

    const Handle session{WinHttpOpen(L"Yorehold", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) { response.error = lastError("open"); return response; }
    WinHttpSetTimeouts(session.handle, request.timeoutMs, request.timeoutMs, request.timeoutMs, request.timeoutMs);

    const Handle connection{WinHttpConnect(session.handle, wide(url->host).c_str(), url->port, 0)};
    if (!connection) { response.error = lastError("connect"); return response; }

    const Handle call{WinHttpOpenRequest(connection.handle, wide(request.method).c_str(), wide(url->path).c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, url->secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!call) { response.error = lastError("request"); return response; }

    std::wstring headers;
    for (const auto& [name, value] : request.headers) headers += wide(name) + L": " + wide(value) + L"\r\n";
    if (!headers.empty() && !WinHttpAddRequestHeaders(call.handle, headers.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD))
    {
        response.error = lastError("headers");
        return response;
    }

    const DWORD bodySize = static_cast<DWORD>(request.body.size());
    void* body = bodySize ? const_cast<char*>(request.body.data()) : WINHTTP_NO_REQUEST_DATA;
    if (!WinHttpSendRequest(call.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, body, bodySize, bodySize, 0)
        || !WinHttpReceiveResponse(call.handle, nullptr))
    {
        response.error = lastError("send");
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(call.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
        &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(call.handle, &available)) { response.error = lastError("read"); return response; }
        if (available == 0) break;
        const size_t before = response.body.size();
        response.body.resize(before + available);
        DWORD read = 0;
        if (!WinHttpReadData(call.handle, response.body.data() + before, available, &read)) { response.error = lastError("read"); return response; }
        response.body.resize(before + read);
        if (read == 0) break;
    }
    response.status = static_cast<int>(status);
    return response;
}

#else

HttpResponse httpSend(const HttpRequest&)
{
    HttpResponse response;
    response.error = "online play is not built for this platform yet";
    return response;
}

#endif

struct HttpClient::State
{
    std::mutex mutex;
    std::deque<std::pair<Callback, HttpResponse>> finished;
    std::vector<std::thread> threads;
    size_t pending = 0;
    bool closing = false;
};

HttpClient::HttpClient() : state_(std::make_shared<State>()) {}

HttpClient::~HttpClient()
{
    { const std::lock_guard lock(state_->mutex); state_->closing = true; }
    for (std::thread& thread : state_->threads) thread.join();
}

void HttpClient::send(HttpRequest request, Callback callback)
{
    const std::lock_guard lock(state_->mutex);
    ++state_->pending;
    state_->threads.emplace_back([state = state_, request = std::move(request), callback = std::move(callback)]() mutable {
        HttpResponse response = httpSend(request);
        const std::lock_guard done(state->mutex);
        if (!state->closing) state->finished.emplace_back(std::move(callback), std::move(response));
    });
}

void HttpClient::poll()
{
    for (;;)
    {
        std::pair<Callback, HttpResponse> next;
        {
            const std::lock_guard lock(state_->mutex);
            if (state_->finished.empty()) break;
            next = std::move(state_->finished.front());
            state_->finished.pop_front();
            --state_->pending;
        }
        // Outside the lock: the callback may send another request.
        if (next.first) next.first(next.second);
    }
}

size_t HttpClient::pending() const
{
    const std::lock_guard lock(state_->mutex);
    return state_->pending;
}

}
