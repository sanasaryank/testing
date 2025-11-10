#include "http_client.h"
#include <custom_exception.h>
#include <utilities.h>

void CheckCurl(CURLcode res)
{
    if (res != CURLE_OK) throw ExceptionType::ApplicationError(curl_easy_strerror(res), res);
}

CURLSH* HTTPClient::curl_share_ = [](){
    CURLSH* share = curl_share_init();
    curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    curl_share_setopt(share, CURLSHOPT_LOCKFUNC, HTTPClient::share_lock_fn);
    curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, HTTPClient::share_unlock_fn);
    return share;
}();

std::mutex HTTPClient::share_lock_;

void HTTPClient::share_lock_fn(CURL*, curl_lock_data, curl_lock_access, void*) {
    share_lock_.lock();
}

void HTTPClient::share_unlock_fn(CURL*, curl_lock_data, void*) {
    share_lock_.unlock();
}

// CURL Handle Pool Implementation
HTTPClient::CurlHandlePool::CurlHandlePool(size_t max_size) : max_size_(max_size) {}

HTTPClient::CurlHandlePool::~CurlHandlePool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!available_handles_.empty()) {
        curl_easy_cleanup(available_handles_.front());
        available_handles_.pop();
    }
}

CURL* HTTPClient::CurlHandlePool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!available_handles_.empty()) {
        CURL* handle = available_handles_.front();
        available_handles_.pop();
        curl_easy_reset(handle);
        return handle;
    }
    
    if (total_handles_ < max_size_) {
        ++total_handles_;
        return curl_easy_init();
    }
    
    // Pool exhausted, create a temporary handle
    return curl_easy_init();
}

void HTTPClient::CurlHandlePool::release(CURL* handle) {
    if (!handle) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (available_handles_.size() < max_size_) {
        available_handles_.push(handle);
    } else {
        curl_easy_cleanup(handle);
        if (total_handles_ > 0) --total_handles_;
    }
}

HTTPClient::HTTPClient(size_t thread_pool_size, size_t curl_pool_size) {
    const size_t default_thread_size = std::thread::hardware_concurrency() * 2;
    const size_t default_curl_pool = std::thread::hardware_concurrency() * 4;
    
    pool_ = std::make_unique<boost::asio::thread_pool>(
            thread_pool_size ? thread_pool_size : default_thread_size
    );
    
    curl_pool_ = std::make_unique<CurlHandlePool>(
            curl_pool_size ? curl_pool_size : default_curl_pool
    );
}

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

// Create a custom response handler that uses heap allocation for large responses
struct ResponseHandler {
    std::string response;
    std::unique_ptr<char[]> large_buffer;
    size_t large_buffer_size = 0;
    static constexpr size_t LARGE_RESPONSE_THRESHOLD = 5 * 1024 * 1024; // 5MB threshold
    static constexpr size_t BUFFER_GROWTH_SIZE = 512 * 1024; // 512KB growth increments

    void append(const char* data, size_t size) {
        if (response.size() + size > LARGE_RESPONSE_THRESHOLD) {
            if (!large_buffer) {
                large_buffer_size = response.size() + size + BUFFER_GROWTH_SIZE;
                large_buffer = std::make_unique<char[]>(large_buffer_size);
                std::copy(response.begin(), response.end(), large_buffer.get());
            } else if (large_buffer_size < response.size() + size) {
                large_buffer_size = response.size() + size + BUFFER_GROWTH_SIZE;
                auto new_buffer = std::make_unique<char[]>(large_buffer_size);
                std::copy(large_buffer.get(), large_buffer.get() + response.size(), new_buffer.get());
                large_buffer = std::move(new_buffer);
            }
            std::copy(data, data + size, large_buffer.get() + response.size());
        } else {
            response.append(data, size);
        }
    }

    [[nodiscard]] std::string get_response() const {
        if (large_buffer) {
            return {large_buffer.get(), response.size()};
        }
        return response;
    }
};

static size_t WriteCallback(char* contents, size_t size, size_t nmemb, void* response)
{
    auto* handler = static_cast<ResponseHandler*>(response);
    const size_t total = size * nmemb;

    try {
        handler->append(contents, total);
        return total;
    } catch (...) {
        return 0;
    }                             // must return exactly what we consumed
}

static size_t DiscardHeader(char* ptr, size_t size, size_t nmemb, void*) {
    return size * nmemb; // MUST return exactly what was provided
}

static size_t ReadCallback(char* buffer, size_t size, size_t nitems, std::string* data) {
    const size_t copy_size = std::min(size * nitems, data->size());
    std::memcpy(buffer, data->data(), copy_size);
    data->erase(0, copy_size);
    return copy_size;
}

void HTTPClient::Get(const std::string& url,
                     const std::function<void(Result)>& callback,
                     bool async) {
    auto task = [url, callback]() {
        static CurlGlobal global_init;

        CURL* curl = curl_easy_init();
        if(!curl) {
            callback({0, url, "", CURLE_FAILED_INIT});
            return;
        }

        ResponseHandler response_handler;
        long http_status = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_handler);

        // Security settings
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

        // Performance settings for high-load
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);
        curl_easy_setopt(curl, CURLOPT_SHARE, curl_share_);
        curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L); // 5 minutes DNS cache
        curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L); // Allow connection reuse
        
        // HTTP/2 support for multiplexing
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
        
        // Optimized timeouts for high-load
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L); // 30 seconds
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10 seconds
        
        // TCP optimizations
        curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L); // Disable Nagle's algorithm
        curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L); // Enable TCP Fast Open

        CURLcode res = curl_easy_perform(curl);

        if(res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        }

        curl_easy_cleanup(curl);

        callback({http_status, url, response_handler.get_response(), res, ""});
    };

    if(async) {
        boost::asio::post(*pool_, task);
    } else {
        task();
    }
}

void HTTPClient::Post(const std::string& url, const nlohmann::json& body,
                      const std::function<void(Result)>& callback,
                      bool async,
                      const std::vector<std::string>& headers)
{
    auto task = [url, body, headers, callback]() {
        static CurlGlobal global_init;
        CURL* curl = curl_easy_init();
        if(!curl) {
            callback({0, url, "", CURLE_FAILED_INIT, ""});
            return;
        }

        ResponseHandler response_handler;
        long http_status = 0;
        CURLcode res = CURLE_OK;
        struct curl_slist* header_list = nullptr;
        std::unique_ptr<struct curl_slist, decltype(&curl_slist_free_all)>
                headers_guard(header_list, curl_slist_free_all);

        try {
            // Set common options
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_handler);

            // Security settings
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

            // POST specific configuration
            std::string post_data("{}");
            try {
                if(nullptr != body) post_data = body.dump();
            }
            catch (...) {
                throw ExceptionType::ApplicationError("Json parsing error!");
            }
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.size());

            // Add headers
            header_list = curl_slist_append(header_list, "Content-Type: application/json");
            for(const auto& h : headers) {
                header_list = curl_slist_append(header_list, h.c_str());
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);

            // Performance settings for high-load
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);
            curl_easy_setopt(curl, CURLOPT_SHARE, curl_share_);
            curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L); // 5 minutes DNS cache
            curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L); // Allow connection reuse
            
            // HTTP/2 support for multiplexing
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
            
            // Optimized timeouts for high-load
            curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L); // 30 seconds
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L); // 10 seconds
            
            // TCP optimizations
            curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L); // Disable Nagle's algorithm
            curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L); // Enable TCP Fast Open

            // Execute request
            res = curl_easy_perform(curl);

            if(res == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
            }

            callback({http_status, url, response_handler.get_response(), res, ""});

        } catch(const std::exception& e) {
            callback({0, url, "", CURLE_OK, e.what()});
        } catch(...) {
            callback({0, url, "", CURLE_OK, "Unknown exception"});
        }

        curl_easy_cleanup(curl);
    };

    if(async) {
        boost::asio::post(*pool_, task);
    } else {
        task();
    }
}
bool IsSecure(const std::string& endpoint)
{
    return endpoint.find("https:")!= std::string::npos;
}

static bool gunzip(const std::string& in, std::string& out) {
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    if (inflateInit2(&zs, 15 + 16) != Z_OK) return false; // 15=MAX_WBITS, +16=gzip

    char buf[64*1024];
    int rc;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&zs); return false; }
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (rc != Z_STREAM_END);

    inflateEnd(&zs);
    return true;
}

static bool inflate(const std::string& in, std::string& out) {
    z_stream zs{};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    // First try zlib format (with header)
    if (inflateInit(&zs) == Z_OK) {
        char buf[64*1024];
        int rc;
        bool success = true;

        do {
            zs.next_out = reinterpret_cast<Bytef*>(buf);
            zs.avail_out = sizeof(buf);
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) {
                success = false;
                break;
            }
            out.append(buf, sizeof(buf) - zs.avail_out);
        } while (rc != Z_STREAM_END);

        inflateEnd(&zs);

        if (success) {
            return true;
        }

        // If zlib format failed, clear output and try raw deflate
        out.clear();
    }

    // Try raw deflate format
    zs = {};
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    if (inflateInit2(&zs, -15) != Z_OK) return false; // Negative window bits for raw deflate

    char buf[64*1024];
    int rc;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (rc != Z_STREAM_END);

    inflateEnd(&zs);
    return true;
}

//HTTPClient::Result HTTPClient::SendRequest(HttpMethod method,
//                                           const std::string& endpoint,
//                                           const std::vector<std::string>& path,
//                                           const std::string& data,
//                                           const std::vector<std::string>& headers,
//                                           const std::vector<std::pair<std::string, std::string>>& query,
//                                           bool reuse_connection,
//                                           const std::string& socket_path) noexcept
//{
//    Result result;
//    CURL* curl = curl_easy_init();
//    if (!curl) {
//        result.curl_code = CURLE_FAILED_INIT;
//        result.error_message = "CURL initialization failed";
//        result.uri = endpoint;
//        return result;
//    }
//    if(!socket_path.empty()) curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());
//
//    // RAII wrappers for automatic cleanup
//    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_guard(curl, curl_easy_cleanup);
//    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(nullptr, curl_slist_free_all);
//
//    try {
//        // Build and validate URL
//        const std::string url = Utilities::BuildPath(endpoint, path, query);
//        if (url.empty()) {
//            throw std::invalid_argument("Invalid URL constructed");
//        }
//        result.uri = url;
//
//        // Common curl setup
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_URL, url.c_str()));
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback));
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, DiscardHeader));
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_HEADERDATA, nullptr));
//        ResponseHandler response_handler;
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_handler));
//
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""));
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 0L)); // auto-decode
//
//
//        // Security settings
//        if(IsSecure(endpoint)) {
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L));
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L));
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt"));
//        }
//
//        // Connection reuse
//        if(reuse_connection) {
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L));
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L));
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L));
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L));
//        }
//
//        // Set headers
//        if (!headers.empty()) {
//            struct curl_slist* list = nullptr;
//            for (const auto& h : headers) {
//                list = curl_slist_append(list, h.c_str());
//            }
//            header_list.reset(list);
//            CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list));
//        }
//
//        // Method-specific configuration
//        switch (method) {
//            case HttpMethod::POST:
//                ConfigurePost(curl, data);
//                break;
//            case HttpMethod::PUT:
//                ConfigurePut(curl, data);
//                break;
//            case HttpMethod::DELETE:
//                ConfigureDelete(curl, data);
//                break;
//            case HttpMethod::PATCH:
//                ConfigurePatch(curl, data);
//                break;
//            default:
//                ConfigureGet(curl);
//        }
//        // Timeouts
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 10000L));
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L));
//
//        char errbuf[CURL_ERROR_SIZE] = {0};
//        CheckCurl(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf));
//
//        // Execute request
//        result.curl_code = curl_easy_perform(curl);
//        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_status);
//        result.response_body = response_handler.get_response();
//        if (result.curl_code != CURLE_OK) {
//            result.error_message = errbuf[0] ? errbuf : curl_easy_strerror(result.curl_code);
//        }
//        else if(200 == result.http_status) {
//            std::string decoded;
//            if(gunzip(result.response_body, decoded))
//                result.response_body = decoded;
//        }
//        // Get HTTP status code
//
//    } catch (const CurlException& e) {
//        result.curl_code = e.getErrorCode();
//        result.error_message = e.what();
//    } catch (const std::exception& e) {
//        result.error_message = e.what();
//    } catch (...) {
//        result.error_message = "Unknown exception";
//    }
//
//    return result;
//}
HTTPClient::Result HTTPClient::SendRequest(HttpMethod method,
                                           const std::string& endpoint,
                                           const std::vector<std::string>& path,
                                           const std::string& data,
                                           const std::vector<std::string>& headers,
                                           const std::vector<std::pair<std::string, std::string>>& query,
                                           bool reuse_connection,
                                           const std::string& socket_path) noexcept
{
    Result result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.curl_code = CURLE_FAILED_INIT;
        result.error_message = "CURL initialization failed";
        result.uri = endpoint;
        return result;
    }
    if(!socket_path.empty()) curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, socket_path.c_str());

    // RAII wrappers for automatic cleanup
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl_guard(curl, curl_easy_cleanup);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(nullptr, curl_slist_free_all);

    // Structure to capture response headers
    struct HeaderCapture {
        std::string content_encoding;

        static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
            auto capture = static_cast<HeaderCapture*>(userdata);
            const std::string header(buffer, nitems * size);

            // Check for Content-Encoding header (case-insensitive)
            const std::string header_lower = [&header]() {
                std::string lower = header;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                return lower;
            }();

            if (header_lower.find("content-encoding:") == 0) {
                // Extract the value part of the header
                std::string value = header.substr(17); // Skip "Content-Encoding:"

                // Remove any leading/trailing whitespace
                size_t start = value.find_first_not_of(" \t\r\n");
                size_t end = value.find_last_not_of(" \t\r\n");
                if (start != std::string::npos && end != std::string::npos) {
                    capture->content_encoding = value.substr(start, end - start + 1);
                } else {
                    capture->content_encoding.clear();
                }

                // Convert to lowercase for easier comparison
                std::transform(capture->content_encoding.begin(),
                               capture->content_encoding.end(),
                               capture->content_encoding.begin(),
                               ::tolower);
            }
            return nitems * size;
        }
    };

    HeaderCapture header_capture;

    try {
        // Build and validate URL
        const std::string url = Utilities::BuildPath(endpoint, path, query);
        if (url.empty()) {
            throw ExceptionType::ApplicationError("Invalid URL constructed");
        }
        result.uri = url;

        // Common curl setup
        CheckCurl(curl_easy_setopt(curl, CURLOPT_URL, url.c_str()));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCapture::HeaderCallback));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_capture));
        ResponseHandler response_handler;
        CheckCurl(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_handler));

        CheckCurl(curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, ""));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTP_CONTENT_DECODING, 0L)); // auto-decode

        // Security settings
        if(IsSecure(endpoint)) {
            CheckCurl(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L));
            CheckCurl(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L));
            CheckCurl(curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt"));
        }

        // Connection reuse and performance optimizations
        if(reuse_connection) {
            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L));
            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L));
            CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L));
            CheckCurl(curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 0L));
        }
        
        // Performance optimizations for high-load scenarios
        CheckCurl(curl_easy_setopt(curl, CURLOPT_DNS_CACHE_TIMEOUT, 300L)); // 5 minutes
        CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L)); // Disable Nagle's algorithm
        CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L)); // Enable TCP Fast Open
        CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS)); // HTTP/2
        CheckCurl(curl_easy_setopt(curl, CURLOPT_SHARE, curl_share_)); // Share DNS and SSL sessions

        // Set headers
        if (!headers.empty()) {
            struct curl_slist* list = nullptr;
            for (const auto& h : headers) {
                list = curl_slist_append(list, h.c_str());
            }
            header_list.reset(list);
            CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list));
        }

        // Method-specific configuration
        switch (method) {
            case HttpMethod::POST:
                ConfigurePost(curl, data);
                break;
            case HttpMethod::PUT:
                ConfigurePut(curl, data);
                break;
            case HttpMethod::DELETE:
                ConfigureDelete(curl, data);
                break;
            case HttpMethod::PATCH:
                ConfigurePatch(curl, data);
                break;
            default:
                ConfigureGet(curl);
        }
        // Optimized timeouts for high-load scenarios
        CheckCurl(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L)); // 30 seconds total
        CheckCurl(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L)); // 10 seconds connect

        char errbuf[CURL_ERROR_SIZE] = {0};
        CheckCurl(curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf));

        // Execute request
        result.curl_code = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_status);
        result.response_body = response_handler.get_response();

        if (result.curl_code != CURLE_OK) {
            result.error_message = errbuf[0] ? errbuf : curl_easy_strerror(result.curl_code);
        }
        else {
            // Check if response is compressed
            const bool is_gzip_encoded = (header_capture.content_encoding.find("gzip") != std::string::npos);
            const bool is_deflate_encoded = (header_capture.content_encoding.find("deflate") != std::string::npos);

            if (is_gzip_encoded || is_deflate_encoded) {
                std::string decoded;
                bool decompress_success = false;

                if (is_gzip_encoded) {
                    decompress_success = gunzip(result.response_body, decoded);
                } else if (is_deflate_encoded) {
                    decompress_success = inflate(result.response_body, decoded);
                }

                if (decompress_success) {
                    result.response_body = decoded;
                }
                // Note: if decompression fails, we leave the body compressed
            }
        }

    } catch (const TrioWebException& e) {
        result.curl_code = (CURLcode)e.Code();
        result.error_message = e.what();
    } catch (const std::exception& e) {
        result.error_message = e.what();
    } catch (...) {
        result.error_message = "Unknown exception";
    }

    return result;
}

// Helper methods
void HTTPClient::ConfigurePost(CURL* curl, const std::string& data) {
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POST, 1L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size()));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data()));
}

void HTTPClient::ConfigurePut(CURL* curl, const std::string& data) {
    CheckCurl(curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(data.size())));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_READFUNCTION, ReadCallback));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_READDATA, &data));
}

void HTTPClient::ConfigureDelete(CURL* curl, const std::string& data) {
    CheckCurl(curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE"));
    if (!data.empty()) {
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size()));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data()));
    }
}

void HTTPClient::ConfigureGet(CURL* curl) {
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POST, 1L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, nullptr));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_UPLOAD, 0L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POST, 0L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_NOBODY, 0L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L));
}

void HTTPClient::ConfigurePatch(CURL* curl, const std::string& data) {
    // Set PATCH method with proper semantics
    CheckCurl(curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH"));

    if (!data.empty()) {
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, data.size()));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.data()));
    }

        // For PATCH with no data, explicitly set empty body
    else {
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L));
        CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, ""));
    }
}

void HTTPClient::ConfigurePurge(CURL* curl, const std::string& cacheCluster) {
    // Set base PURGE method configuration
    CheckCurl(curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PURGE"));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_POSTFIELDS, nullptr));

    // TCP optimizations for distributed systems
    CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_DNS_SHUFFLE_ADDRESSES, 1L));

    // Distributed cache headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Cache-Purge: distributed");
    headers = curl_slist_append(headers, ("X-Cache-Cluster: " + cacheCluster).c_str());

    // Add unique purge ID for cluster tracking
    const std::string purgeId = "purge-" + Utilities::generate_uuid();
    headers = curl_slist_append(headers, ("X-Purge-ID: " + purgeId).c_str());

    // Add current node timestamp
    headers = curl_slist_append(headers, std::string("X-Purge-Timestamp: " + Utilities::getCurrTimeStrWithMilli()).c_str());

    CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers));

    // Timeout settings optimized for cluster comms
    CheckCurl(curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L));  // 5 seconds
    CheckCurl(curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 2000L));

    // Enable multiplexing for HTTP/2 clusters
    CheckCurl(curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS));
    CheckCurl(curl_easy_setopt(curl, CURLOPT_PIPEWAIT, 1L));
}
