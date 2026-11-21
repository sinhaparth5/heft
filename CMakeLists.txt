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

# TUI dependency. Fetched before our warning flags so they don't apply to it.
include(FetchContent)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v6.1.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ftxui)

add_compile_options(-Wall -Wextra -Wpedantic)

add_library(pkgtop_core STATIC
  src/file.cpp src/json.cpp src/size.cpp src/caches.cpp
  src/dpkg.cpp src/cargo.cpp src/pacman.cpp src/brew.cpp src/uv.cpp src/pipx.cpp
  src/npm.cpp src/pnpm.cpp src/go.cpp src/path.cpp)
target_include_directories(pkgtop_core PUBLIC src)

add_executable(pkgtop src/main.cpp src/tui.cpp)
target_link_libraries(pkgtop PRIVATE pkgtop_core ftxui::component)
target_compile_definitions(pkgtop PRIVATE PKGTOP_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(t dpkg cargo size json providers path)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE pkgtop_core)
  # Tests use assert(); keep it on in every build type.
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  target_compile_definitions(test_${t} PRIVATE FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
