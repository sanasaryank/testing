# HTTP Client Library

A high-performance, feature-rich HTTP client library for C++ built on top of libcurl with async/sync support, thread pooling, and compression handling.

## Features

- **Async and Sync Requests**: Support for both asynchronous and synchronous HTTP requests
- **Multiple HTTP Methods**: GET, POST, PUT, PATCH, DELETE, and custom PURGE method
- **Thread Pool**: Configurable thread pool for handling concurrent requests efficiently
- **Compression Support**: Automatic handling of gzip and deflate compression
- **Connection Sharing**: DNS and SSL session sharing across requests for improved performance
- **Connection Reuse**: TCP keepalive and connection pooling for reduced latency
- **JSON Support**: Built-in JSON handling with nlohmann/json
- **Security**: SSL/TLS verification with configurable certificate paths
- **Error Handling**: Comprehensive error handling with custom exceptions
- **Unix Socket Support**: Support for Unix domain sockets
- **Large Response Handling**: Efficient memory management for large responses

## Dependencies

- **libcurl**: HTTP client library (required)
- **Boost.ASIO**: For async I/O and thread pool (required)
- **zlib**: For compression support (required)
- **nlohmann/json**: For JSON parsing and serialization (required)
- **C++17**: Minimum C++ standard required

## Building

### Using CMake

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Optional: Install
sudo cmake --install .
```

### Build Options

The library is built as a static/shared library. You can link it to your project:

```cmake
find_package(http_client REQUIRED)
target_link_libraries(your_target PRIVATE http_client)
```

## Usage

### Basic GET Request (Sync)

```cpp
#include "http_client.h"
#include <iostream>

int main() {
    // Synchronous GET request
    auto result = HTTPClient::GetSync("https://api.example.com/data");
    
    if (!result.IsError()) {
        std::cout << "Response: " << result.response_body << std::endl;
        std::cout << "Status: " << result.http_status << std::endl;
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
    }
    
    return 0;
}
```

### Basic POST Request (Sync)

```cpp
#include "http_client.h"
#include <nlohmann/json.hpp>
#include <iostream>

int main() {
    nlohmann::json payload = {
        {"username", "john_doe"},
        {"email", "john@example.com"}
    };
    
    auto result = HTTPClient::PostSync(
        "https://api.example.com/users",
        payload
    );
    
    if (!result.IsError()) {
        std::cout << "Response: " << result.response_body << std::endl;
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
    }
    
    return 0;
}
```

### Async Requests with Callbacks

```cpp
#include "http_client.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    // Create client with 4 threads in the pool
    HTTPClient client(4);
    
    // Async GET request
    client.Get("https://api.example.com/data", 
        [](HTTPClient::Result result) {
            if (!result.IsError()) {
                std::cout << "GET Response: " << result.response_body << std::endl;
            } else {
                std::cerr << "GET Error: " << result.error_message << std::endl;
            }
        },
        true  // async = true
    );
    
    // Async POST request
    nlohmann::json payload = {{"key", "value"}};
    client.Post("https://api.example.com/data",
        payload,
        [](HTTPClient::Result result) {
            if (!result.IsError()) {
                std::cout << "POST Response: " << result.response_body << std::endl;
            } else {
                std::cerr << "POST Error: " << result.error_message << std::endl;
            }
        },
        true  // async = true
    );
    
    // Wait for async operations to complete
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    return 0;
}
```

### Advanced: Custom Headers and Methods

```cpp
#include "http_client.h"
#include <iostream>

int main() {
    std::vector<std::string> custom_headers = {
        "Authorization: Bearer your-token-here",
        "X-Custom-Header: custom-value"
    };
    
    nlohmann::json payload = {{"data", "example"}};
    
    auto result = HTTPClient::PostSync(
        "https://api.example.com/endpoint",
        payload,
        custom_headers
    );
    
    if (!result.IsError()) {
        std::cout << "Response: " << result.response_body << std::endl;
    }
    
    return 0;
}
```

### Using SendRequest for More Control

```cpp
#include "http_client.h"
#include <iostream>

int main() {
    std::vector<std::string> path = {"api", "v1", "users"};
    std::vector<std::pair<std::string, std::string>> query = {
        {"page", "1"},
        {"limit", "10"}
    };
    std::vector<std::string> headers = {
        "Authorization: Bearer token"
    };
    
    auto result = HTTPClient::SendRequest(
        HttpMethod::GET,
        "https://api.example.com",
        path,
        "",  // no body for GET
        headers,
        query,
        true  // reuse_connection
    );
    
    if (!result.IsError()) {
        std::cout << "URL: " << result.uri << std::endl;
        std::cout << "Status: " << result.http_status << std::endl;
        std::cout << "Body: " << result.response_body << std::endl;
    }
    
    return 0;
}
```

## Important Notes

### ConfigureGet curl Bug Workaround

The `ConfigureGet` method contains an intentional workaround for a curl bug:

```cpp
CheckCurl(curl_easy_setopt(curl, CURLOPT_POST, 1L));  // Set POST
CheckCurl(curl_easy_setopt(curl, CURLOPT_POST, 0L));  // Then unset it
```

This sequence (setting `CURLOPT_POST` to 1L then to 0L) is **intentional** and should not be removed. It works around a known issue in libcurl where certain options may not reset properly between requests when reusing curl handles.

## Error Handling

The library provides comprehensive error handling:

```cpp
auto result = HTTPClient::GetSync("https://api.example.com/data");

// Check for errors
if (result.IsError()) {
    std::cerr << "HTTP Status: " << result.http_status << std::endl;
    std::cerr << "CURL Code: " << result.curl_code << std::endl;
    std::cerr << "Error Message: " << result.error_message << std::endl;
}

// Get response as JSON
try {
    auto json = result.Json();
    // Process JSON...
} catch (const std::exception& e) {
    std::cerr << "JSON parsing error: " << e.what() << std::endl;
}
```

## Thread Safety

- The library uses CURL's share interface for thread-safe DNS and SSL session sharing
- The thread pool is thread-safe
- Each request gets its own CURL handle, ensuring thread safety
- RAII wrappers ensure proper cleanup even in error conditions

## Performance Tips

1. **Reuse HTTPClient instances** to benefit from the thread pool
2. **Enable connection reuse** in `SendRequest` for multiple requests to the same host
3. **Use async requests** when making multiple concurrent requests
4. **Configure thread pool size** based on your workload (default is 2x CPU cores)

## License

MIT License - See [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please ensure that:
- Code follows the existing style
- All changes maintain backward compatibility
- Security best practices are followed
- Memory safety is preserved (use RAII, avoid dangling pointers)
