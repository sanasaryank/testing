#include "http_client_curl.hpp"
#include <cstring>
#include <algorithm>
#include <sstream>

namespace http {

// ============================================================================
// Global CURL initialization
// ============================================================================

CurlHttpClient::CurlGlobalInit::CurlGlobalInit() {
    CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (code != CURLE_OK) {
        throw NetworkException("Failed to initialize libcurl globally");
    }
}

CurlHttpClient::CurlGlobalInit::~CurlGlobalInit() {
    curl_global_cleanup();
}

CurlHttpClient::CurlGlobalInit CurlHttpClient::curl_global_;

// ============================================================================
// CurlHandle RAII wrapper
// ============================================================================

CurlHttpClient::CurlHandle::CurlHandle() {
    handle = curl_easy_init();
    if (!handle) {
        throw NetworkException("Failed to initialize CURL handle");
    }
}

CurlHttpClient::CurlHandle::~CurlHandle() {
    if (handle) {
        curl_easy_cleanup(handle);
        handle = nullptr;
    }
}

CurlHttpClient::CurlHandle::CurlHandle(CurlHandle&& other) noexcept
    : handle(other.handle) {
    other.handle = nullptr;
}

CurlHttpClient::CurlHandle& CurlHttpClient::CurlHandle::operator=(CurlHandle&& other) noexcept {
    if (this != &other) {
        if (handle) {
            curl_easy_cleanup(handle);
        }
        handle = other.handle;
        other.handle = nullptr;
    }
    return *this;
}

void CurlHttpClient::CurlHandle::reset() {
    if (handle) {
        curl_easy_reset(handle);
    }
}

// ============================================================================
// HeaderList RAII wrapper
// ============================================================================

CurlHttpClient::HeaderList::~HeaderList() {
    if (list) {
        curl_slist_free_all(list);
        list = nullptr;
    }
}

void CurlHttpClient::HeaderList::append(const std::string& header) {
    list = curl_slist_append(list, header.c_str());
}

// ============================================================================
// ResponseData
// ============================================================================

void CurlHttpClient::ResponseData::append_body(const char* data, size_t size) {
    if (max_size > 0 && body.size() + size > max_size) {
        throw ParseException("Response size exceeds maximum allowed size");
    }
    body.append(data, size);
}

// ============================================================================
// CURL callbacks
// ============================================================================

size_t CurlHttpClient::write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* response_data = static_cast<ResponseData*>(userdata);
    const size_t total_size = size * nmemb;
    
    try {
        response_data->append_body(ptr, total_size);
        return total_size;
    } catch (...) {
        // Returning 0 signals an error to curl
        return 0;
    }
}

size_t CurlHttpClient::header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* response_data = static_cast<ResponseData*>(userdata);
    const size_t total_size = size * nitems;
    
    std::string header(buffer, total_size);
    
    // Remove trailing \r\n
    while (!header.empty() && (header.back() == '\r' || header.back() == '\n')) {
        header.pop_back();
    }
    
    // Parse header (format: "Name: Value")
    if (!header.empty() && header.find(':') != std::string::npos) {
        size_t colon_pos = header.find(':');
        std::string name = header.substr(0, colon_pos);
        std::string value = header.substr(colon_pos + 1);
        
        // Trim leading/trailing whitespace from value
        size_t value_start = value.find_first_not_of(" \t");
        size_t value_end = value.find_last_not_of(" \t");
        
        if (value_start != std::string::npos && value_end != std::string::npos) {
            value = value.substr(value_start, value_end - value_start + 1);
        } else {
            value.clear();
        }
        
        response_data->headers[name] = value;
    }
    
    return total_size;
}

// ============================================================================
// CurlHttpClient implementation
// ============================================================================

CurlHttpClient::CurlHttpClient() = default;

CurlHttpClient::~CurlHttpClient() = default;

CurlHttpClient::CurlHttpClient(CurlHttpClient&& other) noexcept
    : curl_handle_(std::move(other.curl_handle_)) {
}

CurlHttpClient& CurlHttpClient::operator=(CurlHttpClient&& other) noexcept {
    if (this != &other) {
        curl_handle_ = std::move(other.curl_handle_);
    }
    return *this;
}

void CurlHttpClient::validate_url(const std::string& url) {
    if (url.empty()) {
        throw UrlException("URL cannot be empty");
    }
    
    // Basic URL validation
    if (url.find("http://") != 0 && url.find("https://") != 0) {
        throw UrlException("URL must start with http:// or https://");
    }
    
    // Check for minimum valid URL length (e.g., "http://a")
    if (url.length() < 10) {
        throw UrlException("URL is too short to be valid");
    }
}

std::string CurlHttpClient::extract_error_message(CURL* curl, CURLcode code) {
    std::ostringstream oss;
    oss << curl_easy_strerror(code);
    
    // Try to get more detailed error info
    char* url = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
    if (url) {
        oss << " (URL: " << url << ")";
    }
    
    return oss.str();
}

void CurlHttpClient::setup_request(
    CURL* curl,
    Method method,
    const std::string& url,
    const Headers& headers,
    const std::string& body,
    const RequestConfig& config,
    ResponseData& response_data
) {
    // Reset the handle to clean state
    curl_easy_reset(curl);
    
    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    
    // Set callbacks
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_data);
    
    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.timeout.count());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, config.connect_timeout.count());
    
    // Set redirect options
    if (config.follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, static_cast<long>(config.max_redirects));
    } else {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    }
    
    // Set SSL options
    if (config.verify_ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        if (!config.ca_bundle_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, config.ca_bundle_path.c_str());
        }
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    
    // Set max response size
    response_data.max_size = config.max_response_size;
    
    // Configure method-specific options
    switch (method) {
        case Method::GET:
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
            
        case Method::POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
            
        case Method::PUT:
            curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!body.empty()) {
                // For PUT with body, we need to use POSTFIELDS
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
            
        case Method::DELETE:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
            
        case Method::PATCH:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
            }
            break;
            
        case Method::HEAD:
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            break;
    }
}

Response CurlHttpClient::request(
    Method method,
    const std::string& url,
    const Headers& headers,
    const std::string& body,
    const RequestConfig& config
) {
    // Validate URL
    validate_url(url);
    
    CURL* curl = curl_handle_.get();
    if (!curl) {
        throw NetworkException("CURL handle is not initialized");
    }
    
    ResponseData response_data;
    
    // Setup request
    setup_request(curl, method, url, headers, body, config, response_data);
    
    // Set custom headers
    HeaderList header_list;
    if (!headers.empty()) {
        for (const auto& [key, value] : headers) {
            header_list.append(key + ": " + value);
        }
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list.get());
    }
    
    // Error buffer for detailed error messages
    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    
    // Perform the request
    CURLcode res = curl_easy_perform(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        std::string error_msg = error_buffer[0] ? error_buffer : curl_easy_strerror(res);
        
        // Categorize the error
        switch (res) {
            case CURLE_OPERATION_TIMEDOUT:
                throw TimeoutException(error_msg, TimeoutException::Type::REQUEST);
            
            case CURLE_COULDNT_CONNECT:
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_RESOLVE_PROXY:
                throw NetworkException(error_msg, res);
            
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
            case CURLE_SSL_CACERT:
            case CURLE_SSL_CACERT_BADFILE:
            case CURLE_SSL_SHUTDOWN_FAILED:
            case CURLE_SSL_ENGINE_NOTFOUND:
            case CURLE_SSL_ENGINE_SETFAILED:
            case CURLE_PEER_FAILED_VERIFICATION:
                throw SslException(error_msg, res);
            
            case CURLE_URL_MALFORMAT:
                throw UrlException(error_msg);
            
            default:
                throw NetworkException(error_msg, res);
        }
    }
    
    // Get response code
    Response response;
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    response.status_code = static_cast<int>(response_code);
    response.headers = std::move(response_data.headers);
    response.body = std::move(response_data.body);
    
    return response;
}

// Convenience methods

Response CurlHttpClient::get(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::GET, url, headers, "", config);
}

Response CurlHttpClient::post(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::POST, url, headers, body, config);
}

Response CurlHttpClient::put(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::PUT, url, headers, body, config);
}

Response CurlHttpClient::del(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::DELETE, url, headers, "", config);
}

Response CurlHttpClient::patch(const std::string& url, const std::string& body, const Headers& headers, const RequestConfig& config) {
    return request(Method::PATCH, url, headers, body, config);
}

Response CurlHttpClient::head(const std::string& url, const Headers& headers, const RequestConfig& config) {
    return request(Method::HEAD, url, headers, "", config);
}

} // namespace http
