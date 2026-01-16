cmake_minimum_required(VERSION 3.16)
project(qr_c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_UNIT_TEST "Enable unit tests" OFF)

include(FetchContent)

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

find_package(Threads REQUIRED)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(spdlog)

add_executable(qr_c
  main.cpp
  core/Core.cpp
  app/JobRunner.cpp
  adapters/rest/RestServer.cpp
)

target_link_libraries(qr_c
  PRIVATE
    Crow::Crow
    CLI11::CLI11
    Threads::Threads
    spdlog::spdlog
)

target_include_directories(qr_c
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          
 )

if(ENABLE_UNIT_TEST)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()
