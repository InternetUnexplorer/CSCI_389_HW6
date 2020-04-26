#include "cache.hh"
#include "request_generator.hh"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

constexpr WorkloadParams PARAMS = {
    15,   // prob_get
    8,    // prob_set
    1,    // prob_del
    1000, // num_keys
    0.08, // val_size_dist
};

constexpr auto NUM_REQUESTS = 500000; // 500k

constexpr auto CACHE_HOST = "localhost";
constexpr auto CACHE_PORT = "4022";

using generator_type = RequestGenerator<std::mt19937>;

// Utility function to measure duration in milliseconds
float measure_latency(std::function<void()> fn) {
    const auto t0 = std::chrono::high_resolution_clock::now();
    fn();
    const auto tf = std::chrono::high_resolution_clock::now();
    const auto ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(tf - t0).count();
    return ns / 1e6f;
}

struct RequestStatistics {
    // Number of GET/SET/DEL requests
    unsigned num_gets = 0;
    unsigned num_sets = 0;
    unsigned num_dels = 0;
    // Number of GET requests that returned a value
    unsigned num_get_hits = 0;
    // Number of DEL requests that deleted a value
    unsigned num_del_hits = 0;

    friend std::ostream &operator<<(std::ostream &stream,
                                    const RequestStatistics &stats) {
        const auto format_percent = [](unsigned num, unsigned total) {
            return std::to_string(static_cast<int>(num * 1e2 / total)) + "%";
        };

        const auto num_total = stats.num_gets + stats.num_sets + stats.num_dels;

        stream << "    num_gets: " << stats.num_gets << " ("
               << format_percent(stats.num_gets, num_total) << ")" << std::endl;
        stream << "    num_sets: " << stats.num_sets << " ("
               << format_percent(stats.num_sets, num_total) << ")" << std::endl;
        stream << "    num_dels: " << stats.num_dels << " ("
               << format_percent(stats.num_dels, num_total) << ")" << std::endl;
        stream << "   num_total: " << num_total << std::endl;

        stream << "num_get_hits: " << stats.num_get_hits << " ("
               << format_percent(stats.num_get_hits, stats.num_gets) << ")"
               << std::endl;
        stream << "num_del_hits: " << stats.num_del_hits << " ("
               << format_percent(stats.num_del_hits, stats.num_dels) << ")"
               << std::endl;

        return stream;
    }
};

// Measure the completion time of `nreq` requests in milliseconds (and record
// statistics on request frequency, hit rate, etc.)
std::vector<float> baseline_latencies(unsigned nreq,
                                      const WorkloadParams &params,
                                      Cache &cache, RequestStatistics &stats) {
    // Create latencies vector (reserve space for `nreq` entries)
    std::vector<float> latencies(nreq);

    generator_type generator;
    for (auto i = 0u; i < nreq; ++i) {
        // Generate a request
        const auto request = generator(params);
        // Create a function to handle the result
        std::function<void()> request_fn;

        switch (request.type) {
        case Request::Type::GET:
            request_fn = [&] {
                Cache::size_type size;
                stats.num_get_hits += cache.get(request.key, size) != nullptr;
                stats.num_gets++;
            };
            break;
        case Request::Type::SET:
            request_fn = [&] {
                const std::string value = *request.value;
                cache.set(request.key, value.c_str(), value.length() + 1);
                stats.num_sets++;
            };
            break;
        case Request::Type::DEL:
            request_fn = [&] {
                stats.num_del_hits += cache.del(request.key);
                stats.num_dels++;
            };
            break;
        }

        // Measure latency of request
        latencies.push_back(measure_latency(request_fn));
    }

    return latencies;
}

// Measure the completion time of `nreq` requests and return the mean throughput
// (req/s) and the 95th-percentile latency (ms)
std::pair<float, float> baseline_performance(unsigned nreq,
                                             const WorkloadParams &params,
                                             Cache &cache,
                                             RequestStatistics &stats) {
    // Get the request latency numbers
    auto latencies = baseline_latencies(nreq, params, cache, stats);

    // Calculate the total amount of time of all of the requests in seconds
    const auto total_time =
        std::accumulate(latencies.begin(), latencies.end(), 0.0f) / 1e3f;
    // Calculate the mean throughput using the total time
    const auto mean_throughput = nreq / total_time;

    // Sort the latency numbers and get the 95th-percentile latency
    std::sort(latencies.begin(), latencies.end(), std::greater<float>());
    const auto latency = latencies[static_cast<unsigned>(nreq * 0.95)];

    // Return mean throughput and latency
    return std::pair{mean_throughput, latency};
}

int main(int argc, char **argv) {
    // Create the cache client
    Cache cache{CACHE_HOST, CACHE_PORT};

    // Warm up the cache first
    std::cerr << "*** warming up cache... (" << NUM_REQUESTS << " requests)"
              << std::endl;
    {
        RequestStatistics stats;
        baseline_performance(NUM_REQUESTS, PARAMS, cache, stats);
    }

    // Do the actual measurements
    std::cerr << "*** measuring... (" << NUM_REQUESTS << " requests)"
              << std::endl;
    RequestStatistics stats;
    if (argc == 2 && std::string{argv[1]} == "latencies") {
        // Measure latencies if `latencies` is passed as an argument
        const auto perf =
            baseline_latencies(NUM_REQUESTS, PARAMS, cache, stats);

        // Output raw latencies
        std::cout << "# Request #  Latency (ms)" << std::endl;
        for (auto i = 0; i < NUM_REQUESTS; ++i) {
            std::cout << i << "  " << std::to_string(perf[i]) << std::endl;
        }
    } else {
        // Otherwise, measure mean throughput and 95th-percentile latency
        const auto perf =
            baseline_performance(NUM_REQUESTS, PARAMS, cache, stats);

        std::cerr << "================================" << std::endl;
        std::cerr << stats << std::endl;
        std::cerr << "mean_throughput: " << perf.first << " req/s" << std::endl;
        std::cerr << "   95th_latency: "
                  << std::to_string(perf.second * 1000.0f) << " µs"
                  << std::endl;
    }
}
