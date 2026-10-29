#include "http.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <winhttp.h>
#endif

#include <algorithm>

namespace dw4::game::online {

#ifdef _WIN32
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

std::string base64(const unsigned char* data, DWORD size) {
    DWORD length = 0;
    const DWORD flags = CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF;
    if (!CryptBinaryToStringA(data, size, flags, nullptr, &length)) return {};
    std::string out(length, '\0');
    if (!CryptBinaryToStringA(data, size, flags, out.data(), &length)) return {};
    out.resize(length);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::string spki_pin(const CERT_INFO& info) {
    BYTE* der = nullptr;
    DWORD length = 0;
    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, &info.SubjectPublicKeyInfo, CRYPT_ENCODE_ALLOC_FLAG, nullptr, &der,
                             &length))
        return {};
    unsigned char digest[32]{};
    const bool hashed = BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, der, length, digest, 32));
    LocalFree(der);
    return hashed ? base64(digest, 32) : std::string{};
}

// One request's pin check, filled in by the callback.
struct PinCheck {
    const std::vector<std::string>* pins{};
    std::string seen;
    bool checked = false;
    bool mismatch = false;
};

void CALLBACK pin_callback(HINTERNET request, DWORD_PTR context, DWORD status, LPVOID, DWORD) {
    if (status != WINHTTP_CALLBACK_STATUS_SENDING_REQUEST || context == 0) return;
    auto* check = reinterpret_cast<PinCheck*>(context);
    PCCERT_CONTEXT cert = nullptr;
    DWORD size = sizeof cert;
    if (WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &size) && cert) {
        check->seen = spki_pin(*cert->pCertInfo);
        CertFreeCertificateContext(cert);
    }
    check->checked = true;
    const bool listed = !check->seen.empty() && std::find(check->pins->begin(), check->pins->end(), check->seen) != check->pins->end();
    if (!listed) {
        check->mismatch = true;
        WinHttpCloseHandle(request); // WinHttpSendRequest fails; no request byte left the machine
    }
}

} // namespace

std::string spki_pin_of_certificate(const std::vector<std::uint8_t>& der) {
    PCCERT_CONTEXT cert = CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), DWORD(der.size()));
    if (!cert) return {};
    std::string pin = spki_pin(*cert->pCertInfo);
    CertFreeCertificateContext(cert);
    return pin;
}

HttpWorker::HttpWorker(ServerEndpoint endpoint, std::string user_agent) : endpoint_(std::move(endpoint)), user_agent_(std::move(user_agent)) {
    session_ = WinHttpOpen(widen(user_agent_).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_) {
        WinHttpSetTimeouts(session_, 5000, 5000, 15000, 15000);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session_, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        connect_ = WinHttpConnect(session_, widen(endpoint_.host).c_str(), INTERNET_PORT(endpoint_.port), 0);
    }
    thread_ = std::thread([this] { run(); });
}

HttpWorker::~HttpWorker() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (connect_) WinHttpCloseHandle(connect_);
    if (session_) WinHttpCloseHandle(session_);
}

HttpResponse HttpWorker::perform(const HttpRequest& request) {
    HttpResponse out;
    if (!connect_) {
        out.error = "WinHTTP is not available (" + std::to_string(GetLastError()) + ")";
        return out;
    }
    if (endpoint_.pinned && endpoint_.pins.empty()) {
        out.error = "this build has no pin for the server's certificate";
        return out;
    }
    std::string token;
    {
        std::lock_guard lock(mutex_);
        token = token_;
    }
    HINTERNET req = WinHttpOpenRequest(connect_, widen(request.method).c_str(), widen(endpoint_.base_path + request.path).c_str(), nullptr,
                                       WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) {
        out.error = "WinHTTP error " + std::to_string(GetLastError());
        return out;
    }
    PinCheck check{&endpoint_.pins};
    if (endpoint_.pinned) {
        DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);
        WinHttpSetStatusCallback(req, pin_callback, WINHTTP_CALLBACK_FLAG_SEND_REQUEST, 0);
    }
    std::wstring headers;
    for (const auto& [name, value] : request.headers) headers += widen(name) + L": " + widen(value) + L"\r\n";
    if (!request.content_type.empty()) headers += L"Content-Type: " + widen(request.content_type) + L"\r\n";
    if (request.authorized && !token.empty()) headers += L"Authorization: Bearer " + widen(token) + L"\r\n";
    const BOOL sent = WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), DWORD(-1),
                                         request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
                                         DWORD(request.body.size()), DWORD(request.body.size()),
                                         endpoint_.pinned ? reinterpret_cast<DWORD_PTR>(&check) : 0);
    if (endpoint_.pinned && check.mismatch) { // the callback closed the request
        out.pin_mismatch = true;
        out.server_pin = check.seen;
        out.error = "the server's certificate key is not the pinned one; nothing was sent";
        return out;
    }
    if (endpoint_.pinned && sent && !check.checked) {
        WinHttpCloseHandle(req);
        out.error = "the server's certificate was not checked";
        return out;
    }
    if (sent && WinHttpReceiveResponse(req, nullptr)) {
        DWORD status = 0, size = sizeof status;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        out.status = int(status);
        for (DWORD avail = 0; WinHttpQueryDataAvailable(req, &avail) && avail;) {
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(req, chunk.data(), avail, &read)) break;
            out.body.append(chunk, 0, read);
            if (out.body.size() > (8u << 20)) break; // answers are small; a runaway body stops here
        }
    } else {
        out.error = "WinHTTP error " + std::to_string(GetLastError());
    }
    WinHttpCloseHandle(req);
    return out;
}

#else // not Windows: no transport (the game's online client is Windows only for now)

std::string spki_pin_of_certificate(const std::vector<std::uint8_t>&) { return {}; }
HttpWorker::HttpWorker(ServerEndpoint endpoint, std::string user_agent) : endpoint_(std::move(endpoint)), user_agent_(std::move(user_agent)) {
    thread_ = std::thread([this] { run(); });
}
HttpWorker::~HttpWorker() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}
HttpResponse HttpWorker::perform(const HttpRequest&) {
    HttpResponse out;
    out.error = "no HTTPS transport on this platform";
    return out;
}

#endif

void HttpWorker::set_token(std::string token) {
    std::lock_guard lock(mutex_);
    token_ = std::move(token);
}

std::uint64_t HttpWorker::submit(HttpRequest request) {
    std::uint64_t id;
    {
        std::lock_guard lock(mutex_);
        id = next_id_++;
        queue_.emplace_back(id, std::move(request));
    }
    wake_.notify_one();
    return id;
}

std::optional<std::pair<std::uint64_t, HttpResponse>> HttpWorker::poll() {
    std::lock_guard lock(mutex_);
    if (done_.empty()) return std::nullopt;
    auto item = std::move(done_.front());
    done_.pop_front();
    return item;
}

HttpResponse HttpWorker::wait(std::uint64_t id) {
    std::unique_lock lock(mutex_);
    for (;;) {
        for (auto it = done_.begin(); it != done_.end(); ++it)
            if (it->first == id) {
                HttpResponse response = std::move(it->second);
                done_.erase(it);
                return response;
            }
        done_wake_.wait(lock);
    }
}

void HttpWorker::run() {
    for (;;) {
        std::pair<std::uint64_t, HttpRequest> item;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        HttpResponse response = perform(item.second);
        {
            std::lock_guard lock(mutex_);
            done_.emplace_back(item.first, std::move(response));
        }
        done_wake_.notify_all();
    }
}

} // namespace dw4::game::online
