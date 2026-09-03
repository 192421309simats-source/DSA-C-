cmake_minimum_required(VERSION 3.10)
project(CampusFindAI CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)

add_executable(campusfind_server backend/main.cpp)
target_include_directories(campusfind_server PRIVATE backend)
target_link_libraries(campusfind_server PRIVATE Threads::Threads)

# On Windows, cpp-httplib needs ws2_32 for sockets.
if(WIN32)
  target_link_libraries(campusfind_server PRIVATE ws2_32 crypt32)
endif()

message(STATUS "Build with: cmake -B build && cmake --build build")
message(STATUS "Run with:   ./campusfind_server  (from the backend/ directory, so data/ and ../frontend resolve)")
