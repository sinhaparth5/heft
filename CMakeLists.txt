cmake_minimum_required(VERSION 3.28)
# CalVer: YYYY.MM.PATCH (PATCH resets each month)
project(pkgtop VERSION 2026.11.5 LANGUAGES CXX)

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
  src/npm.cpp src/pnpm.cpp src/go.cpp src/path.cpp src/usage.cpp src/mutate.cpp
  src/updates.cpp src/docker.cpp)
target_include_directories(pkgtop_core PUBLIC src)

add_executable(pkgtop src/main.cpp src/tui.cpp src/http.cpp)
target_link_libraries(pkgtop PRIVATE pkgtop_core ftxui::component)
# Optional: only the opt-in update checks (--updates, --outdated) need it.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(CURL QUIET libcurl)
  # Optional: reads pacman's gzip sync dbs for update checks. Linked by name (-lz) so the
  # static build picks libz.a.
  pkg_check_modules(ZLIB QUIET zlib)
endif()
if(ZLIB_FOUND)
  target_link_libraries(pkgtop_core PUBLIC ${ZLIB_LIBRARIES})
  target_compile_definitions(pkgtop_core PUBLIC PKGTOP_ZLIB)
endif()
# Release builds: one fully static binary (musl), so it runs on any distro.
option(PKGTOP_STATIC "Link pkgtop fully statically" OFF)
if(PKGTOP_STATIC)
  target_link_options(pkgtop PRIVATE -static)
endif()
if(CURL_FOUND)
  if(PKGTOP_STATIC)
    target_link_directories(pkgtop PRIVATE ${CURL_STATIC_LIBRARY_DIRS})
    target_link_libraries(pkgtop PRIVATE ${CURL_STATIC_LIBRARIES})
  else()
    target_link_libraries(pkgtop PRIVATE ${CURL_LINK_LIBRARIES})
  endif()
  target_include_directories(pkgtop PRIVATE ${CURL_INCLUDE_DIRS})
  target_compile_definitions(pkgtop PRIVATE PKGTOP_CURL)
endif()
target_compile_definitions(pkgtop PRIVATE PKGTOP_VERSION="${PROJECT_VERSION}")

enable_testing()
foreach(t dpkg cargo size json providers path usage mutate updates docker)
  add_executable(test_${t} tests/test_${t}.cpp)
  target_link_libraries(test_${t} PRIVATE pkgtop_core)
  # Tests use assert(); keep it on in every build type.
  target_compile_options(test_${t} PRIVATE -UNDEBUG)
  target_compile_definitions(test_${t} PRIVATE FIXTURES="${CMAKE_SOURCE_DIR}/tests/fixtures")
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
