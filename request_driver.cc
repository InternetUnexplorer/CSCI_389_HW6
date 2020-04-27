#include "cache.hh"
#include "request_generator.hh"

#include <algorithm>
#include <boost/process.hpp>
#include <chrono>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

/// Workload parameters (currently these roughly mimic the ETC workload)
constexpr WorkloadParams PARAMS = {
    15,   // prob_get
    8,    // prob_set
    1,    // prob_del
    1000, // num_keys
    0.08, // val_size_dist
};

/// Number of requests per thread (should be a power of 2)
constexpr auto NUM_REQUESTS = 1 << 20; // ~1M (1,048,576)

/// Minimum and maximum number of threads to spawn (must be powers of 2)
constexpr auto NUM_THREADS_MIN = 1;
constexpr auto NUM_THREADS_MAX = 128;

// Server parameters
constexpr auto SERVER_ADDRESS = "localhost";
constexpr auto SERVER_PORT = "4022";
constexpr auto SERVER_MAXMEM = 1 << 16; // 64KiB
const auto SERVER_THREADS = std::max(1U, std::thread::hardware_concurrency());

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

using latency_stats_type = std::pair<std::vector<float>, RequestStatistics>;

// Measure the completion time of `nreq` requests in milliseconds and record
// statistics on request frequency, hit rate, etc.
latency_stats_type baseline_latencies(const unsigned nreq,
                                      const WorkloadParams &params) {
    // Create the cache client
    Cache cache{SERVER_ADDRESS, SERVER_PORT};

    RequestStatistics stats;
    std::vector<float> latencies;
    latencies.reserve(nreq);

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

    return std::make_pair(latencies, stats);
}

// Measure the completion time of `nreq` requests in milliseconds per client for
// `nthreads` clients on separate threads and record statistics on request
// frequency, hit rate, etc.
latency_stats_type threaded_latencies(const unsigned nreq,
                                      const unsigned nthreads,
                                      const WorkloadParams &params) {
    RequestStatistics stats;
    std::vector<float> latencies;
    latencies.reserve(nreq * nthreads);

    // Spawn `nthreads` threads, each making `nreq` requests
    std::vector<std::future<latency_stats_type>> futures;
    futures.reserve(nthreads);
    for (auto i = 0U; i < nthreads; ++i) {
        futures.push_back(std::async(std::launch::async, [nreq, &params] {
            return baseline_latencies(nreq, params);
        }));
    }

    // Wait for each thread to finish and add the result to the total
    for (auto &future : futures) {
        future.wait();

        auto result = future.get();
        auto thread_latencies = result.first;
        auto thread_stats = result.second;

        latencies.insert(latencies.end(), thread_latencies.begin(),
                         thread_latencies.end());

        stats.num_gets += thread_stats.num_gets;
        stats.num_sets += thread_stats.num_sets;
        stats.num_dels += thread_stats.num_dels;
        stats.num_get_hits += thread_stats.num_get_hits;
        stats.num_del_hits += thread_stats.num_del_hits;
    }

    return std::make_pair(latencies, stats);
}

// Measure the completion time of `nreq` requests and return the mean
// throughput (req/s) and the 95th-percentile latency (ms)
std::pair<float, float> baseline_performance(const unsigned nreq,
                                             const WorkloadParams &params) {
    // Get the request latency numbers
    auto latencies = baseline_latencies(nreq, params).first;
    // Calculate the total amount of time of all of the requests
    const auto total_time =
        std::accumulate(latencies.begin(), latencies.end(), 0.0f);
    // Calculate the mean throughput using the total time
    const auto mean_throughput = nreq / (total_time / 1e3f);

    // Sort the latency numbers and get the 95th-percentile latency
    std::sort(latencies.begin(), latencies.end(), std::greater<float>());
    const auto latency = latencies[static_cast<unsigned>(nreq * 0.95)];

    // Return mean throughput and latency
    return std::pair{mean_throughput, latency};
}

// Measure the completion time of `nreq` requests per client for `nthreads`
// clients on separate threads and return the mean throughput (req/s) and the
// 95th-percentile latency (ms)
std::pair<float, float> threaded_performance(const unsigned nreq,
                                             const unsigned nthreads,
                                             const WorkloadParams &params) {
    std::vector<float> latencies;
    // Calculate the total amount of time of all of the requests
    const auto total_time = measure_latency(
        [&] { latencies = threaded_latencies(nreq, nthreads, params).first; });
    // Calculate the mean throughput using the total time
    const auto mean_throughput = (nreq * nthreads) / (total_time / 1e3f);

    // Sort the latency numbers and get the 95th-percentile latency
    std::sort(latencies.begin(), latencies.end(), std::greater<float>());
    const auto latency = latencies[static_cast<unsigned>(nreq * 0.95)];

    // Return mean throughput and latency
    return std::pair{mean_throughput, latency};
}

/// Spawn the server as a child process and run the provided function after it
/// has started
void run_with_server(const std::function<void()> &inner) {
    /// Spawn the server as a child process, capturing stdout
    boost::process::ipstream std_out;
    boost::process::child server(
        "./cache_server", "--server", SERVER_ADDRESS, "--port", SERVER_PORT,
        "--maxmem", std::to_string(SERVER_MAXMEM), "--threads",
        std::to_string(SERVER_THREADS), boost::process::std_out > std_out);
    // Wait for the line that says the server is running
    std::string line;
    std::getline(std_out, line);
    // Run the provided function
    inner();
    // Terminate the server process (a bit unclean, but there doesn't seem to be
    // a nice platform-independent way to send SIGINT)
    server.terminate();
    server.wait();
}

int main() {
    std::cout << "# Workload Parameters:" << std::endl;
    std::cout << "#   prob_get = " << PARAMS.prob_get
              << ", prob_set = " << PARAMS.prob_set
              << ", prob_del = " << PARAMS.prob_del
              << ", num_keys = " << PARAMS.num_keys
              << ", val_size_dist = " << PARAMS.val_size_dist << std::endl;
    std::cout << "#" << std::endl;

    std::cout << "# Server Parameters:" << std::endl;
    std::cout << "#   address = " << SERVER_ADDRESS
              << ", port = " << SERVER_PORT << ", maxmem = " << SERVER_MAXMEM
              << ", threads = " << SERVER_THREADS << std::endl;
    std::cout << "#" << std::endl;

    // Spawn the server as a child process
    run_with_server([] {
        // Warm up the cache
        baseline_performance(NUM_REQUESTS, PARAMS);

        std::cout << "# Threads  Mean Req/s  95% Latency (µs)" << std::endl;

        // Start at `NUM_THREADS_MIN` and go up to `NUM_THREADS_MAX`, doubling
        // the number of threads each iteration
        for (auto nthreads = NUM_THREADS_MIN; nthreads <= NUM_THREADS_MAX;
             nthreads *= 2) {
            // Measure throughput and latency
            const auto perf =
                threaded_performance(NUM_REQUESTS, nthreads, PARAMS);

            // Output values
            std::cout << "  " << std::setw(7) << std::left << nthreads
                      << std::resetiosflags(std::cout.flags());
            std::cout << "  " << std::setw(10) << std::left
                      << static_cast<unsigned>(perf.first)
                      << std::resetiosflags(std::cout.flags());
            std::cout << "  " << std::setw(8) << std::left << std::fixed
                      << std::setprecision(1) << perf.second * 1000.0f
                      << std::resetiosflags(std::cout.flags());
            std::cout << "  # " << (NUM_REQUESTS / nthreads)
                      << " requests per thread" << std::endl;
        }
    });
}
