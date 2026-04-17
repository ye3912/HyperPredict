/**
 * HyperPredict Web Server
 * Lightweight embedded HTTP + WebSocket server for WebUI communication
 * 
 * Features:
 * - HTTP REST API for status/model queries
 * - WebSocket for real-time streaming
 * - JSON message format
 * - Linux/Android compatible
 */

#pragma once
#include "core/types.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace hp {
namespace net {

constexpr uint16_t DEFAULT_PORT = 8081;
constexpr size_t MAX_WEBSOCKET_FRAME = 8192;
constexpr size_t WS_MASK_SIZE = 4;

/**
 * WebSocket opcodes
 */
enum class WebSocketOpcode : uint8_t {
    CONTINUATION = 0x0,
    TEXT = 0x1,
    BINARY = 0x2,
    CLOSE = 0x8,
    PING = 0x9,
    PONG = 0xA
};

/**
 * WebSocket frame structure
 */
struct WebSocketFrame {
    bool fin;
    bool masked;
    WebSocketOpcode opcode;
    uint64_t payload_len;
    uint8_t mask[WS_MASK_SIZE];
    std::vector<uint8_t> payload;
};

/**
 * WebSocket client connection
 */
struct WebSocketClient {
    int fd;
    uint64_t id;
    bool alive;
    std::vector<uint8_t> recv_buf;
    std::vector<uint8_t> send_buf;
    std::chrono::steady_clock::time_point last_ping;
};

/**
 * HTTP request/response
 */
struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status_code;
    std::string status_text;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    
    static HttpResponse json(int code, const std::string& json_body) {
        HttpResponse resp;
        resp.status_code = code;
        resp.status_text = code == 200 ? "OK" : (code == 400 ? "Bad Request" : "Error");
        resp.headers["Content-Type"] = "application/json";
        resp.headers["Access-Control-Allow-Origin"] = "*";
        resp.headers["Connection"] = "close";
        resp.body = json_body;
        return resp;
    }
    
    static HttpResponse not_found() {
        HttpResponse resp;
        resp.status_code = 404;
        resp.status_text = "Not Found";
        resp.headers["Content-Type"] = "text/plain";
        resp.body = "Not Found";
        return resp;
    }
};

/**
 * Message types for WebSocket communication
 */
enum class MsgType : uint8_t {
    STATUS_UPDATE = 1,
    MODEL_WEIGHTS = 2,
    COMMAND_ACK = 3,
    ERROR = 4,
    PING = 5,
    PONG = 6
};

/**
 * Status update message
 */
struct StatusUpdate {
    uint64_t timestamp;
    uint32_t fps;
    uint32_t target_fps;
    uint32_t cpu_util;
    uint32_t run_queue_len;
    uint32_t wakeups_100ms;
    uint32_t frame_interval_us;
    uint32_t touch_rate_100ms;
    int32_t thermal_margin;
    int32_t temperature;
    int32_t battery_level;
    bool is_gaming;
    std::string mode;
    uint8_t uclamp_min;
    uint8_t uclamp_max;
    
    std::string to_json() const;
};

/**
 * Model weights message
 */
struct ModelWeights {
    // Linear model
    float w_util, w_rq, w_wakeups, w_frame, w_touch, w_thermal, w_battery, bias;
    float ema_error;
    
    // Neural network (if enabled)
    bool has_nn;
    std::vector<std::vector<std::vector<float>>> nn_weights;
    std::vector<std::vector<float>> nn_biases;
    
    std::string to_json() const;
};

/**
 * Command from WebUI
 */
struct WebCommand {
    std::string cmd;
    std::unordered_map<std::string, std::string> params;
    
    bool get_string(const std::string& key, std::string& out) const;
    bool get_int(const std::string& key, int& out) const;
    bool get_float(const std::string& key, float& out) const;
};

/**
 * WebServer callbacks
 */
class WebServerDelegate {
public:
    virtual ~WebServerDelegate() = default;
    virtual StatusUpdate get_status() = 0;
    virtual ModelWeights get_model_weights() = 0;
    virtual bool set_model_weights(const ModelWeights& weights) = 0;
    virtual bool handle_command(const WebCommand& cmd) = 0;
};

/**
 * Main Web Server class
 */
class WebServer {
public:
    WebServer(uint16_t port = DEFAULT_PORT);
    ~WebServer();
    
    bool start() noexcept;
    void stop() noexcept;
    void set_delegate(WebServerDelegate* delegate) { delegate_ = delegate; }
    
    bool is_running() const { return running_.load(); }
    uint16_t port() const { return port_; }
    
    // Broadcast to all WebSocket clients
    void broadcast(const std::vector<uint8_t>& data);
    void broadcast_json(MsgType type, const std::string& json);
    
    // Send to specific client
    void send_to(uint64_t client_id, const std::vector<uint8_t>& data);
    
    // Get connected client count
    size_t client_count() const;

private:
    void worker_loop();
    void accept_loop();
    void handle_client(int fd);
    
    // HTTP handlers
    HttpResponse handle_http(const HttpRequest& req);
    bool parse_http_request(const std::vector<uint8_t>& data, HttpRequest& req);
    std::vector<uint8_t> build_http_response(const HttpResponse& resp);
    
    // WebSocket handlers
    bool is_websocket_upgrade(const HttpRequest& req);
    std::vector<uint8_t> build_websocket_accept(const HttpRequest& req);
    bool parse_websocket_frame(const std::vector<uint8_t>& data, WebSocketFrame& frame);
    std::vector<uint8_t> build_websocket_frame(WebSocketOpcode opcode, const void* data, size_t len);
    bool decode_websocket_payload(WebSocketFrame& frame, const uint8_t* masked_data);
    
    // Client management
    uint64_t add_client(int fd);
    void remove_client(uint64_t id);
    void send_to_client(WebSocketClient& client, WebSocketOpcode opcode, const void* data, size_t len);
    
    uint16_t port_;
    int server_fd_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_accept_;
    
    WebServerDelegate* delegate_;
    
    std::vector<std::thread> workers_;
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;
    
    std::mutex clients_mutex_;
    std::condition_variable clients_cv_;
    std::vector<WebSocketClient*> clients_;
    std::atomic<uint64_t> next_client_id_;
    
    // Select() fd sets
    int max_fd_;
    fd_set read_fds_;
    fd_set write_fds_;
    std::mutex fd_mutex_;
};

} // namespace net
} // namespace hp
