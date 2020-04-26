cmake_minimum_required(VERSION 3.1.0)
project(csci389_cache)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Boost REQUIRED COMPONENTS program_options system)
include_directories(${Boost_INCLUDE_DIRS})

# General options
add_compile_options("-Wall" "-Wextra" "-Wpedantic" "-std=c++17")
# Debug options
add_compile_options("$<$<CONFIG:DEBUG>:-g;-fsanitize=address>")
add_link_options("$<$<CONFIG:DEBUG>:-fsanitize=address>")
# Release options
add_compile_options("$<$<CONFIG:RELEASE>:-Ofast;-march=native>")

add_executable(test_cache_lib
               test_cache_lib.cc cache_lib.cc fifo_evictor.cc)

add_executable(test_evictors
               test_evictors.cc cache_lib.cc fifo_evictor.cc lru_evictor.cc)

add_executable(cache_server
               cache_server.cc cache_lib.cc)
target_link_libraries(cache_server ${Boost_LIBRARIES} Threads::Threads)

add_executable(test_cache_client
               test_cache_client.cc cache_client.cc)
add_dependencies(test_cache_client cache_server)
target_link_libraries(test_cache_client ${Boost_LIBRARIES} Threads::Threads)

add_executable(request_driver
               request_driver.cc request_generator.cc cache_client.cc)
target_link_libraries(request_driver ${Boost_LIBRARIES} Threads::Threads)
