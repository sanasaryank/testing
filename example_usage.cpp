#include "http_client_curl.hpp"
#include "http_client_asio.hpp"
#include <iostream>
#include <iomanip>

// Helper function to print response
void print_response(const std::string& client_name, const http::Response& response) {
    std::cout << "\n========== " << client_name << " Response ==========\n";
    std::cout << "Status Code: " << response.status_code << "\n";
    std::cout << "Headers:\n";
    for (const auto& [key, value] : response.headers) {
        std::cout << "  " << key << ": " << value << "\n";
    }
    std::cout << "Body Length: " << response.body.length() << " bytes\n";
    if (response.body.length() < 500) {
        std::cout << "Body:\n" << response.body << "\n";
    } else {
        std::cout << "Body (first 500 chars):\n" 
                  << response.body.substr(0, 500) << "...\n";
    }
    std::cout << "Success: " << (response.is_success() ? "Yes" : "No") << "\n";
    std::cout << "==========================================\n";
}

// Helper function to demonstrate exception handling
template<typename ClientType>
void demonstrate_client(const std::string& client_name) {
    std::cout << "\n\n########## Testing " << client_name << " ##########\n";
    
    try {
        ClientType client;
        http::RequestConfig config;
        config.timeout = std::chrono::seconds(10);
        config.connect_timeout = std::chrono::seconds(5);
        
        // NOTE: Using HTTP URLs for testing purposes to demonstrate both HTTP and HTTPS support
        // In production code, always prefer HTTPS for sensitive data
        
        // Example 1: Simple GET request
        std::cout << "\n--- Example 1: Simple GET request ---\n";
        try {
            auto response = client.get("http://httpbin.org/get", {}, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 2: GET request with custom headers
        std::cout << "\n--- Example 2: GET with custom headers ---\n";
        try {
            http::Headers headers = {
                {"User-Agent", "CustomHttpClient/1.0"},
                {"Accept", "application/json"}
            };
            auto response = client.get("http://httpbin.org/headers", headers, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 3: POST request with JSON body
        std::cout << "\n--- Example 3: POST with JSON body ---\n";
        try {
            http::Headers headers = {
                {"Content-Type", "application/json"}
            };
            std::string json_body = R"({"name": "John Doe", "email": "john@example.com"})";
            auto response = client.post("http://httpbin.org/post", json_body, headers, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 4: PUT request
        std::cout << "\n--- Example 4: PUT request ---\n";
        try {
            http::Headers headers = {
                {"Content-Type", "application/json"}
            };
            std::string json_body = R"({"field": "updated value"})";
            auto response = client.put("http://httpbin.org/put", json_body, headers, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 5: DELETE request
        std::cout << "\n--- Example 5: DELETE request ---\n";
        try {
            auto response = client.del("http://httpbin.org/delete", {}, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 6: PATCH request
        std::cout << "\n--- Example 6: PATCH request ---\n";
        try {
            http::Headers headers = {
                {"Content-Type", "application/json"}
            };
            std::string json_body = R"({"field": "patched value"})";
            auto response = client.patch("http://httpbin.org/patch", json_body, headers, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 7: HEAD request
        std::cout << "\n--- Example 7: HEAD request ---\n";
        try {
            auto response = client.head("http://httpbin.org/get", {}, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 8: HTTPS request
        std::cout << "\n--- Example 8: HTTPS request ---\n";
        try {
            auto response = client.get("https://httpbin.org/get", {}, config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 9: Timeout handling
        std::cout << "\n--- Example 9: Timeout handling ---\n";
        try {
            http::RequestConfig short_timeout_config;
            short_timeout_config.timeout = std::chrono::milliseconds(100);
            short_timeout_config.connect_timeout = std::chrono::milliseconds(50);
            
            // This should timeout (httpbin/delay/5 waits 5 seconds)
            auto response = client.get("http://httpbin.org/delay/5", {}, short_timeout_config);
            print_response(client_name, response);
        } catch (const http::TimeoutException& e) {
            std::cout << "Expected timeout caught: " << e.what() << "\n";
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 10: Invalid URL handling
        std::cout << "\n--- Example 10: Invalid URL handling ---\n";
        try {
            auto response = client.get("not-a-valid-url", {}, config);
            print_response(client_name, response);
        } catch (const http::UrlException& e) {
            std::cout << "Expected URL error caught: " << e.what() << "\n";
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 11: Network error handling
        std::cout << "\n--- Example 11: Network error handling ---\n";
        try {
            // Try to connect to a non-existent host
            auto response = client.get("http://this-domain-definitely-does-not-exist-12345.com", {}, config);
            print_response(client_name, response);
        } catch (const http::NetworkException& e) {
            std::cout << "Expected network error caught: " << e.what() << "\n";
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 12: HTTP error status
        std::cout << "\n--- Example 12: HTTP error status (404) ---\n";
        try {
            auto response = client.get("http://httpbin.org/status/404", {}, config);
            print_response(client_name, response);
            if (response.is_client_error()) {
                std::cout << "Client error detected (4xx)\n";
            }
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
        // Example 13: Redirect handling
        std::cout << "\n--- Example 13: Redirect handling ---\n";
        try {
            http::RequestConfig redirect_config;
            redirect_config.follow_redirects = true;
            redirect_config.max_redirects = 5;
            
            auto response = client.get("http://httpbin.org/redirect/2", {}, redirect_config);
            print_response(client_name, response);
        } catch (const http::HttpException& e) {
            std::cerr << "Error: " << e.what() << " (code: " << e.error_code() << ")\n";
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << "\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "HTTP Client Implementation Examples\n";
    std::cout << "========================================\n";
    
    std::string client_choice;
    if (argc > 1) {
        client_choice = argv[1];
    } else {
        std::cout << "\nChoose client to test:\n";
        std::cout << "1. libcurl implementation\n";
        std::cout << "2. Boost.ASIO implementation\n";
        std::cout << "3. Both (default)\n";
        std::cout << "Enter choice (1-3): ";
        std::getline(std::cin, client_choice);
    }
    
    if (client_choice == "1") {
        demonstrate_client<http::CurlHttpClient>("libcurl");
    } else if (client_choice == "2") {
        demonstrate_client<http::AsioHttpClient>("Boost.ASIO");
    } else {
        // Test both implementations
        demonstrate_client<http::CurlHttpClient>("libcurl");
        demonstrate_client<http::AsioHttpClient>("Boost.ASIO");
    }
    
    std::cout << "\n\n========================================\n";
    std::cout << "All examples completed!\n";
    std::cout << "========================================\n";
    
    return 0;
}
