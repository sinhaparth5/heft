cmake_minimum_required(VERSION 3.28)
# CalVer: YYYY.MM.PATCH (PATCH resets each month)
project(pkgtop VERSION 2026.11.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_executable(pkgtop src/main.cpp)
target_compile_options(pkgtop PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(pkgtop PRIVATE PKGTOP_VERSION="${PROJECT_VERSION}")
