#pragma once

#include <string>
#include <map>
#include <vector>
#include <stdexcept>
#include <optional>
#include <chrono>
#include <string_view>
#include <algorithm>

namespace http {

// HTTP method enumeration
enum class Method {
    GET,
    POST,
    PUT,
    DELETE,
    PATCH,
    HEAD
};

// Convert method enum to string
inline std::string_view method_to_string(Method method) {
    switch (method) {
        case Method::GET: return "GET";
        case Method::POST: return "POST";
        case Method::PUT: return "PUT";
        case Method::DELETE: return "DELETE";
        case Method::PATCH: return "PATCH";
        case Method::HEAD: return "HEAD";
        default: return "UNKNOWN";
    }
}

// HTTP headers type
using Headers = std::map<std::string, std::string>;

// HTTP response structure
struct Response {
    int status_code = 0;
    Headers headers;
    std::string body;
    
    // Helper to check if response is successful (2xx)
    [[nodiscard]] bool is_success() const noexcept {
        return status_code >= 200 && status_code < 300;
    }
    
    // Helper to check if response is client error (4xx)
    [[nodiscard]] bool is_client_error() const noexcept {
        return status_code >= 400 && status_code < 500;
    }
    
    // Helper to check if response is server error (5xx)
    [[nodiscard]] bool is_server_error() const noexcept {
        return status_code >= 500 && status_code < 600;
    }
    
    // Helper to get specific header (case-insensitive)
    [[nodiscard]] std::optional<std::string> get_header(const std::string& name) const;
};

// HTTP request configuration
struct RequestConfig {
    // Timeout for the entire request
    std::chrono::milliseconds timeout{30000};
    
    // Connection timeout
    std::chrono::milliseconds connect_timeout{10000};
    
    // Follow redirects
    bool follow_redirects = true;
    
    // Maximum number of redirects to follow
    int max_redirects = 5;
    
    // Maximum response size in bytes (0 = unlimited)
    size_t max_response_size = 0;
    
    // Verify SSL certificate
    bool verify_ssl = true;
    
    // Path to CA certificate bundle (empty = use system default)
    std::string ca_bundle_path;
};

// ============================================================================
// Exception Hierarchy
// ============================================================================

// Base exception class for all HTTP client errors
class HttpException : public std::runtime_error {
public:
    explicit HttpException(const std::string& message)
        : std::runtime_error(message) {}
    
    explicit HttpException(const std::string& message, int code)
        : std::runtime_error(message), error_code_(code) {}
    
    [[nodiscard]] int error_code() const noexcept { return error_code_; }
    
protected:
    int error_code_ = 0;
};

// Network-related errors (connection failures, DNS errors, etc.)
class NetworkException : public HttpException {
public:
    explicit NetworkException(const std::string& message)
        : HttpException("Network error: " + message) {}
    
    NetworkException(const std::string& message, int code)
        : HttpException("Network error: " + message, code) {}
};

// Timeout errors
class TimeoutException : public HttpException {
public:
    explicit TimeoutException(const std::string& message)
        : HttpException("Timeout error: " + message) {}
    
    enum class Type {
        CONNECTION,
        REQUEST,
        RESPONSE
    };
    
    TimeoutException(const std::string& message, Type type)
        : HttpException("Timeout error: " + message), timeout_type_(type) {}
    
    [[nodiscard]] Type timeout_type() const noexcept { return timeout_type_; }
    
private:
    Type timeout_type_ = Type::REQUEST;
};

// HTTP protocol errors (4xx, 5xx responses)
class HttpStatusException : public HttpException {
public:
    HttpStatusException(int status_code, const std::string& message)
        : HttpException("HTTP " + std::to_string(status_code) + ": " + message, status_code),
          status_code_(status_code) {}
    
    HttpStatusException(int status_code, const std::string& message, const std::string& body)
        : HttpException("HTTP " + std::to_string(status_code) + ": " + message, status_code),
          status_code_(status_code),
          response_body_(body) {}
    
    [[nodiscard]] int status_code() const noexcept { return status_code_; }
    [[nodiscard]] const std::string& response_body() const noexcept { return response_body_; }
    
private:
    int status_code_;
    std::string response_body_;
};

// SSL/TLS errors
class SslException : public HttpException {
public:
    explicit SslException(const std::string& message)
        : HttpException("SSL/TLS error: " + message) {}
    
    SslException(const std::string& message, int code)
        : HttpException("SSL/TLS error: " + message, code) {}
};

// URL parsing and validation errors
class UrlException : public HttpException {
public:
    explicit UrlException(const std::string& message)
        : HttpException("URL error: " + message) {}
};

// Request/response parsing errors
class ParseException : public HttpException {
public:
    explicit ParseException(const std::string& message)
        : HttpException("Parse error: " + message) {}
};

// Configuration errors
class ConfigException : public HttpException {
public:
    explicit ConfigException(const std::string& message)
        : HttpException("Configuration error: " + message) {}
};

// Response implementation
inline std::optional<std::string> Response::get_header(const std::string& name) const {
    // Convert to lowercase for case-insensitive comparison
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    for (const auto& [key, value] : headers) {
        std::string lower_key = key;
        std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        
        if (lower_key == lower_name) {
            return value;
        }
    }
    
    return std::nullopt;
}

} // namespace http
