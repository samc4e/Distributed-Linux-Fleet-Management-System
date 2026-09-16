cmake_minimum_required(VERSION 3.20)
project(fleet-backend CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
find_package(OpenSSL REQUIRED)

# jwt-cpp is header-only; add its flattened include/jwt-cpp copy as an
# include directory (see third_party/jwt-cpp).
include_directories(${CMAKE_SOURCE_DIR}/third_party)

add_executable(fleet-backend
  src/main.cc
  src/AuthController.cc
  src/NodeController.cc
  src/MetricsController.cc
  src/AgentController.cc
  src/InternalController.cc
  src/JwtMiddleware.cc
  src/Database.cc
  src/PrometheusPoller.cc
  src/HmacUtils.cc
  src/JwtUtils.cc
)

target_link_libraries(fleet-backend
  PRIVATE
  Drogon::Drogon
  OpenSSL::SSL
  OpenSSL::Crypto
)
