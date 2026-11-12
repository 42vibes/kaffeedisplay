cmake_minimum_required(VERSION 3.20)
project(kaffee_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

set(DOMAIN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/domain)
set(STORAGE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/storage)
set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/service)
set(REPORTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/reports)

add_executable(kaffee_tests
  doctest_main.cpp
  domain_test.cpp
  storage_test.cpp
  service_test.cpp
  auth_test.cpp
  reports_test.cpp
  ${DOMAIN_DIR}/src/domain.cpp
  ${DOMAIN_DIR}/src/json.cpp
  ${DOMAIN_DIR}/src/sha256.cpp
  ${STORAGE_DIR}/src/journal_store.cpp
  ${STORAGE_DIR}/src/file_store.cpp
  ${SERVICE_DIR}/src/auth.cpp
  ${SERVICE_DIR}/src/service.cpp
  ${REPORTS_DIR}/src/reports.cpp
)

target_include_directories(kaffee_tests PRIVATE
  ${DOMAIN_DIR}/include
  ${DOMAIN_DIR}/third_party
  ${STORAGE_DIR}/include
  ${SERVICE_DIR}/include
  ${REPORTS_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)

target_compile_definitions(kaffee_tests PRIVATE
  GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
)

find_package(Threads REQUIRED)
target_link_libraries(kaffee_tests PRIVATE Threads::Threads)
