#include "http_client_asio.hpp"
#include <boost/asio/connect.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/steady_timer.hpp>
#include <sstream>
#include <algorithm>

namespace http {

// ============================================================================
// AsioHttpClient implementation
// ============================================================================

AsioHttpClient::AsioHttpClient() {
    // Initialize SSL context for HTTPS support
    ssl_context_ = std::make_unique<boost::asio::ssl::context>(
        boost::asio::ssl::context::tlsv12_client
    );
    
    // Set default SSL verification mode
    ssl_context_->set_default_verify_paths();
    ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
}

AsioHttpClient::~AsioHttpClient() = default;

AsioHttpClient::ParsedUrl AsioHttpClient::parse_url(const std::string& url) {
    // Regular expression to parse URL
    // Format: (http|https)://host[:port][/path]
    std::regex url_regex(
        R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)",
        std::regex::icase
    );
    
    std::smatch matches;
    if (!std::regex_match(url, matches, url_regex)) {
        throw UrlException("Invalid URL format: " + url);
    }
    
    ParsedUrl parsed;
    parsed.protocol = matches[1].str();
    parsed.host = matches[2].str();
    parsed.port = matches[3].str();
    parsed.path = matches[4].str();
    
    // Convert protocol to lowercase
    std::transform(parsed.protocol.begin(), parsed.protocol.end(),
                   parsed.protocol.begin(), ::tolower);
    
    parsed.is_https = (parsed.protocol == "https");
    
    // Set default port if not specified
    if (parsed.port.empty()) {
        parsed.port = parsed.is_https ? "443" : "80";
    }
    
    // Set default path if empty
    if (parsed.path.empty()) {
        parsed.path = "/";
    }
    
    return parsed;
}

std::string AsioHttpClient::build_request(
    Method method,
    const ParsedUrl& url,
    const Headers& headers,
    const std::string& body
) {
    std::ostringstream request;
    
    // Request line
    request << method_to_string(method) << " " << url.path << " HTTP/1.1\r\n";
    
    // Host header (required for HTTP/1.1)
    request << "Host: " << url.host;
    if ((url.is_https && url.port != "443") || (!url.is_https && url.port != "80")) {
        request << ":" << url.port;
    }
    request << "\r\n";
    
    // Add custom headers
    for (const auto& [key, value] : headers) {
        request << key << ": " << value << "\r\n";
    }
    
    // Add Content-Length for methods that support body
    if (!body.empty() && (method == Method::POST || method == Method::PUT || 
                          method == Method::PATCH || method == Method::DELETE)) {
        request << "Content-Length: " << body.length() << "\r\n";
    }
    
    // Connection header
    request << "Connection: close\r\n";
    
    // End of headers
    request << "\r\n";
    
    // Add body if present
    if (!body.empty()) {
        request << body;
    }
    
    return request.str();
}

Response AsioHttpClient::parse_response(const std::string& response_data) {
    Response response;
    
    // Find end of headers (empty line)
    size_t header_end = response_data.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw ParseException("Invalid HTTP response: no header/body separator found");
    }
    
    std::string header_section = response_data.substr(0, header_end);
    std::string body_section = response_data.substr(header_end + 4);
    
    // Parse status line
    std::istringstream header_stream(header_section);
    std::string status_line;
    std::getline(header_stream, status_line);
    
    // Remove trailing \r if present
    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }
    
    // Parse status line: "HTTP/1.1 200 OK"
    std::istringstream status_stream(status_line);
    std::string http_version;
    status_stream >> http_version >> response.status_code;
    
    if (response.status_code == 0) {
        throw ParseException("Invalid HTTP response: could not parse status code");
    }
    
    // Parse headers
    std::string header_line;
    while (std::getline(header_stream, header_line)) {
        // Remove trailing \r if present
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }
        
        if (header_line.empty()) {
            continue;
        }
        
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string name = header_line.substr(0, colon_pos);
            std::string value = header_line.substr(colon_pos + 1);
            
            // Trim leading/trailing whitespace from value
            size_t value_start = value.find_first_not_of(" \t");
            size_t value_end = value.find_last_not_of(" \t");
            
            if (value_start != std::string::npos && value_end != std::string::npos) {
                value = value.substr(value_start, value_end - value_start + 1);
            } else {
                value.clear();
            }
            
            response.headers[name] = value;
        }
    }
    
    // Set response body
    response.body = body_section;
    
    return response;
}

Response AsioHttpClient::do_http_request(
    const ParsedUrl& url,
    const std::string& request_data,
    const RequestConfig& config
) {
    boost::system::error_code ec;
    
    // Create TCP socket
    boost::asio::ip::tcp::socket socket(io_context_);
    
    // Resolve hostname
    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(url.host, url.port, ec);
    
    if (ec) {
        throw NetworkException("Failed to resolve hostname: " + url.host + " - " + ec.message());
    }
    
    // Set up deadline timer for connection timeout
    boost::asio::steady_timer timer(io_context_);
    timer.expires_after(config.connect_timeout);
    
    bool timeout_occurred = false;
    timer.async_wait([&socket, &timeout_occurred](const boost::system::error_code& error) {
        if (!error) {
            timeout_occurred = true;
            boost::system::error_code ignored_ec;
            socket.close(ignored_ec);
        }
    });
    
    // Connect to server
    boost::asio::connect(socket, endpoints, ec);
    timer.cancel();
    
    if (timeout_occurred) {
        throw TimeoutException("Connection timeout", TimeoutException::Type::CONNECTION);
    }
    
    if (ec) {
        throw NetworkException("Failed to connect: " + ec.message());
    }
    
    // Set up deadline timer for request timeout
    timer.expires_after(config.timeout);
    timer.async_wait([&socket, &timeout_occurred](const boost::system::error_code& error) {
        if (!error) {
            timeout_occurred = true;
            boost::system::error_code ignored_ec;
            socket.close(ignored_ec);
        }
    });
    
    // Send request
    boost::asio::write(socket, boost::asio::buffer(request_data), ec);
    
    if (ec) {
        timer.cancel();
        throw NetworkException("Failed to send request: " + ec.message());
    }
    
    // Read response
    boost::asio::streambuf response_buffer;
    
    // Read until end of headers
    boost::asio::read_until(socket, response_buffer, "\r\n\r\n", ec);
    
    if (ec && ec != boost::asio::error::eof) {
        timer.cancel();
        if (timeout_occurred) {
            throw TimeoutException("Request timeout", TimeoutException::Type::REQUEST);
        }
        throw NetworkException("Failed to read response headers: " + ec.message());
    }
    
    // Read the rest of the response body
    std::string response_data{
        boost::asio::buffers_begin(response_buffer.data()),
        boost::asio::buffers_end(response_buffer.data())
    };
    
    // Continue reading body if Content-Length is specified
    while (!ec) {
        size_t bytes_transferred = boost::asio::read(socket, response_buffer,
            boost::asio::transfer_at_least(1), ec);
        
        if (bytes_transferred > 0) {
            std::string chunk{
                boost::asio::buffers_begin(response_buffer.data()),
                boost::asio::buffers_end(response_buffer.data())
            };
            response_data = chunk;
            
            // Check max response size
            if (config.max_response_size > 0 && response_data.size() > config.max_response_size) {
                timer.cancel();
                throw ParseException("Response size exceeds maximum allowed size");
            }
        }
    }
    
    timer.cancel();
    
    if (timeout_occurred) {
        throw TimeoutException("Request timeout", TimeoutException::Type::RESPONSE);
    }
    
    // Parse and return response
    return parse_response(response_data);
}

Response AsioHttpClient::do_https_request(
    const ParsedUrl& url,
    const std::string& request_data,
    const RequestConfig& config
) {
    boost::system::error_code ec;
    
    // Update SSL context verification settings
    if (!config.verify_ssl) {
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_none);
    } else {
        ssl_context_->set_verify_mode(boost::asio::ssl::verify_peer);
        
        if (!config.ca_bundle_path.empty()) {
            ssl_context_->load_verify_file(config.ca_bundle_path, ec);
            if (ec) {
                throw SslException("Failed to load CA bundle: " + ec.message());
            }
        }
    }
    
    // Create SSL socket
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket(io_context_, *ssl_context_);
    
    // Set SNI hostname
    SSL_set_tlsext_host_name(ssl_socket.native_handle(), url.host.c_str());
    
    // Resolve hostname
    boost::asio::ip::tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(url.host, url.port, ec);
    
    if (ec) {
        throw NetworkException("Failed to resolve hostname: " + url.host + " - " + ec.message());
    }
    
    // Set up deadline timer for connection timeout
    boost::asio::steady_timer timer(io_context_);
    timer.expires_after(config.connect_timeout);
    
    bool timeout_occurred = false;
    timer.async_wait([&ssl_socket, &timeout_occurred](const boost::system::error_code& error) {
        if (!error) {
            timeout_occurred = true;
            boost::system::error_code ignored_ec;
            ssl_socket.lowest_layer().close(ignored_ec);
        }
    });
    
    // Connect to server
    boost::asio::connect(ssl_socket.lowest_layer(), endpoints, ec);
    
    if (ec) {
        timer.cancel();
        if (timeout_occurred) {
            throw TimeoutException("Connection timeout", TimeoutException::Type::CONNECTION);
        }
        throw NetworkException("Failed to connect: " + ec.message());
    }
    
    // Perform SSL handshake
    ssl_socket.handshake(boost::asio::ssl::stream_base::client, ec);
    timer.cancel();
    
    if (timeout_occurred) {
        throw TimeoutException("SSL handshake timeout", TimeoutException::Type::CONNECTION);
    }
    
    if (ec) {
        throw SslException("SSL handshake failed: " + ec.message());
    }
    
    // Set up deadline timer for request timeout
    timer.expires_after(config.timeout);
    timer.async_wait([&ssl_socket, &timeout_occurred](const boost::system::error_code& error) {
        if (!error) {
            timeout_occurred = true;
            boost::system::error_code ignored_ec;
            ssl_socket.lowest_layer().close(ignored_ec);
        }
    });
    
    // Send request
    boost::asio::write(ssl_socket, boost::asio::buffer(request_data), ec);
    
    if (ec) {
        timer.cancel();
        throw NetworkException("Failed to send request: " + ec.message());
    }
    
    // Read response
    boost::asio::streambuf response_buffer;
    
    // Read until end of headers
    boost::asio::read_until(ssl_socket, response_buffer, "\r\n\r\n", ec);
    
    if (ec && ec != boost::asio::error::eof) {
        timer.cancel();
        if (timeout_occurred) {
            throw TimeoutException("Request timeout", TimeoutException::Type::REQUEST);
        }
        throw NetworkException("Failed to read response headers: " + ec.message());
    }
    
    // Read the rest of the response body
    std::string response_data{
        boost::asio::buffers_begin(response_buffer.data()),
        boost::asio::buffers_end(response_buffer.data())
    };
    
    // Continue reading body
    while (!ec) {
        size_t bytes_transferred = boost::asio::read(ssl_socket, response_buffer,
            boost::asio::transfer_at_least(1), ec);
        
        if (bytes_transferred > 0) {
            std::string chunk{
                boost::asio::buffers_begin(response_buffer.data()),
                boost::asio::buffers_end(response_buffer.data())
            };
            response_data = chunk;
            
            // Check max response size
            if (config.max_response_size > 0 && response_data.size() > config.max_response_size) {
                timer.cancel();
                throw ParseException("Response size exceeds maximum allowed size");
            }
        }
    }
    
    timer.cancel();
    
    if (timeout_occurred) {
        throw TimeoutException("Request timeout", TimeoutException::Type::RESPONSE);
    }
    
    // Shutdown SSL connection
    ssl_socket.shutdown(ec);
    // Ignore shutdown errors as the connection is closing anyway
    
    // Parse and return response
    return parse_response(response_data);
}

std::string AsioHttpClient::extract_header_value(const std::string& header_line, const std::string& header_name) {
    std::string lower_line = header_line;
    std::string lower_name = header_name;
    
    std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    if (lower_line.find(lower_name) == 0) {
        size_t colon_pos = header_line.find(':');
        if (colon_pos != std::string::npos) {
            std::string value = header_line.substr(colon_pos + 1);
            
            // Trim whitespace
            size_t start = value.find_first_not_of(" \t");
            size_t end = value.find_last_not_of(" \t\r\n");
            
            if (start != std::string::npos && end != std::string::npos) {
                return value.substr(start, end - start + 1);
            }
        }
    }
    
    return "";
}

Response AsioHttpClient::handle_redirect(
    Method method,
    const std::string& location,
    const Headers& headers,
    const std::string& body,
    const RequestConfig& config,
    int redirect_count
) {
    if (redirect_count >= config.max_redirects) {
        throw NetworkException("Too many redirects (max: " + std::to_string(config.max_redirects) + ")");
    }
    
    // Follow redirect
    return request(method, location, headers, body, config);
}

Response AsioHttpClient::request(
    Method method,
    const std::string& url,
    const Headers& headers,
    const std::string& body,
    const RequestConfig& config
) {
    // Parse URL
    ParsedUrl parsed_url = parse_url(url);
    
    // Build request
    std::string request_data = build_request(method, parsed_url, headers, body);
    
    // Perform request based on protocol
    Response response;
    if (parsed_url.is_https) {
        response = do_https_request(parsed_url, request_data, config);
    } else {
        response = do_http_request(parsed_url, request_data, config);
    }
    
    // Handle redirects if enabled
    if (config.follow_redirects && 
        (response.status_code == 301 || response.status_code == 302 || 
         response.status_code == 303 || response.status_code == 307 || 
         response.status_code == 308)) {
        
        auto location = response.get_header("Location");
        if (location.has_value() && !location->empty()) {
            return handle_redirect(method, *location, headers, body, config, 1);
        }
    }
    
    return response;
}

// Convenience methods

Response AsioHttpClient::get(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::GET, url, headers, "", config);
}

Response AsioHttpClient::post(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::POST, url, headers, body, config);
}

Response AsioHttpClient::put(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::PUT, url, headers, body, config);
}

Response AsioHttpClient::del(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::DELETE, url, headers, "", config);
}

Response AsioHttpClient::patch(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::PATCH, url, headers, body, config);
}

Response AsioHttpClient::head(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::HEAD, url, headers, "", config);
}

} // namespace http
