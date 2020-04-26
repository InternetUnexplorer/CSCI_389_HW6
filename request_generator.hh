#ifndef REQUEST_GENERATOR_H
#define REQUEST_GENERATOR_H

#include <functional>
#include <optional>
#include <random>

struct WorkloadParams {
    // Probabilities for each request type (by ratio)
    unsigned prob_get;
    unsigned prob_set;
    unsigned prob_del;
    // Number of keys to choose between for each request
    unsigned num_keys;
    // Geometric distribution probability used to calculate value size
    double val_size_dist;
};

struct Request {
    enum class Type { GET, SET, DEL };

    const Type type;
    const std::string key;
    const std::optional<std::string> value;

    static Request get(std::string key);
    static Request set(std::string key, std::string value);
    static Request del(std::string key);

  private:
    Request(Type type, std::string key,
            std::optional<std::string> value = std::nullopt);
};

template <typename Rand> class RequestGenerator {
  private:
    Rand random;

    Request::Type generate_type(const WorkloadParams &params);
    std::string generate_key(const WorkloadParams &params);
    std::string generate_value(const WorkloadParams &params);

  public:
    RequestGenerator();
    Request operator()(const WorkloadParams &params);
};

template <typename Rand>
Request::Type
RequestGenerator<Rand>::generate_type(const WorkloadParams &params) {
    const unsigned total = params.prob_get + params.prob_set + params.prob_del;
    auto dist = std::uniform_int_distribution<unsigned>{0, total - 1};

    const unsigned value = dist(random);
    if (value < params.prob_get) {
        return Request::Type::GET;
    } else if (value < params.prob_get + params.prob_set) {
        return Request::Type::SET;
    } else {
        return Request::Type::DEL;
    }
}

template <typename Rand>
std::string RequestGenerator<Rand>::generate_key(const WorkloadParams &params) {
    auto dist =
        std::geometric_distribution<unsigned>(1.0 / (params.num_keys - 1));
    return std::to_string(dist(random));
}

template <typename Rand>
std::string
RequestGenerator<Rand>::generate_value(const WorkloadParams &params) {
    auto dist = std::geometric_distribution<unsigned>(params.val_size_dist);
    return std::string(dist(random) + 1, 'a');
}

template <typename Rand>
RequestGenerator<Rand>::RequestGenerator()
: random{Rand{std::random_device{}()}} {}

template <typename Rand>
Request RequestGenerator<Rand>::operator()(const WorkloadParams &params) {
    switch (generate_type(params)) {
    case Request::Type::GET:
        return Request::get(generate_key(params));
    case Request::Type::SET:
        return Request::set(generate_key(params), generate_value(params));
    case Request::Type::DEL:
        return Request::del(generate_key(params));
    }
    __builtin_unreachable();
}

#endif // REQUEST_GENERATOR_H
