cmake_minimum_required(VERSION 3.20)
project(fleet-agent CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# cpp-httplib is header-only.
include_directories(${CMAKE_SOURCE_DIR}/third_party)

add_executable(fleet-agent
  src/main.cc
  src/MetricsCollector.cc
  src/MetricsSerializer.cc
  src/CommandRunner.cc
  src/Heartbeat.cc
  src/HmacUtils.cc
)

target_link_libraries(fleet-agent
  PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  Threads::Threads
)

# Enables cpp-httplib's TLS client code path so the agent can reach a
# MANAGER_URL exposed over HTTPS (e.g. via Tailscale Funnel), not just plain
# HTTP. OpenSSL is already linked above for HmacUtils, so this adds no new
# dependency.
target_compile_definitions(fleet-agent PRIVATE CPPHTTPLIB_OPENSSL_SUPPORT)

# Attempt static linking for portability across nodes that may not have a
# matching libstdc++/libgcc.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(fleet-agent PRIVATE -static-libgcc -static-libstdc++)
endif()
