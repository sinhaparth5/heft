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

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(pkgtop_core STATIC src/file.cpp src/dpkg.cpp src/cargo.cpp)
target_include_directories(pkgtop_core PUBLIC src)

add_executable(pkgtop src/main.cpp)
target_link_libraries(pkgtop PRIVATE pkgtop_core)
target_compile_definitions(pkgtop PRIVATE PKGTOP_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(t dpkg cargo)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE pkgtop_core)
  # Tests use assert(); keep it on in every build type.
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  target_compile_definitions(test_${t} PRIVATE FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
