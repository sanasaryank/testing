#include "http_client_curl.hpp"
#include "http_client_asio.hpp"
#include <iostream>
#include <cassert>

// Test basic functionality without actual network requests
void test_url_validation() {
    std::cout << "Testing URL validation...\n";
    
    // Test libcurl client
    {
        http::CurlHttpClient client;
        
        // Test invalid URL
        try {
            client.get("not-a-url");
            assert(false && "Should have thrown UrlException");
        } catch (const http::UrlException& e) {
            std::cout << "  ✓ libcurl: Invalid URL correctly rejected: " << e.what() << "\n";
        }
        
        // Test empty URL
        try {
            client.get("");
            assert(false && "Should have thrown UrlException");
        } catch (const http::UrlException& e) {
            std::cout << "  ✓ libcurl: Empty URL correctly rejected: " << e.what() << "\n";
        }
    }
    
    // Test ASIO client
    {
        http::AsioHttpClient client;
        
        // Test invalid URL
        try {
            client.get("not-a-url");
            assert(false && "Should have thrown UrlException");
        } catch (const http::UrlException& e) {
            std::cout << "  ✓ ASIO: Invalid URL correctly rejected: " << e.what() << "\n";
        }
        
        // Test empty URL
        try {
            client.get("");
            assert(false && "Should have thrown UrlException");
        } catch (const http::UrlException& e) {
            std::cout << "  ✓ ASIO: Empty URL correctly rejected: " << e.what() << "\n";
        }
    }
}

void test_response_helpers() {
    std::cout << "\nTesting Response helper methods...\n";
    
    http::Response response;
    
    // Test success status
    response.status_code = 200;
    assert(response.is_success());
    assert(!response.is_client_error());
    assert(!response.is_server_error());
    std::cout << "  ✓ 200 status correctly identified as success\n";
    
    // Test client error status
    response.status_code = 404;
    assert(!response.is_success());
    assert(response.is_client_error());
    assert(!response.is_server_error());
    std::cout << "  ✓ 404 status correctly identified as client error\n";
    
    // Test server error status
    response.status_code = 500;
    assert(!response.is_success());
    assert(!response.is_client_error());
    assert(response.is_server_error());
    std::cout << "  ✓ 500 status correctly identified as server error\n";
    
    // Test header retrieval (case-insensitive)
    response.headers["Content-Type"] = "application/json";
    response.headers["X-Custom-Header"] = "custom-value";
    
    auto content_type = response.get_header("content-type");
    assert(content_type.has_value());
    assert(content_type.value() == "application/json");
    std::cout << "  ✓ Case-insensitive header retrieval works\n";
    
    auto custom_header = response.get_header("X-CUSTOM-HEADER");
    assert(custom_header.has_value());
    assert(custom_header.value() == "custom-value");
    std::cout << "  ✓ Case-insensitive custom header retrieval works\n";
    
    auto missing_header = response.get_header("Non-Existent");
    assert(!missing_header.has_value());
    std::cout << "  ✓ Missing header correctly returns nullopt\n";
}

void test_request_config() {
    std::cout << "\nTesting RequestConfig...\n";
    
    http::RequestConfig config;
    
    // Test default values
    assert(config.timeout == std::chrono::milliseconds(30000));
    assert(config.connect_timeout == std::chrono::milliseconds(10000));
    assert(config.follow_redirects == true);
    assert(config.max_redirects == 5);
    assert(config.max_response_size == 0);
    assert(config.verify_ssl == true);
    std::cout << "  ✓ Default configuration values are correct\n";
    
    // Test custom values
    config.timeout = std::chrono::seconds(60);
    config.connect_timeout = std::chrono::seconds(15);
    config.follow_redirects = false;
    config.max_redirects = 10;
    config.max_response_size = 1024 * 1024; // 1 MB
    config.verify_ssl = false;
    config.ca_bundle_path = "/custom/path/ca-bundle.crt";
    
    assert(config.timeout == std::chrono::seconds(60));
    assert(config.connect_timeout == std::chrono::seconds(15));
    assert(config.follow_redirects == false);
    assert(config.max_redirects == 10);
    assert(config.max_response_size == 1024 * 1024);
    assert(config.verify_ssl == false);
    assert(config.ca_bundle_path == "/custom/path/ca-bundle.crt");
    std::cout << "  ✓ Custom configuration values can be set\n";
}

void test_method_to_string() {
    std::cout << "\nTesting method_to_string...\n";
    
    assert(http::method_to_string(http::Method::GET) == "GET");
    assert(http::method_to_string(http::Method::POST) == "POST");
    assert(http::method_to_string(http::Method::PUT) == "PUT");
    assert(http::method_to_string(http::Method::DELETE) == "DELETE");
    assert(http::method_to_string(http::Method::PATCH) == "PATCH");
    assert(http::method_to_string(http::Method::HEAD) == "HEAD");
    std::cout << "  ✓ All HTTP methods convert to strings correctly\n";
}

void test_exception_hierarchy() {
    std::cout << "\nTesting exception hierarchy...\n";
    
    try {
        throw http::NetworkException("Test network error", 123);
    } catch (const http::HttpException& e) {
        assert(e.error_code() == 123);
        std::cout << "  ✓ NetworkException caught as HttpException\n";
    }
    
    try {
        throw http::TimeoutException("Test timeout", http::TimeoutException::Type::CONNECTION);
    } catch (const http::HttpException& e) {
        std::cout << "  ✓ TimeoutException caught as HttpException\n";
    }
    
    try {
        throw http::SslException("Test SSL error");
    } catch (const http::HttpException& e) {
        std::cout << "  ✓ SslException caught as HttpException\n";
    }
    
    try {
        throw http::UrlException("Test URL error");
    } catch (const http::HttpException& e) {
        std::cout << "  ✓ UrlException caught as HttpException\n";
    }
    
    try {
        throw http::ParseException("Test parse error");
    } catch (const http::HttpException& e) {
        std::cout << "  ✓ ParseException caught as HttpException\n";
    }
    
    try {
        throw http::ConfigException("Test config error");
    } catch (const http::HttpException& e) {
        std::cout << "  ✓ ConfigException caught as HttpException\n";
    }
    
    try {
        throw http::HttpStatusException(404, "Not Found", "Error body");
    } catch (const http::HttpException& e) {
        const auto* status_ex = dynamic_cast<const http::HttpStatusException*>(&e);
        assert(status_ex != nullptr);
        assert(status_ex->status_code() == 404);
        assert(status_ex->response_body() == "Error body");
        std::cout << "  ✓ HttpStatusException caught with correct details\n";
    }
}

void test_client_creation() {
    std::cout << "\nTesting client creation and destruction...\n";
    
    {
        http::CurlHttpClient client;
        std::cout << "  ✓ libcurl client created successfully\n";
    }
    std::cout << "  ✓ libcurl client destroyed successfully\n";
    
    {
        http::AsioHttpClient client;
        std::cout << "  ✓ ASIO client created successfully\n";
    }
    std::cout << "  ✓ ASIO client destroyed successfully\n";
    
    // Test move semantics for CurlHttpClient
    {
        http::CurlHttpClient client1;
        http::CurlHttpClient client2(std::move(client1));
        std::cout << "  ✓ libcurl client move constructor works\n";
        
        http::CurlHttpClient client3;
        client3 = std::move(client2);
        std::cout << "  ✓ libcurl client move assignment works\n";
    }
}

int main() {
    std::cout << "========================================\n";
    std::cout << "HTTP Client Unit Tests\n";
    std::cout << "========================================\n";
    
    try {
        test_method_to_string();
        test_response_helpers();
        test_request_config();
        test_exception_hierarchy();
        test_url_validation();
        test_client_creation();
        
        std::cout << "\n========================================\n";
        std::cout << "✓ All tests passed!\n";
        std::cout << "========================================\n";
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << "\n";
        return 1;
    }
}
