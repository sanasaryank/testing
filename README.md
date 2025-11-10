# HTTP Client Implementations

Production-ready synchronous single-threaded HTTP client implementations in C++17.

## Overview

This project provides two independent HTTP client implementations:
1. **libcurl-based client** (`http_client_curl.*`) - Using the popular libcurl library
2. **Boost.ASIO-based client** (`http_client_asio.*`) - Using Boost.ASIO for networking

Both implementations provide the same interface and features while being completely independent of each other.

## Features

- ✅ Synchronous, single-threaded operation
- ✅ Support for HTTP methods: GET, POST, PUT, DELETE, PATCH, HEAD
- ✅ Custom headers support
- ✅ Request body support (for POST, PUT, PATCH)
- ✅ Configurable timeouts (connection and request)
- ✅ Response structure with status code, headers, and body
- ✅ HTTPS support (SSL/TLS)
- ✅ Strong exception handling with custom exception hierarchy
- ✅ RAII principles for resource management
- ✅ Automatic redirect following (configurable)
- ✅ Response size limits (configurable)
- ✅ SSL certificate verification (configurable)
- ✅ C++17 features (std::optional, std::string_view)

## Files

- `http_common.hpp` - Common types, exceptions, and interfaces used by both implementations
- `http_client_curl.hpp` - libcurl implementation header
- `http_client_curl.cpp` - libcurl implementation
- `http_client_asio.hpp` - Boost.ASIO implementation header
- `http_client_asio.cpp` - Boost.ASIO implementation
- `example_usage.cpp` - Example usage demonstrating both implementations
- `CMakeLists.txt` - CMake build configuration

## Dependencies

### libcurl Implementation
- libcurl (with SSL support)
- C++17 compiler

### Boost.ASIO Implementation
- Boost (system component)
- OpenSSL
- C++17 compiler

## Building

### Using CMake

```bash
mkdir build
cd build
cmake ..
make
```

### Manual Compilation

**libcurl implementation:**
```bash
g++ -std=c++17 -c http_client_curl.cpp -o http_client_curl.o
g++ -std=c++17 example_usage.cpp http_client_curl.o http_client_asio.o -lcurl -lboost_system -lssl -lcrypto -lpthread -o example
```

**Boost.ASIO implementation:**
```bash
g++ -std=c++17 -c http_client_asio.cpp -o http_client_asio.o
g++ -std=c++17 example_usage.cpp http_client_curl.o http_client_asio.o -lcurl -lboost_system -lssl -lcrypto -lpthread -o example
```

## Usage Examples

### Basic GET Request

```cpp
#include "http_client_curl.hpp"
// or
#include "http_client_asio.hpp"

int main() {
    try {
        http::CurlHttpClient client;
        // or: http::AsioHttpClient client;
        
        auto response = client.get("https://api.example.com/data");
        
        std::cout << "Status: " << response.status_code << "\n";
        std::cout << "Body: " << response.body << "\n";
        
    } catch (const http::HttpException& e) {
        std::cerr << "Error: " << e.what() << "\n";
    }
    
    return 0;
}
```

### POST Request with JSON

```cpp
http::CurlHttpClient client;

http::Headers headers = {
    {"Content-Type", "application/json"},
    {"Authorization", "Bearer token123"}
};

std::string json_body = R"({"name": "John", "email": "john@example.com"})";

auto response = client.post("https://api.example.com/users", json_body, headers);
```

### Custom Configuration

```cpp
http::RequestConfig config;
config.timeout = std::chrono::seconds(30);
config.connect_timeout = std::chrono::seconds(10);
config.follow_redirects = true;
config.max_redirects = 5;
config.max_response_size = 10 * 1024 * 1024; // 10 MB
config.verify_ssl = true;

auto response = client.get("https://api.example.com/data", {}, config);
```

### Exception Handling

```cpp
try {
    auto response = client.get("https://api.example.com/data");
    
    if (response.is_success()) {
        // Process successful response
        std::cout << "Success: " << response.body << "\n";
    } else if (response.is_client_error()) {
        std::cout << "Client error (4xx): " << response.status_code << "\n";
    } else if (response.is_server_error()) {
        std::cout << "Server error (5xx): " << response.status_code << "\n";
    }
    
} catch (const http::TimeoutException& e) {
    std::cerr << "Request timeout: " << e.what() << "\n";
} catch (const http::NetworkException& e) {
    std::cerr << "Network error: " << e.what() << "\n";
} catch (const http::SslException& e) {
    std::cerr << "SSL error: " << e.what() << "\n";
} catch (const http::HttpException& e) {
    std::cerr << "HTTP error: " << e.what() << "\n";
}
```

## Exception Hierarchy

```
std::runtime_error
    └── HttpException (base class for all HTTP errors)
        ├── NetworkException (connection failures, DNS errors)
        ├── TimeoutException (connection or request timeouts)
        ├── SslException (SSL/TLS errors)
        ├── UrlException (URL parsing/validation errors)
        ├── ParseException (response parsing errors)
        ├── ConfigException (configuration errors)
        └── HttpStatusException (HTTP error responses)
```

## API Reference

### Methods

Both implementations provide the same interface:

```cpp
// Generic request method
Response request(Method method, const std::string& url, 
                const Headers& headers = {}, 
                const std::string& body = {},
                const RequestConfig& config = RequestConfig{});

// Convenience methods
Response get(const std::string& url, const Headers& headers = {}, 
            const RequestConfig& config = RequestConfig{});

Response post(const std::string& url, const std::string& body, 
             const Headers& headers = {}, 
             const RequestConfig& config = RequestConfig{});

Response put(const std::string& url, const std::string& body, 
            const Headers& headers = {}, 
            const RequestConfig& config = RequestConfig{});

Response del(const std::string& url, const Headers& headers = {}, 
            const RequestConfig& config = RequestConfig{});

Response patch(const std::string& url, const std::string& body, 
              const Headers& headers = {}, 
              const RequestConfig& config = RequestConfig{});

Response head(const std::string& url, const Headers& headers = {}, 
             const RequestConfig& config = RequestConfig{});
```

### Response Structure

```cpp
struct Response {
    int status_code;              // HTTP status code
    Headers headers;              // Response headers (map<string, string>)
    std::string body;            // Response body
    
    // Helper methods
    bool is_success() const;      // 2xx status
    bool is_client_error() const; // 4xx status
    bool is_server_error() const; // 5xx status
    std::optional<std::string> get_header(const std::string& name) const;
};
```

### Request Configuration

```cpp
struct RequestConfig {
    std::chrono::milliseconds timeout{30000};           // Total timeout
    std::chrono::milliseconds connect_timeout{10000};   // Connection timeout
    bool follow_redirects = true;                       // Follow redirects
    int max_redirects = 5;                              // Max redirect count
    size_t max_response_size = 0;                       // Max response size (0=unlimited)
    bool verify_ssl = true;                             // Verify SSL certificates
    std::string ca_bundle_path;                         // Custom CA bundle path
};
```

## Thread Safety

Both implementations are **NOT thread-safe**. Each thread should create its own instance of the HTTP client. The implementations are designed for single-threaded, synchronous use.

## Design Principles

1. **RAII**: All resources are managed using RAII principles (sockets, SSL contexts, CURL handles)
2. **Exception Safety**: Strong exception safety guarantees with detailed error messages
3. **Single Responsibility**: Each class has a focused, well-defined purpose
4. **Const Correctness**: Methods are marked const where appropriate
5. **Move Semantics**: Move constructors and move assignment operators are provided
6. **Modern C++**: Uses C++17 features like std::optional and std::string_view

## Testing

Run the example program to test both implementations:

```bash
./example_usage
```

This will run comprehensive tests including:
- Basic HTTP requests (GET, POST, PUT, DELETE, PATCH, HEAD)
- HTTPS requests
- Custom headers
- Timeout handling
- Error handling
- Redirect handling
- Invalid URL handling
- Network error handling

## License

This code is provided as-is for educational and production use.
