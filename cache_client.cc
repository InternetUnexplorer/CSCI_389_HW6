#include "cache.hh"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <string>

namespace beast = boost::beast; // from <boost/beast.hpp>
namespace http = beast::http;   // from <boost/beast/http.hpp>
namespace net = boost::asio;    // from <boost/asio.hpp>
using tcp = net::ip::tcp;       // from <boost/asio/ip/tcp.hpp>

/// Regular expression for parsing JSON responses
const std::regex KEY_VALUE_JSON_RE{"\\{\\s*\"key\"\\s*:\\s*\"([A-Za-z0-9\\._-]+"
                                   ")\"\\s*,\\s*\"value\"\\s*:\\s*\"([A-"
                                   "Za-z0-9\\._-]+)\"\\}\\s*"};

class Cache::Impl {
  private:
    std::string address;
    // I/O context; must persist through runtime
    net::io_context context;
    // Mutable for use in const methods
    mutable beast::tcp_stream stream;
    mutable beast::flat_buffer buffer;
    // Last value which was returned by `get()`
    mutable std::string last_value;

  public:
    Impl(const std::string &address, const std::string &port)
    : address{address}, stream{context} {
        tcp::resolver resolver{context};
        auto const results = resolver.resolve(address, port);
        stream.connect(results);
    }

    ~Impl() {
        beast::error_code error;
        stream.socket().shutdown(tcp::socket::shutdown_both, error);
    }

    // Send an HTTP request with an empty body and the specified target
    void send_request(const http::verb method,
                      const std::string &target) const {
        // Create a request with an empty body
        http::request<http::empty_body> request;
        // Set the fields
        request.method(method);
        request.version(11);
        request.set(http::field::host, address);
        request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        request.target(target);
        // Send the request
        http::write(stream, request);
    }

    void set(key_type &key, val_type val, size_type size) {
        // Send a PUT request
        send_request(http::verb::put,
                     "/" + key + "/" + std::string(val, size - 1));
        // Read the response
        http::response<http::string_body> response;
        http::read(stream, buffer, response);
        // Must be 200 OK
        if (response.result() != http::status::ok) {
            throw std::runtime_error{"server returned invalid status"};
        }
    }

    val_type get(key_type &key, size_type &val_size) const {
        // Send a GET request
        send_request(http::verb::get, "/" + key);
        // Read the response
        http::response<http::string_body> response;
        http::read(stream, buffer, response);
        // Check whether the value was found
        if (response.result() != http::status::ok) {
            return nullptr;
        }
        // Parse the response
        std::smatch match;
        if (!std::regex_search(response.body(), match, KEY_VALUE_JSON_RE)) {
            throw std::runtime_error{"unable to parse response"};
        }
        // Get the value string from the match
        last_value = match[2];
        // Update `val_size`
        val_size = last_value.size() + 1;
        // Return a pointer to the value (must be copied by the caller before
        // `get()` is called again)
        return last_value.c_str();
    }

    bool del(key_type &key) {
        // Send a DELETE request
        send_request(http::verb::delete_, "/" + key);
        // Read the response
        http::response<http::dynamic_body> response;
        http::read(stream, buffer, response);
        // Return true if 200 OK, otherwise false
        return response.result() == http::status::ok;
    }

    size_type space_used() const {
        // Send a HEAD request
        send_request(http::verb::head, "/");
        // Read the response
        http::response<http::empty_body> response;
        http::read(stream, buffer, response);
        // Return the value of the `Space-Used` field
        return std::atoi(response.base().at("Space-Used").data());
    }

    void reset() {
        // Send a POST request
        send_request(http::verb::post, "/reset");
        // Read the response
        http::response<http::string_body> response;
        http::read(stream, buffer, response);
        // Must be 200 OK
        if (response.result() != http::status::ok) {
            throw std::runtime_error{"server returned invalid status"};
        }
    }
};

Cache::Cache(std::string host, std::string port)
: pImpl_(new Impl{host, port}) {}

Cache::~Cache() = default;

void Cache::set(key_type key, val_type val, size_type size) {
    pImpl_->set(key, val, size);
}

Cache::val_type Cache::get(key_type key, size_type &val_size) const {
    return pImpl_->get(key, val_size);
}

bool Cache::del(key_type key) {
    return pImpl_->del(key);
}

Cache::size_type Cache::space_used() const {
    return pImpl_->space_used();
}

void Cache::reset() {
    pImpl_->reset();
}
