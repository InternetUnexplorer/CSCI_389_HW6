#include "cache.hh"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <optional>
#include <regex>
#include <string>
#include <thread>

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using tcp = net::ip::tcp;

/// Default port to run the server on
constexpr auto DEFAULT_PORT = 4022;

/// Timeout for reading a request
constexpr auto SOCKET_TIMEOUT = std::chrono::seconds(10);

/// Regular expressions for parsing request targets
const std::regex KEY_RE{R"(/([A-Za-z0-9\._-]+))"};
const std::regex KEY_VALUE_RE{R"(/([A-Za-z0-9\._-]+)/([A-Za-z0-9\._-]+))"};

class Connection : public std::enable_shared_from_this<Connection> {
  private:
    beast::tcp_stream stream;
    std::shared_ptr<Cache> cache;

    // These values are stored in the class so that they stay alive throughout
    // the duration of an async operation
    using buffer_type = beast::flat_buffer;
    using request_type = http::request<http::empty_body>;
    using response_type = std::shared_ptr<void>;
    buffer_type buffer;     // Buffer used by `http::async_read`
    request_type request;   // Request set by `http::async_read`
    response_type response; // Response used by `http::async_write`

    /// Extract the key from a target
    std::optional<std::string> extract_key(const std::string &target) const {
        // Attempt to match against key regex
        std::smatch match;
        if (!std::regex_match(target, match, KEY_RE)) {
            // No match
            return std::nullopt;
        }
        // Return key
        return std::optional<std::string>{match[1]};
    }

    /// Extract the key-value pair from a target
    std::optional<std::pair<std::string, std::string>>
    extract_key_value(const std::string &target) const {
        // Attempt to match against key-value regex
        std::smatch match;
        if (!std::regex_match(target, match, KEY_VALUE_RE)) {
            // No match
            return std::nullopt;
        }
        // Return key-value pair
        return std::make_optional<std::pair<std::string, std::string>>(
            match[1], match[2]);
    }

    /// Make an HTTP response with the specified status
    template <typename Body>
    http::response<Body> make_response(http::status status,
                                       request_type &request) const {
        http::response<Body> response{status, request.version()};
        response.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        response.keep_alive(request.keep_alive());
        return response;
    }

    /// Make an HTTP response with the specified status and an empty body
    http::response<http::empty_body>
    make_empty_response(http::status status, request_type &request) const {
        auto response = make_response<http::empty_body>(status, request);
        response.prepare_payload();
        return response;
    }

    /// Handle a request
    void handle_request(request_type &&request) {
        switch (request.method()) {
        case http::verb::get:
            return handle_get_request(std::move(request));
        case http::verb::put:
            return handle_put_request(std::move(request));
        case http::verb::delete_:
            return handle_delete_request(std::move(request));
        case http::verb::head:
            return handle_head_request(std::move(request));
        case http::verb::post:
            return handle_post_request(std::move(request));
        default:
            // Send 400 Bad Request for all other methods
            do_write(make_empty_response(http::status::bad_request, request));
        }
    }

    /// Handle a GET request
    void handle_get_request(request_type &&request) {
        // Extract the key from the target
        const auto key = extract_key(request.target().to_string());
        // Send 400 Bad Request if the target did not match
        if (key == std::nullopt) {
            return do_write(
                make_empty_response(http::status::bad_request, request));
        }
        // Fetch the value from the cache
        Cache::size_type size;
        Cache::val_type value = cache->get(*key, size);
        // Check whether the value was found
        if (value != nullptr) {
            // Send 200 OK with a JSON body containing the key-value pair
            auto response =
                make_response<http::string_body>(http::status::ok, request);
            response.set(http::field::content_type, "application/json");
            response.body() = R"({"key":")" + *key + R"(","value":")" +
                              std::string(value, size - 1) + R"("})";
            response.prepare_payload();
            do_write(response);
        } else {
            // Send 404 Not Found if the value was not found
            do_write(make_empty_response(http::status::not_found, request));
        }
    }

    /// Handle a PUT request
    void handle_put_request(request_type &&request) {
        // Extract the key-value pair from the target
        const auto kv_pair = extract_key_value(request.target().to_string());
        // Send 400 Bad Request if the target did not match
        if (kv_pair == std::nullopt) {
            return do_write(
                make_empty_response(http::status::bad_request, request));
        }
        // Set the value and send 200 OK
        cache->set(kv_pair->first, kv_pair->second.c_str(),
                   kv_pair->second.size() + 1);
        do_write(make_empty_response(http::status::ok, request));
    }

    /// Handle a DELETE request
    void handle_delete_request(request_type &&request) {
        // Extract the key from the target
        const auto key = extract_key(request.target().to_string());
        // Send 400 Bad Request if the target did not match
        if (key == std::nullopt) {
            return do_write(
                make_empty_response(http::status::bad_request, request));
        }
        // Delete the entry
        const auto status =
            cache->del(*key) ? http::status::ok : http::status::not_found;
        // Send 200 OK if the entry was deleted, or 404 Not Found if it was not
        // in the cache
        do_write(make_empty_response(status, request));
    }

    /// Handle a HEAD request
    void handle_head_request(request_type &&request) {
        auto response = make_empty_response(http::status::ok, request);
        response.set(http::field::content_type, "application/json");
        // The server technically doesn't accept any content type since
        // everything is in the target, but this works as a substitute
        response.set(http::field::accept, "text/plain");
        response.set("Space-Used", cache->space_used());
        do_write(response);
    }

    /// Handle a POST request
    void handle_post_request(request_type &&request) {
        if (request.target() == "/reset") {
            cache->reset();
            do_write(make_empty_response(http::status::ok, request));
        } else {
            // Targets other than `/reset` are not supported
            do_write(make_empty_response(http::status::not_found, request));
        }
    }

    /// Read an HTTP request asynchronously
    void do_read() {
        // Make the request empty before reading
        request = {};
        // Set the timeout
        stream.expires_after(SOCKET_TIMEOUT);
        // Dispatch the read operation
        http::async_read(stream, buffer, request,
                         beast::bind_front_handler(&Connection::on_read,
                                                   shared_from_this()));
    }

    /// Handle the result of a read
    void on_read(const beast::error_code error, const size_t) {
        // Close the connection if there was an error
        if (error) {
            return do_close();
        }
        // Handle the request
        handle_request(std::move(request));
    }

    /// Write an HTTP response asynchronously
    template <typename Body>
    void do_write(const http::response<Body> &response_) {
        const auto response_ptr =
            std::make_shared<http::response<Body>>(std::move(response_));
        // Dispatch the write operation
        http::async_write(stream, *response_ptr,
                          beast::bind_front_handler(&Connection::on_write,
                                                    shared_from_this(),
                                                    response_ptr->need_eof()));
        // Keep a type-erased pointer to the response
        response = std::move(response_ptr);
    }

    /// Handle the result of a write
    void on_write(const bool close, const beast::error_code error, size_t) {
        // Close the connection if there was an error or if the response
        // requires an EOF
        if (error || close) {
            return do_close();
        }
        // Delete the response; it will be missed
        response = nullptr;
        // Read another request
        do_read();
    }

    /// Close the connection gracefully
    void do_close() {
        beast::error_code error;
        stream.socket().shutdown(tcp::socket::shutdown_send, error);
    }

  public:
    Connection(tcp::socket &&socket, std::shared_ptr<Cache> cache)
    : stream{std::move(socket)}, cache{std::move(cache)} {}

    /// Start reading requests
    void run() {
        do_read();
    }
};

class Server : public std::enable_shared_from_this<Server> {
  private:
    tcp::acceptor acceptor;
    // No need to synchronize access to the cache since the server is
    // single-threaded for now (everything runs in a single implicit strand)
    std::shared_ptr<Cache> cache;

    /// Accept an incoming connection asynchronously
    void do_accept() {
        acceptor.async_accept(
            beast::bind_front_handler(&Server::on_accept, shared_from_this()));
    }

    /// Handle the result of an accept
    void on_accept(const beast::error_code error, tcp::socket &&socket) {
        // Create a connection for this socket if the accept succeeded
        if (!error) {
            std::make_shared<Connection>(std::move(socket), cache)->run();
        }
        // Keep accepting connections
        do_accept();
    }

  public:
    Server(net::io_context &context, const tcp::endpoint &endpoint,
           std::unique_ptr<Cache> cache)
    : acceptor{context}, cache{std::move(cache)} {
        // Configure the acceptor and bind it to the endpoint
        acceptor.open(endpoint.protocol());
        acceptor.set_option(net::socket_base::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();
    }

    /// Start accepting connections
    void run() {
        do_accept();
    }
};

/// Use a resolver to get the first endpoint associated with the specified
/// address and port
tcp::endpoint get_endpoint(const std::string &address, uint16_t port) {
    // Create a resolver with a new I/O service
    net::io_service service;
    tcp::resolver resolver{service};
    // Return the endpoint of the first entry found
    return resolver.resolve(address, std::to_string(port)).begin()->endpoint();
}

/// Parse the program options and run the server
int run_server(const int argc, const char *const argv[]) {
    namespace po = boost::program_options;

    // Configure command-line options
    po::options_description options{"Usage"};
    options.add_options()("help,h", "show this help message");
    options.add_options()("maxmem,m", po::value<Cache::size_type>()->required(),
                          "set cache memory limit in bytes");
    options.add_options()("server,s",
                          po::value<std::string>()->default_value("localhost"),
                          "set server address");
    options.add_options()("port,p", po::value<uint16_t>()->default_value(4022),
                          "set server port");
    options.add_options()("threads,t", po::value<unsigned>()->default_value(1),
                          "set number of threads (NYI)");

    // Parse command-line arguments
    po::variables_map config;
    try {
        po::store(po::parse_command_line(argc, argv, options), config);
        po::notify(config);
    } catch (const po::error &error) {
        // Show usage if arguments were invalid
        std::cerr << "error: " << error.what() << std::endl << std::endl;
        std::cerr << options << std::endl;
        return 1;
    }

    // Show help if help flag is present
    if (config.count("help")) {
        std::cerr << options << std::endl;
        return 0;
    }

    // Get configuration values
    const auto maxmem = config["maxmem"].as<Cache::size_type>();
    const auto host = config["server"].as<std::string>();
    const auto port = config["port"].as<uint16_t>();
    const auto threads = config["threads"].as<unsigned>();

    // Warn if number of threads != 1
    if (threads != 1) {
        std::cerr
            << "warning: setting the number of threads is not supported yet"
            << std::endl;
    }

    // Resolve the endpoint
    const auto endpoint = get_endpoint(host, port);

    // Create the I/O context
    net::io_context context;

    // Add a handler to stop the I/O context on SIGINT/SIGTERM
    net::signal_set quit_signals{context, SIGINT, SIGTERM};
    quit_signals.async_wait(
        [&](beast::error_code const &, int) { context.stop(); });

    // Create the cache and the server
    auto cache = std::make_unique<Cache>(maxmem);
    std::make_shared<Server>(context, endpoint, std::move(cache))->run();

    // Print that the server has been started
    context.post([&] {
        std::cout << "running on " << endpoint.address() << " port "
                  << endpoint.port() << std::endl;
    });

    // Run the I/O context
    context.run();

    // Terminate normally
    return 0;
}

/// Wrapper that calls `run_server` and catches errors
int main(const int argc, const char *const argv[]) {
    try {
        return run_server(argc, argv);
    } catch (boost::system::system_error &error) {
        std::cerr << "error: " << error.what() << std::endl;
        return 2;
    }
}
