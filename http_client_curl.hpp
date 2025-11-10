#pragma once

#include "http_common.hpp"
#include <curl/curl.h>
#include <memory>

namespace http {

/**
 * Synchronous HTTP client implementation using libcurl
 * 
 * This is a production-ready, single-threaded HTTP client that provides:
 * - Synchronous, blocking operations
 * - Support for GET, POST, PUT, DELETE, PATCH, HEAD methods
 * - Custom headers support
 * - Request body support
 * - Timeout configuration
 * - HTTPS/SSL support
 * - Strong exception handling
 * - RAII resource management
 * 
 * Thread safety: This class is NOT thread-safe. Each thread should use
 * its own instance of CurlHttpClient.
 * 
 * Example usage:
 * @code
 *   CurlHttpClient client;
 *   RequestConfig config;
 *   config.timeout = std::chrono::seconds(10);
 *   
 *   Response response = client.request(
 *       Method::GET,
 *       "https://api.example.com/data",
 *       {},      // headers
 *       "",      // body
 *       config
 *   );
 * @endcode
 */
class CurlHttpClient {
public:
    /**
     * Constructor - initializes libcurl for this instance
     * @throws NetworkException if curl initialization fails
     */
    CurlHttpClient();
    
    /**
     * Destructor - cleans up curl resources
     */
    ~CurlHttpClient();
    
    // Non-copyable
    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;
    
    // Moveable
    CurlHttpClient(CurlHttpClient&& other) noexcept;
    CurlHttpClient& operator=(CurlHttpClient&& other) noexcept;
    
    /**
     * Perform an HTTP request
     * 
     * @param method HTTP method to use
     * @param url Full URL to request
     * @param headers Custom headers to send
     * @param body Request body (used for POST, PUT, PATCH)
     * @param config Request configuration
     * @return Response object containing status, headers, and body
     * 
     * @throws NetworkException for network-related errors
     * @throws TimeoutException for timeout errors
     * @throws SslException for SSL/TLS errors
     * @throws UrlException for invalid URLs
     * @throws HttpStatusException for HTTP error responses (if throw_on_error is enabled)
     */
    Response request(
        Method method,
        const std::string& url,
        const Headers& headers = {},
        const std::string& body = {},
        const RequestConfig& config = RequestConfig{}
    );
    
    /**
     * Convenience methods for common HTTP operations
     */
    Response get(const std::string& url, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    Response post(const std::string& url, const std::string& body, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    Response put(const std::string& url, const std::string& body, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    Response del(const std::string& url, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    Response patch(const std::string& url, const std::string& body, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    Response head(const std::string& url, const Headers& headers = {}, const RequestConfig& config = RequestConfig{});
    
private:
    /**
     * RAII wrapper for CURL handle
     */
    struct CurlHandle {
        CURL* handle = nullptr;
        
        CurlHandle();
        ~CurlHandle();
        
        CurlHandle(const CurlHandle&) = delete;
        CurlHandle& operator=(const CurlHandle&) = delete;
        
        CurlHandle(CurlHandle&& other) noexcept;
        CurlHandle& operator=(CurlHandle&& other) noexcept;
        
        CURL* get() const noexcept { return handle; }
        void reset();
    };
    
    /**
     * RAII wrapper for curl_slist (header list)
     */
    struct HeaderList {
        curl_slist* list = nullptr;
        
        ~HeaderList();
        
        void append(const std::string& header);
        curl_slist* get() const noexcept { return list; }
    };
    
    /**
     * Structure to hold response data during curl callback
     */
    struct ResponseData {
        std::string body;
        Headers headers;
        size_t max_size = 0;
        
        void append_body(const char* data, size_t size);
    };
    
    // CURL callbacks
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);
    static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata);
    
    // Helper methods
    void setup_request(CURL* curl, Method method, const std::string& url,
                      const Headers& headers, const std::string& body,
                      const RequestConfig& config, ResponseData& response_data);
    void validate_url(const std::string& url);
    std::string extract_error_message(CURL* curl, CURLcode code);
    
    CurlHandle curl_handle_;
    
    // Global curl initialization (static)
    struct CurlGlobalInit {
        CurlGlobalInit();
        ~CurlGlobalInit();
    };
    static CurlGlobalInit curl_global_;
};

} // namespace http
