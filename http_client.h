#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <boost/asio.hpp>
#include <json_utils.h>
#include <curl/curl.h>

enum class HttpMethod { GET, POST, PUT, PATCH, DELETE, UNKNOWN };

class TApiRequest;
class HTTPClient {
public:
    static HttpMethod ToHttpMethod(const std::string& method) {
        if (method == "GET") return HttpMethod::GET;
        if (method == "POST") return HttpMethod::POST;
        if (method == "PUT") return HttpMethod::PUT;
        if (method == "DELETE") return HttpMethod::DELETE;
        if (method == "PATCH") return HttpMethod::PATCH;
        return HttpMethod::UNKNOWN;
    }
    struct Args {
        TApiRequest* request{nullptr};
        const std::string& id;
        JSON& payload;
    };
    struct Result {
        long http_status = 0;
        std::string uri;
        std::string response_body;
        CURLcode curl_code = CURLE_OK;
        std::string error_message;
        [[nodiscard]] bool IsError() const { return 200 != http_status || CURLE_OK != curl_code; }
        const std::string& Text() { return error_message.empty() ? response_body : error_message; }
        JSON Json() { return JS::Parse(Text()); }
    };
    struct PurgeNodeResult {
        std::string endpoint;
        long status;
        bool success;
    };

    struct PurgeResult {
        std::string purgeId;
        std::vector<PurgeNodeResult> nodes;
        bool completeSuccess = false;
    };

    explicit HTTPClient(size_t thread_pool_size = 0, size_t curl_pool_size = 0);

    static HTTPClient::Result SendRequest(HttpMethod method,
                                          const std::string& endpoint,
                                          const std::vector<std::string>& path,
                                          const std::string& data,
                                          const std::vector<std::string>& headers,
                                          const std::vector<std::pair<std::string, std::string>>& query = {},
                                          bool reuse_connection = false,
                                          const std::string& socket_path = "") noexcept;


private:
    // CURL handle pool for reuse
    class CurlHandlePool {
    public:
        explicit CurlHandlePool(size_t max_size);
        ~CurlHandlePool();
        CURL* acquire();
        void release(CURL* handle);
    private:
        std::mutex mutex_;
        std::queue<CURL*> available_handles_;
        std::atomic<size_t> total_handles_{0};
        size_t max_size_;
    };

    std::unique_ptr<boost::asio::thread_pool> pool_;
    std::unique_ptr<CurlHandlePool> curl_pool_;
    static CURLSH* curl_share_;
    static std::mutex share_lock_;
    
    static void share_lock_fn(CURL* handle, curl_lock_data data, curl_lock_access access, void* userptr);
    static void share_unlock_fn(CURL* handle, curl_lock_data data, void* userptr);
    
    static void ConfigurePost(CURL* curl, const std::string& data);
    static void ConfigurePut(CURL* curl, const std::string& data);
    static void ConfigureDelete(CURL* curl, const std::string& data);
    static void ConfigureGet(CURL* curl);
    static void ConfigurePatch(CURL* curl, const std::string& data);
    static void ConfigurePurge(CURL* curl, const std::string& cacheCluster);
};
