cmake_minimum_required(VERSION 3.16)
project(qr_c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_UNIT_TEST "Enable unit tests" OFF)

# ----------------------------
# Build quality toggles (Linux only)
# ----------------------------
option(ENABLE_WARNINGS "Enable strong compiler warnings" ON)
option(WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
set(CLANG_TIDY_BIN "clang-tidy" CACHE STRING "clang-tidy executable")

option(ENABLE_CPPCHECK "Run cppcheck during compilation" OFF)
set(CPPCHECK_BIN "cppcheck" CACHE STRING "cppcheck executable")

option(ENABLE_IWYU "Run include-what-you-use during compilation" OFF)
set(IWYU_BIN "include-what-you-use" CACHE STRING "IWYU executable")

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

option(ENABLE_COVERAGE "Enable coverage flags" OFF) # GCC/Clang only

include(FetchContent)
find_package(Threads REQUIRED)

# ----------------------------
# Tooling helpers
# ----------------------------
function(apply_project_warnings target)
  if(NOT ENABLE_WARNINGS)
    return()
  endif()

  target_compile_options(${target} PRIVATE
    -Wall -Wextra -Wpedantic
    -Wshadow -Wconversion -Wsign-conversion
    -Wformat=2 -Wundef
    -Wnon-virtual-dtor
    -Wold-style-cast
  )

  if(WARNINGS_AS_ERRORS)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

function(apply_sanitizers target)
  set(SAN_FLAGS "")
  if(ENABLE_ASAN)
    list(APPEND SAN_FLAGS -fsanitize=address)
  endif()
  if(ENABLE_UBSAN)
    list(APPEND SAN_FLAGS -fsanitize=undefined)
  endif()
  if(ENABLE_TSAN)
    list(APPEND SAN_FLAGS -fsanitize=thread)
  endif()

  if(SAN_FLAGS)
    target_compile_options(${target} PRIVATE ${SAN_FLAGS} -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE ${SAN_FLAGS})
  endif()
endfunction()

function(apply_coverage target)
  if(NOT ENABLE_COVERAGE)
    return()
  endif()

  target_compile_options(${target} PRIVATE -O0 -g --coverage)
  target_link_options(${target} PRIVATE --coverage)
endfunction()

function(enable_static_analysis target)
  if(ENABLE_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES
      CXX_CLANG_TIDY "${CLANG_TIDY_BIN}"
    )
  endif()

  if(ENABLE_CPPCHECK)
    set_target_properties(${target} PROPERTIES
      CXX_CPPCHECK "${CPPCHECK_BIN};--enable=warning,style,performance,portability;--inline-suppr;--std=c++17"
    )
  endif()

  if(ENABLE_IWYU)
    set_target_properties(${target} PROPERTIES
      CXX_INCLUDE_WHAT_YOU_USE "${IWYU_BIN}"
    )
  endif()
endfunction()

# ----------------------------
# Dependencies
# ----------------------------
FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.0
)
FetchContent_MakeAvailable(Crow)

FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(CLI11)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

# ----------------------------
# Library (production code, no main)
# ----------------------------
add_library(qr_c_lib STATIC
  core/Core.cpp
  app/JobRunner.cpp
  adapters/rest/RestServer.cpp
)

target_include_directories(qr_c_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(qr_c_lib PUBLIC
  Threads::Threads
  spdlog::spdlog
  Crow::Crow
)

apply_project_warnings(qr_c_lib)
apply_sanitizers(qr_c_lib)
apply_coverage(qr_c_lib)
enable_static_analysis(qr_c_lib)

# ----------------------------
# Main executable
# ----------------------------
add_executable(qr_c
  main.cpp
)

target_link_libraries(qr_c PRIVATE
  qr_c_lib
  CLI11::CLI11
)

apply_project_warnings(qr_c)
apply_sanitizers(qr_c)
apply_coverage(qr_c)
enable_static_analysis(qr_c)

# ----------------------------
# Tests
# ----------------------------
if(ENABLE_UNIT_TEST)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()

# ----------------------------
# Formatting helper (optional)
# ----------------------------
find_program(CLANG_FORMAT_BIN clang-format)
if(CLANG_FORMAT_BIN)
  file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  )

  add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i ${ALL_CXX_SOURCE_FILES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running clang-format"
  )
endif()
