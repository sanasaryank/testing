#include "http_client.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <thread>
#include <chrono>

void example_sync_get() {
    std::cout << "\n=== Synchronous GET Example ===" << std::endl;
    
    auto result = HTTPClient::GetSync("https://jsonplaceholder.typicode.com/posts/1");
    
    if (!result.IsError()) {
        std::cout << "Status: " << result.http_status << std::endl;
        std::cout << "Response Body: " << result.response_body << std::endl;
        
        // Parse as JSON
        try {
            auto json = nlohmann::json::parse(result.response_body);
            std::cout << "Title: " << json["title"] << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
        std::cerr << "CURL Code: " << result.curl_code << std::endl;
    }
}

void example_sync_post() {
    std::cout << "\n=== Synchronous POST Example ===" << std::endl;
    
    nlohmann::json payload = {
        {"title", "foo"},
        {"body", "bar"},
        {"userId", 1}
    };
    
    auto result = HTTPClient::PostSync(
        "https://jsonplaceholder.typicode.com/posts",
        payload
    );
    
    if (!result.IsError()) {
        std::cout << "Status: " << result.http_status << std::endl;
        std::cout << "Response: " << result.response_body << std::endl;
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
    }
}

void example_async_get() {
    std::cout << "\n=== Asynchronous GET Example ===" << std::endl;
    
    // Create client with 4 threads in the pool
    HTTPClient client(4);
    
    std::atomic<int> completed{0};
    const int total_requests = 3;
    
    // Make multiple async requests
    for (int i = 1; i <= total_requests; ++i) {
        std::string url = "https://jsonplaceholder.typicode.com/posts/" + std::to_string(i);
        
        client.Get(url, 
            [i, &completed](HTTPClient::Result result) {
                if (!result.IsError()) {
                    std::cout << "Request " << i << " completed with status: " 
                              << result.http_status << std::endl;
                    
                    try {
                        auto json = nlohmann::json::parse(result.response_body);
                        std::cout << "  Title: " << json["title"] << std::endl;
                    } catch (const std::exception& e) {
                        std::cerr << "  JSON parsing error: " << e.what() << std::endl;
                    }
                } else {
                    std::cerr << "Request " << i << " failed: " 
                              << result.error_message << std::endl;
                }
                completed++;
            },
            true  // async = true
        );
    }
    
    // Wait for all requests to complete
    while (completed < total_requests) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "All async requests completed!" << std::endl;
}

void example_with_custom_headers() {
    std::cout << "\n=== POST with Custom Headers Example ===" << std::endl;
    
    std::vector<std::string> headers = {
        "Authorization: Bearer fake-token-for-example",
        "X-Custom-Header: custom-value"
    };
    
    nlohmann::json payload = {
        {"name", "John Doe"},
        {"email", "john@example.com"}
    };
    
    auto result = HTTPClient::PostSync(
        "https://jsonplaceholder.typicode.com/users",
        payload,
        headers
    );
    
    if (!result.IsError()) {
        std::cout << "Status: " << result.http_status << std::endl;
        std::cout << "Response: " << result.response_body << std::endl;
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
    }
}

void example_send_request() {
    std::cout << "\n=== SendRequest with Path and Query Example ===" << std::endl;
    
    std::vector<std::string> path = {"posts"};
    std::vector<std::pair<std::string, std::string>> query = {
        {"userId", "1"}
    };
    
    auto result = HTTPClient::SendRequest(
        HttpMethod::GET,
        "https://jsonplaceholder.typicode.com",
        path,
        "",  // no body for GET
        {},  // no custom headers
        query,
        true  // reuse_connection
    );
    
    if (!result.IsError()) {
        std::cout << "Request URL: " << result.uri << std::endl;
        std::cout << "Status: " << result.http_status << std::endl;
        
        try {
            auto json = nlohmann::json::parse(result.response_body);
            std::cout << "Number of posts: " << json.size() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "Error: " << result.error_message << std::endl;
    }
}

int main() {
    std::cout << "HTTP Client Library - Examples\n" << std::endl;
    std::cout << "Note: These examples use JSONPlaceholder (https://jsonplaceholder.typicode.com)" << std::endl;
    std::cout << "      which is a free fake API for testing and prototyping.\n" << std::endl;
    
    try {
        // Run examples
        example_sync_get();
        example_sync_post();
        example_async_get();
        example_with_custom_headers();
        example_send_request();
        
        std::cout << "\n=== All examples completed! ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        return 1;
    }
    
    return 0;
}
