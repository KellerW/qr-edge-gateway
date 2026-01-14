cmake_minimum_required(VERSION 3.16)

# Unit tests for qr-c (Catch2)
include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

find_package(Threads REQUIRED)

set(QR_C_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/..)

add_executable(qr_c_tests
  test_core.cpp
  test_jobstore.cpp
  test_dispatcher.cpp
  test_jobrunner.cpp
)

target_include_directories(qr_c_tests PRIVATE
  ${QR_C_SRC_DIR}
  ${QR_C_SRC_DIR}/core
  ${QR_C_SRC_DIR}/app
  ${QR_C_SRC_DIR}/adapters/rest
)

target_link_libraries(qr_c_tests PRIVATE
  Catch2::Catch2WithMain
  Threads::Threads
)

add_test(NAME qr_c_tests COMMAND qr_c_tests)
