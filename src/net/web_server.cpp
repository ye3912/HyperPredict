/**
 * HyperPredict Web Server Implementation
 * Lightweight embedded HTTP + WebSocket server (no external dependencies)
 */

#include "net/web_server.h"
#include "core/logger.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

namespace hp {
namespace net {

// ============= SHA1 Implementation (no OpenSSL) =============

static void sha1_init(uint32_t state[5]) {
    state[0] = 0x67452301;
    state[1] = 0xEFCDAB89;
    state[2] = 0x98BADCFE;
    state[3] = 0x10325476;
    state[4] = 0xC3D2E1F0;
}

#define SHA1_ROTL(n, x) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];
    
    for (int i = 0; i < 16; i++) {
        w[i] = (buffer[i*4] << 24) | (buffer[i*4+1] << 16) | (buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROTL(1, w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]);
    }
    
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        
        uint32_t temp = SHA1_ROTL(5, a) + f + e + k + w[i];
        e = d; d = c; c = SHA1_ROTL(30, b); b = a; a = temp;
    }
    
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_final(uint8_t digest[20], uint32_t state[5]) {
    digest[0] = (state[0] >> 24) & 0xFF;
    digest[1] = (state[0] >> 16) & 0xFF;
    digest[2] = (state[0] >> 8) & 0xFF;
    digest[3] = state[0] & 0xFF;
    digest[4] = (state[1] >> 24) & 0xFF;
    digest[5] = (state[1] >> 16) & 0xFF;
    digest[6] = (state[1] >> 8) & 0xFF;
    digest[7] = state[1] & 0xFF;
    digest[8] = (state[2] >> 24) & 0xFF;
    digest[9] = (state[2] >> 16) & 0xFF;
    digest[10] = (state[2] >> 8) & 0xFF;
    digest[11] = state[2] & 0xFF;
    digest[12] = (state[3] >> 24) & 0xFF;
    digest[13] = (state[3] >> 16) & 0xFF;
    digest[14] = (state[3] >> 8) & 0xFF;
    digest[15] = state[3] & 0xFF;
    digest[16] = (state[4] >> 24) & 0xFF;
    digest[17] = (state[4] >> 16) & 0xFF;
    digest[18] = (state[4] >> 8) & 0xFF;
    digest[19] = state[4] & 0xFF;
}

static std::string sha1_base64(const std::string& input) {
    uint32_t state[5];
    sha1_init(state);
    
    size_t i = 0;
    uint8_t buffer[64];
    size_t len = input.size();
    
    // Process complete 64-byte blocks
    while (i + 64 <= len) {
        sha1_transform(state, reinterpret_cast<const uint8_t*>(input.data() + i));
        i += 64;
    }
    
    // Process remaining bytes
    size_t remaining = len - i;
    memcpy(buffer, input.data() + i, remaining);
    buffer[remaining] = 0x80;
    memset(buffer + remaining + 1, 0, 63 - remaining);
    
    if (remaining >= 56) {
        sha1_transform(state, buffer);
        memset(buffer, 0, 56);
    }
    
    // Append length in bits
    uint64_t bits = len * 8;
    for (int j = 0; j < 8; j++) {
        buffer[56 + j] = (bits >> (56 - j * 8)) & 0xFF;
    }
    sha1_transform(state, buffer);
    
    uint8_t digest[20];
    sha1_final(digest, state);
    
    // Base64 encode
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    for (int i = 0; i < 20; i += 3) {
        result += alphabet[digest[i] >> 2];
        result += alphabet[((digest[i] & 3) << 4) | (digest[i+1] >> 4)];
        result += alphabet[((digest[i+1] & 15) << 2) | (digest[i+2] >> 6)];
        result += alphabet[digest[i+2] & 63];
    }
    return result;
}

// ============= Utility Functions =============

static std::string base64_encode(const uint8_t* data, size_t len) {
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    
    for (size_t i = 0; i < len; i += 3) {
        int a = data[i];
        int b = (i + 1 < len) ? data[i + 1] : 0;
        int c = (i + 2 < len) ? data[i + 2] : 0;
        
        result += alphabet[a >> 2];
        result += alphabet[((a & 3) << 4) | (b >> 4)];
        result += (i + 1 < len) ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        result += (i + 2 < len) ? alphabet[c & 63] : '=';
    }
    
    return result;
}

static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void set_reuse_addr(int fd) {
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}

static void set_no_delay(int fd) {
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

// ============= StatusUpdate =============

std::string StatusUpdate::to_json() const {
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{"
        "\"timestamp\":%lu,"
        "\"fps\":%u,"
        "\"target_fps\":%u,"
        "\"cpu_util\":%u,"
        "\"run_queue_len\":%u,"
        "\"wakeups_100ms\":%u,"
        "\"frame_interval_us\":%u,"
        "\"touch_rate_100ms\":%u,"
        "\"thermal_margin\":%d,"
        "\"temperature\":%d,"
        "\"battery_level\":%d,"
        "\"is_gaming\":%s,"
        "\"mode\":\"%s\","
        "\"uclamp_min\":%u,"
        "\"uclamp_max\":%u"
        "}",
        timestamp,
        fps,
        target_fps,
        cpu_util,
        run_queue_len,
        wakeups_100ms,
        frame_interval_us,
        touch_rate_100ms,
        thermal_margin,
        temperature,
        battery_level,
        is_gaming ? "true" : "false",
        mode.c_str(),
        uclamp_min,
        uclamp_max
    );
    return std::string(buf);
}

// ============= ModelWeights =============

std::string ModelWeights::to_json() const {
    std::string nn_str = has_nn ? "true" : "false";
    
    char buf[2048];
    snprintf(buf, sizeof(buf),
        "{"
        "\"type\":\"model_weights\","
        "\"linear\":{"
        "\"w_util\":%.4f,"
        "\"w_rq\":%.4f,"
        "\"w_wakeups\":%.4f,"
        "\"w_frame\":%.4f,"
        "\"w_touch\":%.4f,"
        "\"w_thermal\":%.4f,"
        "\"w_battery\":%.4f,"
        "\"bias\":%.4f,"
        "\"ema_error\":%.4f"
        "},"
        "\"has_nn\":%s",
        w_util, w_rq, w_wakeups, w_frame, w_touch, w_thermal, w_battery, bias, ema_error,
        nn_str.c_str()
    );
    
    std::string json = buf;
    
    // 添加神经网络权重
    if (has_nn && nn_weights.size() >= 2 && nn_biases.size() >= 2) {
        json += ",\"nn_weights\":[";
        // 层1: 4×8
        json += "[";
        for (size_t h = 0; h < nn_weights[0].size(); h++) {
            if (h > 0) json += ",";
            json += "[";
            for (size_t i = 0; i < nn_weights[0][h].size(); i++) {
                if (i > 0) json += ",";
                json += std::to_string(nn_weights[0][h][i]);
            }
            json += "]";
        }
        json += "],";
        // 层2: 1×4
        json += "[";
        for (size_t h = 0; h < nn_weights[1][0].size(); h++) {
            if (h > 0) json += ",";
            json += std::to_string(nn_weights[1][0][h]);
        }
        json += "]]";
        
        json += ",\"nn_biases\":[[";
        for (size_t i = 0; i < nn_biases[0].size(); i++) {
            if (i > 0) json += ",";
            json += std::to_string(nn_biases[0][i]);
        }
        json += "],[" + std::to_string(nn_biases[1][0]) + "]]";
    }
    
    json += "}";
    return json;
}

// ============= WebCommand =============

bool WebCommand::get_string(const std::string& key, std::string& out) const {
    auto it = params.find(key);
    if (it != params.end()) {
        out = it->second;
        return true;
    }
    return false;
}

bool WebCommand::get_int(const std::string& key, int& out) const {
    auto it = params.find(key);
    if (it != params.end()) {
        out = atoi(it->second.c_str());
        return true;
    }
    return false;
}

bool WebCommand::get_float(const std::string& key, float& out) const {
    auto it = params.find(key);
    if (it != params.end()) {
        out = atof(it->second.c_str());
        return true;
    }
    return false;
}

// ============= WebServer =============

WebServer::WebServer(uint16_t port)
    : port_(port)
    , server_fd_(-1)
    , running_(false)
    , stop_accept_(false)
    , delegate_(nullptr)
    , next_client_id_(1) {
    FD_ZERO(&read_fds_);
    FD_ZERO(&write_fds_);
}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start() noexcept {
    if (running_.load()) {
        LOGW("WebServer already running");
        return true;
    }
    
    // Create server socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        LOGE("Failed to create socket: %s", strerror(errno));
        return false;
    }
    
    set_reuse_addr(server_fd_);
    set_nonblocking(server_fd_);
    
    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOGE("Failed to bind to port %u: %s", port_, strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    // Listen
    if (listen(server_fd_, 16) < 0) {
        LOGE("Failed to listen: %s", strerror(errno));
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    
    running_ = true;
    
    // Start accept thread
    accept_thread_ = std::thread(&WebServer::accept_loop, this);
    
    // Start worker threads
    for (int i = 0; i < 2; i++) {
        workers_.emplace_back(&WebServer::worker_loop, this);
    }
    
    LOGI("WebServer started on port %u", port_);
    return true;
}

void WebServer::stop() noexcept {
    if (!running_.load()) return;
    
    running_ = false;
    stop_accept_ = true;
    
    // Close server socket to unblock accept
    if (server_fd_ >= 0) {
        close(server_fd_);
        server_fd_ = -1;
    }
    
    // Wait for threads
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    
    // Clean up clients
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (auto* client : clients_) {
            close(client->fd);
            delete client;
        }
        clients_.clear();
    }
    
    LOGI("WebServer stopped");
}

void WebServer::accept_loop() {
    while (!stop_accept_.load() && server_fd_ >= 0) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            break;
        }
        
        set_nonblocking(client_fd);
        set_no_delay(client_fd);
        
        // Spawn client handler thread
        std::lock_guard<std::mutex> lock(clients_mutex_);
        client_threads_.emplace_back(&WebServer::handle_client, this, client_fd);
    }
}

void WebServer::handle_client(int fd) {
    WebSocketClient* client = nullptr;
    uint64_t client_id = 0;
    bool is_websocket = false;
    std::vector<uint8_t> buf(4096);
    
    while (running_.load()) {
        ssize_t n = recv(fd, buf.data(), buf.size(), 0);
        
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            break;
        }
        
        // First packet: check if HTTP upgrade
        if (!client) {
            HttpRequest req;
            if (parse_http_request({buf.begin(), buf.begin() + n}, req)) {
                if (is_websocket_upgrade(req)) {
                    // Upgrade to WebSocket
                    auto response = build_websocket_accept(req);
                    send(fd, response.data(), response.size(), 0);
                    
                    client_id = add_client(fd);
                    client = [&]() -> WebSocketClient* {
                        std::lock_guard<std::mutex> lock(clients_mutex_);
                        for (auto* c : clients_) {
                            if (c->id == client_id) return c;
                        }
                        return nullptr;
                    }();
                    
                    if (client) {
                        client->alive = true;
                        is_websocket = true;
                        LOGI("Client %lu upgraded to WebSocket", client_id);
                    }
                } else {
                    // Handle as HTTP
                    auto resp = handle_http(req);
                    auto response = build_http_response(resp);
                    send(fd, response.data(), response.size(), 0);
                    break;
                }
            }
        } else if (is_websocket) {
            // WebSocket frame parsing
            client->recv_buf.insert(client->recv_buf.end(), buf.begin(), buf.begin() + n);
            
            // Process complete frames
            while (client->recv_buf.size() >= 2) {
                WebSocketFrame frame;
                if (!parse_websocket_frame(client->recv_buf, frame)) {
                    break;
                }
                
                // Handle frame
                switch (frame.opcode) {
                    case WebSocketOpcode::CLOSE:
                        client->alive = false;
                        {
                            auto close_frame = build_websocket_frame(WebSocketOpcode::CLOSE, nullptr, 0);
                            send(fd, close_frame.data(), close_frame.size(), 0);
                        }
                        break;
                        
                    case WebSocketOpcode::PING:
                        {
                            auto pong_frame = build_websocket_frame(WebSocketOpcode::PONG, 
                                frame.payload.data(), frame.payload.size());
                            send(fd, pong_frame.data(), pong_frame.size(), 0);
                        }
                        break;
                        
                    case WebSocketOpcode::TEXT:
                        {
                            // Parse command
                            std::string msg(frame.payload.begin(), frame.payload.end());
                            LOGD("WebSocket message: %s", msg.c_str());
                            
                            // Simple JSON command parsing
                            if (delegate_ && msg.find("\"cmd\"") != std::string::npos) {
                                WebCommand cmd;
                                // Extract cmd value (simplified)
                                size_t cmd_start = msg.find("\"cmd\"");
                                if (cmd_start != std::string::npos) {
                                    size_t colon = msg.find(":", cmd_start);
                                    size_t quote1 = msg.find("\"", colon + 1);
                                    size_t quote2 = msg.find("\"", quote1 + 1);
                                    if (colon != std::string::npos && quote1 != std::string::npos && quote2 != std::string::npos) {
                                        cmd.cmd = msg.substr(quote1 + 1, quote2 - quote1 - 1);
                                        
                                        // Parse params
                                        size_t params_start = msg.find("\"params\"");
                                        if (params_start != std::string::npos) {
                                            // Extract mode param
                                            size_t mode_pos = msg.find("\"mode\"", params_start);
                                            if (mode_pos != std::string::npos) {
                                                size_t m1 = msg.find("\"", mode_pos + 6);
                                                size_t m2 = msg.find("\"", m1 + 1);
                                                if (m1 != std::string::npos && m2 != std::string::npos) {
                                                    cmd.params["mode"] = msg.substr(m1 + 1, m2 - m1 - 1);
                                                }
                                            }
                                        }
                                        
                                        delegate_->handle_command(cmd);
                                    }
                                }
                            }
                            
                            // Send acknowledgment
                            if (delegate_) {
                                std::string ack = "{\"type\":3,\"cmd\":\"ack\"}";
                                auto ack_frame = build_websocket_frame(WebSocketOpcode::TEXT, 
                                    ack.data(), ack.size());
                                send(fd, ack_frame.data(), ack_frame.size(), 0);
                            }
                        }
                        break;
                        
                    default:
                        break;
                }
                
                // Remove processed frame from buffer
                size_t frame_size = 2 + frame.payload_len + 
                    (frame.masked ? WS_MASK_SIZE : 0);
                if (frame.payload_len == 126) frame_size += 2;
                else if (frame.payload_len == 127) frame_size += 8;
                
                if (frame_size <= client->recv_buf.size()) {
                    client->recv_buf.erase(client->recv_buf.begin(), 
                        client->recv_buf.begin() + frame_size);
                } else {
                    break;
                }
            }
        }
        
        if (client && !client->alive) break;
    }
    
    if (client_id > 0) {
        remove_client(client_id);
    }
    
    close(fd);
}

bool WebServer::parse_http_request(const std::vector<uint8_t>& data, HttpRequest& req) {
    std::string text(data.begin(), data.end());
    
    // Parse request line
    size_t line_end = text.find("\r\n");
    if (line_end == std::string::npos) return false;
    
    std::string request_line = text.substr(0, line_end);
    char method_buf[256] = {0}, path_buf[256] = {0}, version_buf[16] = {0};
    sscanf(request_line.c_str(), "%255s %255s %10s", method_buf, path_buf, version_buf);
    req.method = method_buf;
    req.path = path_buf;
    req.version = version_buf;
    
    // Parse headers
    size_t pos = line_end + 2;
    while (pos < text.size()) {
        size_t next_line = text.find("\r\n", pos);
        if (next_line == std::string::npos) break;
        
        std::string line = text.substr(pos, next_line - pos);
        if (line.empty()) break;
        
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // Trim leading space
            while (value[0] == ' ') value.erase(0, 1);
            req.headers[key] = value;
        }
        
        pos = next_line + 2;
    }
    
    return true;
}

bool WebServer::is_websocket_upgrade(const HttpRequest& req) {
    auto it = req.headers.find("Upgrade");
    if (it != req.headers.end() && it->second == "websocket") {
        auto conn_it = req.headers.find("Connection");
        if (conn_it != req.headers.end() && 
            conn_it->second.find("Upgrade") != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::vector<uint8_t> WebServer::build_websocket_accept(const HttpRequest& req) {
    std::string key;
    auto it = req.headers.find("Sec-WebSocket-Key");
    if (it != req.headers.end()) {
        key = it->second;
    }
    
    std::string accept_key = sha1_base64(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    
    char response[256];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        accept_key.c_str());
    
    return std::vector<uint8_t>(response, response + strlen(response));
}

std::vector<uint8_t> WebServer::build_http_response(const HttpResponse& resp) {
    char status_line[64];
    snprintf(status_line, sizeof(status_line),
        "HTTP/1.1 %d %s\r\n",
        resp.status_code, resp.status_text.c_str());
    
    std::string response = status_line;
    for (const auto& h : resp.headers) {
        response += h.first + ": " + h.second + "\r\n";
    }
    
    char content_len[64];
    snprintf(content_len, sizeof(content_len), "Content-Length: %zu\r\n", resp.body.size());
    response += content_len;
    response += "\r\n";
    response += resp.body;
    
    return std::vector<uint8_t>(response.begin(), response.end());
}

bool WebServer::parse_websocket_frame(const std::vector<uint8_t>& data, WebSocketFrame& frame) {
    if (data.size() < 2) return false;
    
    frame.fin = (data[0] & 0x80) != 0;
    frame.opcode = static_cast<WebSocketOpcode>(data[0] & 0x0F);
    frame.masked = (data[1] & 0x80) != 0;
    
    frame.payload_len = data[1] & 0x7F;
    size_t header_len = 2;
    
    if (frame.payload_len == 126) {
        if (data.size() < 4) return false;
        frame.payload_len = (data[2] << 8) | data[3];
        header_len = 4;
    } else if (frame.payload_len == 127) {
        if (data.size() < 10) return false;
        frame.payload_len = 0;
        for (int i = 0; i < 8; i++) {
            frame.payload_len = (frame.payload_len << 8) | data[2 + i];
        }
        header_len = 10;
    }
    
    if (frame.masked) {
        if (data.size() < header_len + WS_MASK_SIZE) return false;
        memcpy(frame.mask, data.data() + header_len, WS_MASK_SIZE);
        header_len += WS_MASK_SIZE;
    }
    
    if (data.size() < header_len + frame.payload_len) return false;
    
    frame.payload.resize(frame.payload_len);
    memcpy(frame.payload.data(), data.data() + header_len, frame.payload_len);
    
    // Decode mask
    if (frame.masked) {
        for (size_t i = 0; i < frame.payload_len; i++) {
            frame.payload[i] ^= frame.mask[i % WS_MASK_SIZE];
        }
    }
    
    return true;
}

std::vector<uint8_t> WebServer::build_websocket_frame(WebSocketOpcode opcode, 
    const void* data, size_t len) {
    std::vector<uint8_t> frame;
    
    // First byte: FIN + opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));
    
    // Payload length
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((len >> (i * 8)) & 0xFF);
        }
    }
    
    // Payload
    if (data && len > 0) {
        frame.insert(frame.end(), static_cast<const uint8_t*>(data), 
            static_cast<const uint8_t*>(data) + len);
    }
    
    return frame;
}

HttpResponse WebServer::handle_http(const HttpRequest& req) {
    if (!delegate_) {
        return HttpResponse::json(500, "{\"error\":\"No delegate\"}");
    }
    
    // Route handling
    if (req.path == "/api/status" || req.path == "/status") {
        auto status = delegate_->get_status();
        return HttpResponse::json(200, status.to_json());
    }
    
    if (req.path == "/api/model" || req.path == "/model") {
        if (req.method == "GET") {
            auto weights = delegate_->get_model_weights();
            return HttpResponse::json(200, weights.to_json());
        } else if (req.method == "POST") {
            // Parse model weights from body
            // For now, return success
            return HttpResponse::json(200, "{\"success\":true}");
        }
    }
    
    if (req.path == "/api/command" || req.path == "/command") {
        if (req.method == "POST") {
            WebCommand cmd;
            // Simple parsing
            if (req.body.find("\"cmd\"") != std::string::npos) {
                // Extract command from JSON body
                size_t cmd_pos = req.body.find("\"cmd\"");
                size_t colon = req.body.find(":", cmd_pos);
                size_t quote1 = req.body.find("\"", colon + 1);
                size_t quote2 = req.body.find("\"", quote1 + 1);
                if (colon != std::string::npos && quote1 != std::string::npos && quote2 != std::string::npos) {
                    cmd.cmd = req.body.substr(quote1 + 1, quote2 - quote1 - 1);
                    
                    // Extract params
                    std::vector<std::string> param_names = {"mode", "min", "max", "preset", "model", "target_fps"};
                    for (const auto& pname : param_names) {
                        size_t pos = req.body.find("\"" + pname + "\"");
                        if (pos != std::string::npos) {
                            size_t col = req.body.find(":", pos);
                            size_t q1 = req.body.find("\"", col + 1);
                            size_t q2 = req.body.find("\"", q1 + 1);
                            if (q1 != std::string::npos && q2 != std::string::npos) {
                                cmd.params[pname] = req.body.substr(q1 + 1, q2 - q1 - 1);
                            } else {
                                // 可能是数字
                                size_t num_start = col + 1;
                                while (num_start < req.body.size() && 
                                       (req.body[num_start] == ' ' || req.body[num_start] == '\t')) num_start++;
                                size_t num_end = num_start;
                                while (num_end < req.body.size() && 
                                       (isdigit(req.body[num_end]) || req.body[num_end] == '.' || req.body[num_end] == '-')) {
                                    num_end++;
                                }
                                if (num_end > num_start) {
                                    cmd.params[pname] = req.body.substr(num_start, num_end - num_start);
                                }
                            }
                        }
                    }
                    
                    delegate_->handle_command(cmd);
                }
            }
            return HttpResponse::json(200, "{\"success\":true}");
        }
    }
    
    // Health check
    if (req.path == "/health") {
        return HttpResponse::json(200, "{\"status\":\"ok\"}");
    }
    
    return HttpResponse::not_found();
}

uint64_t WebServer::add_client(int fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto* client = new WebSocketClient();
    client->fd = fd;
    client->id = next_client_id_++;
    client->alive = true;
    clients_.push_back(client);
    LOGI("Client %lu connected (fd=%d)", client->id, fd);
    return client->id;
}

void WebServer::remove_client(uint64_t id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if ((*it)->id == id) {
            LOGI("Client %lu disconnected", id);
            close((*it)->fd);
            delete *it;
            clients_.erase(it);
            break;
        }
    }
}

void WebServer::send_to_client(WebSocketClient& client, WebSocketOpcode opcode, 
    const void* data, size_t len) {
    auto frame = build_websocket_frame(opcode, data, len);
    send(client.fd, frame.data(), frame.size(), 0);
}

void WebServer::broadcast(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto* client : clients_) {
        if (client->alive) {
            send_to_client(*client, WebSocketOpcode::TEXT, data.data(), data.size());
        }
    }
}

void WebServer::broadcast_json(MsgType type, const std::string& json) {
    // Add message type prefix
    std::string msg = "{\"type\":" + std::to_string(static_cast<int>(type)) + 
        ",\"data\":" + json + "}";
    
    std::vector<uint8_t> data(msg.begin(), msg.end());
    broadcast(data);
}

void WebServer::send_to(uint64_t client_id, const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (auto* client : clients_) {
        if (client->id == client_id && client->alive) {
            send_to_client(*client, WebSocketOpcode::TEXT, data.data(), data.size());
            break;
        }
    }
}

size_t WebServer::client_count() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(clients_mutex_));
    return clients_.size();
}

void WebServer::worker_loop() {
    while (running_.load()) {
        // Broadcast periodic status updates
        if (delegate_) {
            auto status = delegate_->get_status();
            broadcast_json(MsgType::STATUS_UPDATE, status.to_json());
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

} // namespace net
} // namespace hp
