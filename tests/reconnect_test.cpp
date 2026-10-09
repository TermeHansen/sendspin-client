// Copyright 2026 TermeHansen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/// @file reconnect_test.cpp
/// @brief Integration test: outbound reconnect policy of the sendspin-client binary.
///
/// Drives the real client binary against a minimal in-process Sendspin server. Since
/// sendspin-cpp v0.9 every connection is Noise-encrypted (Sendspin 1.0.0-rc1), so the fake
/// server implements the full handshake: it plays the Noise INITIATOR (the client is always the
/// responder) over the raw noise-c library, then speaks the encrypted transport.
///
/// Wire sequence (see sendspin-cpp src/noise_handshake.h):
///   1. WS upgrade.
///   2. Client sends TEXT `client/init` {client_id, version, suite}.
///   3. Server sends TEXT `server/init` {server_id, version, suite}.
///   4. Server sends TEXT `noise/handshake` msg1 (encrypted {psk_id, psk_category}).
///      prologue = bytes(client/init) || bytes(server/init).
///   5. Client sends TEXT `noise/handshake` msg2; both sides split to transport ciphers.
///   6. Transport: BINARY WS frames, each carrying a Noise ciphertext whose plaintext is
///      [0x00 | utf8(json)] for JSON control messages.
///   7. Server sends `server/hello`, then a `server/activate` (empty activities). The activate is
///      what makes the connection operational and arms the client's reconnect policy.
///
/// Scenarios:
///   1. An established outbound connection reconnects after the transport dies.
///   2. A blackholed connection is dropped by the liveness watchdog and reconnects.
///   3. Retry gaps grow while the server is unreachable (1 s doubling backoff).
///   4. Backoff resets after a successful reconnect.
///   5. RECONNECT_ON_LOSS=false leaves the connection dropped.
///
/// Registered with CTest when BUILD_TESTING=ON. See the top-level CMakeLists.txt.

#include "crypto/constants.h"
#include "noise_test_helpers.h"
#include "platform/base64.h"
#include "sendspin/client.h"
#include "sendspin/config.h"
#include "sendspin/types.h"

// noise-c is a C library.
extern "C" {
#include <noise/protocol/cipherstate.h>
#include <noise/protocol/constants.h>
#include <noise/protocol/dhstate.h>
#include <noise/protocol/handshakestate.h>
}

#include <ArduinoJson.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sendspin;  // NOLINT(google-build-using-namespace): test-local convenience

constexpr int LIVENESS_S = 3;
constexpr const char* RECONNECT_LINE = ">>> Connection lost, reconnecting to";
constexpr const char* WATCHDOG_LINE = "silent for >";
// OutboundReconnect latches was_connected from SendspinClient::is_connected(), which only turns
// true once the connection is admitted (hello + server/activate). A test that drops the transport
// before admission never arms the backoff, so every phase waits for admission, not just the
// handshake.
constexpr const char* ADMITTED_LINE = "Connection admitted: server_id=";
constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr uint8_t MSG_TYPE_JSON_BODY = 0x00;

// ============================================================================
// Minimal SHA-1 + base64 for the WebSocket upgrade (no OpenSSL dependency).
// ============================================================================

std::string sha1_base64(const std::string& in) {
    // Compact SHA-1 (RFC 3174) - only used for the WebSocket accept key.
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    std::vector<uint8_t> msg(in.begin(), in.end());
    const uint64_t bit_len = static_cast<uint64_t>(msg.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0x00);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<uint8_t>((bit_len >> (i * 8)) & 0xFF));
    }
    auto rol = [](uint32_t v, int b) { return (v << b) | (v >> (32 - b)); };
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = tmp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    uint8_t digest[20];
    for (int i = 0; i < 5; ++i) {
        digest[i * 4] = static_cast<uint8_t>(h[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i]);
    }
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < 20; i += 3) {
        const uint32_t n = (static_cast<uint32_t>(digest[i]) << 16) |
                           (i + 1 < 20 ? static_cast<uint32_t>(digest[i + 1]) << 8 : 0) |
                           (i + 2 < 20 ? static_cast<uint32_t>(digest[i + 2]) : 0);
        out.push_back(b64[(n >> 18) & 63]);
        out.push_back(b64[(n >> 12) & 63]);
        out.push_back(i + 1 < 20 ? b64[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < 20 ? b64[n & 63] : '=');
    }
    return out;
}

// ============================================================================
// Socket helpers
// ============================================================================

bool recv_exact(int fd, uint8_t* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        const ssize_t r = ::recv(fd, buf + got, n - got, 0);
        if (r <= 0) {
            return false;
        }
        got += static_cast<size_t>(r);
    }
    return true;
}

bool send_all(int fd, const uint8_t* buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        const ssize_t r = ::send(fd, buf + sent, n - sent, MSG_NOSIGNAL);
        if (r <= 0) {
            return false;
        }
        sent += static_cast<size_t>(r);
    }
    return true;
}

// ============================================================================
// WebSocket framing (server side: reads masked client frames, writes unmasked)
// ============================================================================

struct WsFrame {
    uint8_t opcode{0};
    std::vector<uint8_t> payload;
};

bool ws_read_frame(int fd, WsFrame& out) {
    uint8_t hdr[2];
    if (!recv_exact(fd, hdr, 2)) {
        return false;
    }
    out.opcode = hdr[0] & 0x0F;
    const bool masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126) {
        uint8_t ext[2];
        if (!recv_exact(fd, ext, 2)) {
            return false;
        }
        len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        uint8_t ext[8];
        if (!recv_exact(fd, ext, 8)) {
            return false;
        }
        len = 0;
        for (int i = 0; i < 8; ++i) {
            len = (len << 8) | ext[i];
        }
    }
    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !recv_exact(fd, mask, 4)) {
        return false;
    }
    out.payload.assign(len, 0);
    if (len > 0 && !recv_exact(fd, out.payload.data(), len)) {
        return false;
    }
    if (masked) {
        for (size_t i = 0; i < out.payload.size(); ++i) {
            out.payload[i] ^= mask[i % 4];
        }
    }
    return true;
}

bool ws_send(int fd, uint8_t opcode, const uint8_t* payload, size_t len) {
    std::vector<uint8_t> hdr;
    hdr.push_back(0x80 | opcode);
    if (len < 126) {
        hdr.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        hdr.push_back(126);
        hdr.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        hdr.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        hdr.push_back(127);
        for (int i = 7; i >= 0; --i) {
            hdr.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
        }
    }
    if (!send_all(fd, hdr.data(), hdr.size())) {
        return false;
    }
    return len == 0 || send_all(fd, payload, len);
}

bool ws_send_text(int fd, const std::string& s) {
    return ws_send(fd, 0x1, reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

// ============================================================================
// Noise initiator (the "server" role): raw noise-c
// ============================================================================

struct NoiseInitiator {
    NoiseHandshakeState* hs{nullptr};
    std::vector<uint8_t> server_priv;  // 32 bytes
    std::vector<uint8_t> server_pub;   // 32 bytes
    NoiseCipherState* send_cs{nullptr};  // encrypts server -> client
    NoiseCipherState* recv_cs{nullptr};  // decrypts client -> server

    ~NoiseInitiator() { reset(); }

    void reset() {
        if (send_cs) {
            noise_cipherstate_free(send_cs);
            send_cs = nullptr;
        }
        if (recv_cs) {
            noise_cipherstate_free(recv_cs);
            recv_cs = nullptr;
        }
        if (hs) {
            noise_handshakestate_free(hs);
            hs = nullptr;
        }
    }

    /// Generate the server's static X25519 keypair once. The public half is advertised in
    /// server/init, so it must stay fixed across the throwaway build that only exists to learn
    /// the public key and the real build that runs the handshake.
    bool generate_server_key() {
        NoiseDHState* dh = nullptr;
        if (noise_dhstate_new_by_id(&dh, NOISE_DH_CURVE25519) != NOISE_ERROR_NONE) {
            return false;
        }
        bool ok = noise_dhstate_generate_keypair(dh) == NOISE_ERROR_NONE;
        server_priv.assign(X25519_KEY_SIZE, 0);
        server_pub.assign(X25519_KEY_SIZE, 0);
        if (ok) {
            ok = noise_dhstate_get_keypair(dh, server_priv.data(), server_priv.size(),
                                           server_pub.data(), server_pub.size()) ==
                 NOISE_ERROR_NONE;
        }
        noise_dhstate_free(dh);
        return ok;
    }

    /// Build the initiator with the already-generated server static keypair.
    bool build(const uint8_t* remote_pub, const uint8_t* psk, const uint8_t* prologue,
               size_t prologue_len) {
        if (server_priv.empty()) {
            return false;
        }
        if (noise_handshakestate_new_by_name(&hs, NOISE_SUITE_CHACHAPOLY.data(),
                                             NOISE_ROLE_INITIATOR) != NOISE_ERROR_NONE) {
            return false;
        }
        NoiseDHState* local_dh = noise_handshakestate_get_local_keypair_dh(hs);
        if (noise_dhstate_set_keypair(local_dh, server_priv.data(), X25519_KEY_SIZE,
                                      server_pub.data(), X25519_KEY_SIZE) != NOISE_ERROR_NONE) {
            return false;
        }
        NoiseDHState* remote_dh = noise_handshakestate_get_remote_public_key_dh(hs);
        if (noise_dhstate_set_public_key(remote_dh, remote_pub, X25519_KEY_SIZE) !=
            NOISE_ERROR_NONE) {
            return false;
        }
        if (noise_handshakestate_set_pre_shared_key(hs, psk, NOISE_PSK_SIZE) !=
            NOISE_ERROR_NONE) {
            return false;
        }
        if (prologue_len > 0 &&
            noise_handshakestate_set_prologue(hs, prologue, prologue_len) != NOISE_ERROR_NONE) {
            return false;
        }
        return noise_handshakestate_start(hs) == NOISE_ERROR_NONE;
    }

    /// Write msg1 carrying the psk_id/psk_category payload. Returns the raw Noise bytes.
    std::vector<uint8_t> write_msg1(const std::string& psk_id, const std::string& psk_category) {
        const std::string payload_json =
            "{\"psk_id\":\"" + psk_id + "\",\"psk_category\":\"" + psk_category + "\"}";
        std::vector<uint8_t> out(4096);
        NoiseBuffer ob;
        noise_buffer_set_output(ob, out.data(), out.size());
        NoiseBuffer pb;
        noise_buffer_set_input(
            pb, const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(payload_json.data())),
            payload_json.size());
        if (noise_handshakestate_write_message(hs, &ob, &pb) != NOISE_ERROR_NONE) {
            return {};
        }
        out.resize(ob.size);
        return out;
    }

    /// Read msg2 and split into transport cipher states.
    bool read_msg2_and_split(const std::vector<uint8_t>& msg2) {
        std::vector<uint8_t> scratch(4096);
        NoiseBuffer ib;
        noise_buffer_set_input(ib, const_cast<uint8_t*>(msg2.data()), msg2.size());
        NoiseBuffer pb;
        noise_buffer_set_output(pb, scratch.data(), scratch.size());
        if (noise_handshakestate_read_message(hs, &ib, &pb) != NOISE_ERROR_NONE) {
            return false;
        }
        return noise_handshakestate_split(hs, &send_cs, &recv_cs) == NOISE_ERROR_NONE;
    }
};

// ============================================================================
// Client log capture (subprocess stderr)
// ============================================================================

struct ClientLog {
    std::mutex mu;
    std::vector<std::pair<double, std::string>> entries;

    void add(const std::string& line) {
        std::lock_guard<std::mutex> lk(mu);
        entries.emplace_back(now_s(), line);
    }

    static double now_s() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    std::vector<std::pair<double, std::string>> snapshot() {
        std::lock_guard<std::mutex> lk(mu);
        return entries;
    }

    size_t count(const std::string& needle) {
        size_t n = 0;
        for (auto& e : snapshot()) {
            if (e.second.find(needle) != std::string::npos) {
                ++n;
            }
        }
        return n;
    }

    std::vector<double> timestamps(const std::string& needle) {
        std::vector<double> out;
        for (auto& e : snapshot()) {
            if (e.second.find(needle) != std::string::npos) {
                out.push_back(e.first);
            }
        }
        return out;
    }

    bool wait_for(const std::string& needle, size_t want, double deadline_s) {
        const double end = now_s() + deadline_s;
        while (now_s() < end) {
            if (count(needle) >= want) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return false;
    }

    void dump() {
        std::fprintf(stderr, "--- client log ---\n");
        auto snap = snapshot();
        const double t0 = snap.empty() ? 0.0 : snap.front().first;
        for (auto& e : snap) {
            std::fprintf(stderr, "[%7.2f] %s\n", e.first - t0, e.second.c_str());
        }
    }
};

// ============================================================================
// Fake Sendspin server
// ============================================================================

class FakeServer {
public:
    explicit FakeServer(uint16_t port) : port_(port) { start_listener(); }

    ~FakeServer() { stop(); }

    bool start_listener() {
        stop_listener();
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return false;
        }
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port_);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            ::listen(fd, 4) != 0) {
            ::close(fd);
            return false;
        }
        listener_.store(fd);
        accept_thread_ = std::thread([this] { accept_loop(); });
        return true;
    }

    void stop_listener() {
        const int fd = listener_.exchange(-1);
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }
    }

    void set_muted(bool m) { muted_.store(m); }

    size_t handshake_count() { return handshakes_.load(); }

    /// Drop every live socket abruptly (RST, no WebSocket close frame).
    void kill_connections() {
        std::vector<int> socks;
        {
            std::lock_guard<std::mutex> lk(live_mu_);
            socks.swap(live_);
        }
        for (int fd : socks) {
            linger lg{1, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
            ::close(fd);
        }
    }

    void stop() {
        stopping_.store(true);
        stop_listener();
        kill_connections();
    }

private:
    void accept_loop() {
        while (!stopping_.load()) {
            const int lfd = listener_.load();
            if (lfd < 0) {
                return;
            }
            const int conn = ::accept(lfd, nullptr, nullptr);
            if (conn < 0) {
                if (stopping_.load()) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }
            std::thread([this, conn] { serve(conn); }).detach();
        }
    }

    bool ws_upgrade(int fd) {
        std::string req;
        char buf[1024];
        while (req.find("\r\n\r\n") == std::string::npos) {
            const ssize_t r = ::recv(fd, buf, sizeof(buf), 0);
            if (r <= 0) {
                return false;
            }
            req.append(buf, static_cast<size_t>(r));
        }
        std::string key;
        const std::string lower = [&] {
            std::string s = req;
            for (auto& c : s) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return s;
        }();
        const std::string needle = "sec-websocket-key:";
        const size_t p = lower.find(needle);
        if (p == std::string::npos) {
            return false;
        }
        size_t e = req.find("\r\n", p);
        key = req.substr(p + needle.size(), e - (p + needle.size()));
        while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) {
            key.erase(key.begin());
        }
        while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) {
            key.pop_back();
        }
        const std::string accept = sha1_base64(key + WS_GUID);
        const std::string resp = "HTTP/1.1 101 Switching Protocols\r\n"
                                 "Upgrade: websocket\r\n"
                                 "Connection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " +
                                 accept + "\r\n\r\n";
        return send_all(fd, reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
    }

    /// Encrypt a JSON control message and send it as a BINARY frame.
    bool send_json(NoiseInitiator& n, const std::string& json) {
        std::vector<uint8_t> pt;
        pt.push_back(MSG_TYPE_JSON_BODY);
        pt.insert(pt.end(), json.begin(), json.end());
        const std::vector<uint8_t> ct = raw_encrypt(n.send_cs, pt);
        if (ct.empty()) {
            return false;
        }
        return ws_send(fd_, 0x2, ct.data(), ct.size());
    }

    std::string server_hello() {
        JsonDocument doc;
        doc["type"] = "server/hello";
        doc["payload"]["server_id"] = b64url_encode(server_id_.data(), server_id_.size());
        doc["payload"]["name"] = "Fake Server";
        doc["payload"]["version"] = 1;
        doc["payload"]["active_roles"].add("player");
        doc["payload"]["connection_reason"] = "discovery";
        std::string out;
        serializeJson(doc, out);
        return out;
    }

    /// server/activate declaring no activities. With the Sentinel PSK (the only PSK this test
    /// client holds) and unpaired access off, the admissible activity sets are [] and ["pairing"];
    /// an empty set keeps the connection operational without claiming any role.
    std::string server_activate() {
        JsonDocument doc;
        doc["type"] = "server/activate";
        doc["payload"]["activities"].to<JsonArray>();
        std::string out;
        serializeJson(doc, out);
        return out;
    }

    std::string server_time(const JsonDocument& client_time) {
        const int64_t client_transmitted =
            client_time["payload"]["client_transmitted"] | static_cast<int64_t>(0);
        const int64_t now_us =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        JsonDocument doc;
        doc["type"] = "server/time";
        doc["payload"]["client_transmitted"] = client_transmitted;
        doc["payload"]["server_received"] = now_us;
        doc["payload"]["server_transmitted"] = now_us;
        std::string out;
        serializeJson(doc, out);
        return out;
    }

    void serve(int fd) {
        fd_ = fd;
        {
            std::lock_guard<std::mutex> lk(live_mu_);
            live_.push_back(fd);
        }
        NoiseInitiator noise;
        try {
            if (!ws_upgrade(fd)) {
                return;
            }

            // Step 2: client/init.
            WsFrame frame;
            if (!ws_read_frame(fd, frame) || frame.opcode != 0x1) {
                return;
            }
            const std::string client_init_text(frame.payload.begin(), frame.payload.end());
            JsonDocument init_doc;
            if (deserializeJson(init_doc, client_init_text) != DeserializationError::Ok) {
                return;
            }
            if (std::string(init_doc["type"] | "") != "client/init") {
                return;
            }
            const std::string client_id = init_doc["payload"]["client_id"] | "";
            auto client_pub = b64url_decode(client_id);
            if (!client_pub.has_value() || client_pub->size() != X25519_KEY_SIZE) {
                return;
            }

            // Step 3: server/init (prologue is built from the exact bytes of both).
            if (!noise.generate_server_key()) {
                return;
            }
            server_id_ = noise.server_pub;
            JsonDocument sdoc;
            sdoc["type"] = "server/init";
            sdoc["payload"]["server_id"] = b64url_encode(server_id_.data(), server_id_.size());
            sdoc["payload"]["version"] = 1;
            sdoc["payload"]["suite"] = "25519_ChaChaPoly_SHA256";
            std::string server_init_text;
            serializeJson(sdoc, server_init_text);
            if (!ws_send_text(fd, server_init_text)) {
                return;
            }

            // Build the initiator with the real prologue (reusing the advertised keypair).
            std::string prologue = client_init_text + server_init_text;
            if (!noise.build(client_pub->data(), SENTINEL_PSK.data(),
                             reinterpret_cast<const uint8_t*>(prologue.data()), prologue.size())) {
                return;
            }

            // Step 4: noise/handshake msg1 (Sentinel PSK => category "sn").
            const std::vector<uint8_t> msg1 = noise.write_msg1(SENTINEL_PSK_ID, "sn");
            if (msg1.empty()) {
                return;
            }
            JsonDocument m1doc;
            m1doc["type"] = "noise/handshake";
            m1doc["payload"]["data"] = b64url_encode(msg1.data(), msg1.size());
            std::string m1text;
            serializeJson(m1doc, m1text);
            if (!ws_send_text(fd, m1text)) {
                return;
            }

            // Step 5: client sends msg2; split to transport.
            if (!ws_read_frame(fd, frame) || frame.opcode != 0x1) {
                return;
            }
            JsonDocument m2doc;
            if (deserializeJson(m2doc, std::string(frame.payload.begin(), frame.payload.end())) !=
                DeserializationError::Ok) {
                return;
            }
            const std::string m2data = m2doc["payload"]["data"] | "";
            auto msg2 = b64url_decode(m2data);
            if (!msg2.has_value() || !noise.read_msg2_and_split(*msg2)) {
                return;
            }
            handshakes_.fetch_add(1);

            // Step 7: encrypted transport. Answer server/hello and client/time.
            if (!send_json(noise, server_hello())) {
                return;
            }

            while (!stopping_.load()) {
                if (!ws_read_frame(fd, frame)) {
                    return;
                }
                if (frame.opcode == 0x8) {
                    ws_send(fd, 0x8, nullptr, 0);
                    return;
                }
                if (frame.opcode == 0x9) {
                    ws_send(fd, 0xA, frame.payload.data(), frame.payload.size());
                    continue;
                }
                if (frame.opcode != 0x2) {
                    continue;
                }
                if (muted_.load()) {
                    continue;  // blackhole: accrue inbound silence so the watchdog fires
                }
                std::vector<uint8_t> pt = raw_decrypt(noise.recv_cs, frame.payload);
                if (pt.empty()) {
                    continue;
                }
                if (pt[0] != MSG_TYPE_JSON_BODY) {
                    continue;
                }
                const std::string body(pt.begin() + 1, pt.end());
                JsonDocument doc;
                if (deserializeJson(doc, body) != DeserializationError::Ok) {
                    continue;
                }
                const std::string type = doc["type"] | "";
                if (type == "client/time") {
                    send_json(noise, server_time(doc));
                } else if (type == "client/hello") {
                    // server/hello is sent proactively after the split; the activate that
                    // follows client/hello is what makes the connection operational.
                    send_json(noise, server_activate());
                }
            }
        } catch (...) {
            // Fall through to cleanup.
        }
        {
            std::lock_guard<std::mutex> lk(live_mu_);
            for (auto it = live_.begin(); it != live_.end(); ++it) {
                if (*it == fd) {
                    live_.erase(it);
                    break;
                }
            }
        }
        ::close(fd);
    }

    uint16_t port_;
    std::atomic<int> listener_{-1};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> muted_{false};
    std::atomic<size_t> handshakes_{0};
    std::thread accept_thread_;
    std::mutex live_mu_;
    std::vector<int> live_;
    std::vector<uint8_t> server_id_;
    int fd_{-1};
};

// ============================================================================
// Test harness
// ============================================================================

uint16_t free_port() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    const uint16_t port = ntohs(addr.sin_port);
    ::close(fd);
    return port;
}

struct ClientProc {
    pid_t pid{-1};
    int stderr_fd{-1};
    std::thread reader;
    std::shared_ptr<ClientLog> log = std::make_shared<ClientLog>();

    bool start(const std::string& client_path, const std::string& conf_path, uint16_t client_port,
               uint16_t stub_port, const std::string& state_dir) {
        int pipefd[2];
        if (::pipe(pipefd) != 0) {
            return false;
        }
        pid = ::fork();
        if (pid < 0) {
            return false;
        }
        if (pid == 0) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDERR_FILENO);
            ::close(pipefd[1]);
            ::setenv("SENDSPIN_STATE_DIR", state_dir.c_str(), 1);
            const std::string port = std::to_string(client_port);
            const std::string url = "ws://127.0.0.1:" + std::to_string(stub_port) + "/sendspin";
            ::execl(client_path.c_str(), client_path.c_str(), "-c", conf_path.c_str(), "-p",
                    port.c_str(), "-u", url.c_str(), "-l", "info", "reconnect-test",
                    static_cast<char*>(nullptr));
            _exit(127);
        }
        ::close(pipefd[1]);
        stderr_fd = pipefd[0];
        reader = std::thread([this] {
            std::string acc;
            char buf[512];
            ssize_t r;
            while ((r = ::read(stderr_fd, buf, sizeof(buf))) > 0) {
                acc.append(buf, static_cast<size_t>(r));
                size_t nl;
                while ((nl = acc.find('\n')) != std::string::npos) {
                    log->add(acc.substr(0, nl));
                    acc.erase(0, nl + 1);
                }
            }
            if (!acc.empty()) {
                log->add(acc);
            }
        });
        return true;
    }

    bool stop() {
        if (pid <= 0) {
            return true;
        }
        ::kill(pid, SIGTERM);
        int status = 0;
        for (int i = 0; i < 100; ++i) {
            const pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (::waitpid(pid, &status, WNOHANG) == 0) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
        }
        if (reader.joinable()) {
            reader.join();
        }
        pid = -1;
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
};

bool wait_until(const std::function<bool()>& pred, double deadline_s, const std::string& message,
                ClientLog* log = nullptr) {
    const double end = ClientLog::now_s() + deadline_s;
    while (ClientLog::now_s() < end) {
        if (pred()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::fprintf(stderr, "FAIL: timeout: %s\n", message.c_str());
    if (log != nullptr) {
        log->dump();
    }
    return false;
}

std::string write_conf(const std::string& dir, const std::string& name, bool reconnect_enabled) {
    const std::string path = dir + "/" + name;
    std::ofstream out(path, std::ios::trunc);
    out << "LIVENESS_TIMEOUT = " << LIVENESS_S << "\n";
    out << "RECONNECT_ON_LOSS = " << (reconnect_enabled ? "true" : "false") << "\n";
    out << "ENABLE_MDNS = false\n";
    out.close();
    return path;
}

// ============================================================================
// Scenarios
// ============================================================================

bool test_reconnect_enabled(const std::string& client_path, const std::string& workdir) {
    const uint16_t stub_port = free_port();
    const uint16_t client_port = free_port();
    const std::string conf = write_conf(workdir, "enabled.conf", true);
    FakeServer stub(stub_port);
    ClientProc proc;

    if (!proc.start(client_path, conf, client_port, stub_port, workdir)) {
        std::fprintf(stderr, "FAIL: could not start client\n");
        return false;
    }
    bool ok = true;

    // Phase A: initial handshake and admission. Admission is what arms the reconnect policy.
    ok &= wait_until([&] { return stub.handshake_count() >= 1; }, 15,
                     "initial handshake never completed", proc.log.get());
    ok &= proc.log->wait_for(ADMITTED_LINE, 1, 10);

    // Phase B: transport loss (RST) -> immediate reconnect -> recovery.
    stub.kill_connections();
    ok &= proc.log->wait_for(RECONNECT_LINE, 1, LIVENESS_S + 10);
    ok &= wait_until([&] { return stub.handshake_count() >= 2; }, 10,
                     "no second handshake after transport loss", proc.log.get());
    ok &= proc.log->wait_for(ADMITTED_LINE, 2, 10);

    // Phase C: blackhole (mute) with the listener down -> watchdog drop, then failing retries
    // with growing gaps.
    stub.set_muted(true);
    stub.stop_listener();
    ok &= proc.log->wait_for(WATCHDOG_LINE, 1, LIVENESS_S + 10);
    ok &= proc.log->wait_for(RECONNECT_LINE, 2, LIVENESS_S + 10);
    ok &= wait_until([&] { return proc.log->timestamps(RECONNECT_LINE).size() >= 4; }, 25,
                     "expected at least 4 reconnect attempts (backoff phase)", proc.log.get());
    {
        const auto ts = proc.log->timestamps(RECONNECT_LINE);
        if (ts.size() >= 2) {
            const double gap_last = ts[ts.size() - 1] - ts[ts.size() - 2];
            const double gap_prev = ts[ts.size() - 2] - ts[ts.size() - 3 < 0 ? 0 : ts.size() - 3];
            if (gap_last < 1.2) {
                std::fprintf(stderr, "FAIL: retry gap does not grow (%.2fs)\n", gap_last);
                ok = false;
            }
            if (gap_last < gap_prev + 0.8) {
                std::fprintf(stderr, "FAIL: retry gaps do not double (%.2fs then %.2fs)\n",
                             gap_prev, gap_last);
                ok = false;
            }
        }
    }

    // Phase D: server returns -> next due retry reconnects.
    stub.set_muted(false);
    stub.start_listener();
    ok &= wait_until([&] { return stub.handshake_count() >= 3; }, 15,
                     "no handshake after the server came back", proc.log.get());
    ok &= proc.log->wait_for(ADMITTED_LINE, 3, 10);
    // OutboundReconnect resets its backoff from the main loop's next tick (10 ms cadence), so
    // let it observe the admission before the transport is dropped again.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Phase E: transport loss again -> recovery must be prompt (backoff reset by Phase D).
    const size_t prev_attempts = proc.log->count(RECONNECT_LINE);
    stub.kill_connections();
    ok &= wait_until(
        [&] {
            const auto ts = proc.log->timestamps(RECONNECT_LINE);
            return ts.size() >= prev_attempts + 1;
        },
        2.0 + LIVENESS_S + 2, "reconnect after a recovery was not prompt (backoff not reset?)",
        proc.log.get());
    ok &= wait_until([&] { return stub.handshake_count() >= 4; }, 10,
                     "no fourth handshake after backoff reset", proc.log.get());

    if (!proc.stop()) {
        std::fprintf(stderr, "FAIL: client did not shut down cleanly\n");
        ok = false;
    }
    stub.stop();
    return ok;
}

bool test_reconnect_disabled(const std::string& client_path, const std::string& workdir) {
    const uint16_t stub_port = free_port();
    const uint16_t client_port = free_port();
    const std::string conf = write_conf(workdir, "disabled.conf", false);
    FakeServer stub(stub_port);
    ClientProc proc;

    if (!proc.start(client_path, conf, client_port, stub_port, workdir)) {
        std::fprintf(stderr, "FAIL: could not start client\n");
        return false;
    }
    bool ok = true;

    ok &= wait_until([&] { return stub.handshake_count() >= 1; }, 15,
                     "initial handshake never completed", proc.log.get());

    stub.set_muted(true);
    ok &= proc.log->wait_for(WATCHDOG_LINE, 1, LIVENESS_S + 10);
    std::this_thread::sleep_for(std::chrono::seconds(4));
    if (proc.log->count(RECONNECT_LINE) != 0) {
        std::fprintf(stderr, "FAIL: reconnect attempted despite RECONNECT_ON_LOSS=false\n");
        ok = false;
    }
    if (stub.handshake_count() != 1) {
        std::fprintf(stderr, "FAIL: new handshake despite RECONNECT_ON_LOSS=false\n");
        ok = false;
    }

    if (!proc.stop()) {
        std::fprintf(stderr, "FAIL: client did not shut down cleanly\n");
        ok = false;
    }
    stub.stop();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <sendspin-client binary>\n", argv[0]);
        return 2;
    }
    const std::string client_path = argv[1];

    char tmpl[] = "/tmp/sendspin-reconnect-test-XXXXXX";
    const char* dir = ::mkdtemp(tmpl);
    if (dir == nullptr) {
        std::fprintf(stderr, "could not create temp dir\n");
        return 2;
    }
    const std::string workdir = dir;

    bool ok = test_reconnect_enabled(client_path, workdir);
    ok = test_reconnect_disabled(client_path, workdir) && ok;

    if (ok) {
        std::fprintf(stderr, "reconnect test: all scenarios passed\n");
        return 0;
    }
    std::fprintf(stderr, "reconnect test: FAILED\n");
    return 1;
}
