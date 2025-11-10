#pragma once

#include "http_common.hpp"
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <memory>
#include <regex>

namespace http {

/**
 * Synchronous HTTP client implementation using Boost.ASIO
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
 * its own instance of AsioHttpClient.
 * 
 * Example usage:
 * @code
 *   AsioHttpClient client;
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
class AsioHttpClient {
public:
    /**
     * Constructor - initializes ASIO context
     */
    AsioHttpClient();
    
    /**
     * Destructor
     */
    ~AsioHttpClient();
    
    // Non-copyable
    AsioHttpClient(const AsioHttpClient&) = delete;
    AsioHttpClient& operator=(const AsioHttpClient&) = delete;
    
    // Moveable
    AsioHttpClient(AsioHttpClient&& other) noexcept;
    AsioHttpClient& operator=(AsioHttpClient&& other) noexcept;
    
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
     * @throws ParseException for malformed responses
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
     * Parsed URL structure
     */
    struct ParsedUrl {
        std::string protocol;
        std::string host;
        std::string port;
        std::string path;
        bool is_https = false;
    };
    
    /**
     * Parse and validate URL
     */
    ParsedUrl parse_url(const std::string& url);
    
    /**
     * Build HTTP request string
     */
    std::string build_request(
        Method method,
        const ParsedUrl& url,
        const Headers& headers,
        const std::string& body
    );
    
    /**
     * Parse HTTP response
     */
    Response parse_response(const std::string& response_data);
    
    /**
     * Perform HTTP request (non-SSL)
     */
    Response do_http_request(
        const ParsedUrl& url,
        const std::string& request_data,
        const RequestConfig& config
    );
    
    /**
     * Perform HTTPS request (SSL)
     */
    Response do_https_request(
        const ParsedUrl& url,
        const std::string& request_data,
        const RequestConfig& config
    );
    
    /**
     * Handle redirects
     */
    Response handle_redirect(
        Method method,
        const std::string& location,
        const Headers& headers,
        const std::string& body,
        const RequestConfig& config,
        int redirect_count
    );
    
    /**
     * Extract value from header line
     */
    static std::string extract_header_value(const std::string& header_line, const std::string& header_name);
    
    boost::asio::io_context io_context_;
    std::unique_ptr<boost::asio::ssl::context> ssl_context_;
};

} // namespace http
