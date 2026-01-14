cmake_minimum_required(VERSION 3.16)
project(qr_c VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.2.0
)
FetchContent_MakeAvailable(Crow)

option(ENABLE_UNIT_TEST "Enable unit tests" OFF)

add_executable(qr_c
  main.cpp
  core/Core.cpp
  app/JobRunner.cpp
  adapters/rest/RestServer.cpp
)

target_link_libraries(qr_c PRIVATE Crow::Crow)

if(ENABLE_UNIT_TEST)
  enable_testing()
  include(CTest)
  add_subdirectory(tests)
endif()
