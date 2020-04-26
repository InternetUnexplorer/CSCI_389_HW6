#include "request_generator.hh"

#include <iostream>

////////////////////////////////////////////////
// Request
////////////////////////////////////////////////

Request::Request(Request::Type type, std::string key,
                 std::optional<std::string> value)
: type{type}, key{std::move(key)}, value{std::move(value)} {}

Request Request::get(std::string key) {
    return Request{Type::GET, std::move(key)};
}

Request Request::set(std::string key, std::string value) {
    return Request{Type::SET, std::move(key), std::make_optional(value)};
}

Request Request::del(std::string key) {
    return Request{Type::DEL, std::move(key)};
}
