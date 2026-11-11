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
set(UI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../firmware/components/ui)

add_executable(kaffee_tests
  doctest_main.cpp
  domain_test.cpp
  ui_format_test.cpp
  ${DOMAIN_DIR}/src/domain.cpp
  ${DOMAIN_DIR}/src/json.cpp
  ${DOMAIN_DIR}/src/sha256.cpp
  ${UI_DIR}/src/format.cpp
)

target_include_directories(kaffee_tests PRIVATE
  ${DOMAIN_DIR}/include
  ${DOMAIN_DIR}/third_party
  ${UI_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/../third_party
)
