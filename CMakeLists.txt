cmake_minimum_required(VERSION 3.20)
project(tehran_metro_route_finder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
# JSON parsing only (header-only). The graph and Dijkstra are our own code.
FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(json)

add_library(metro_core
  src/network.cpp
  src/router.cpp
  src/fuzzy.cpp
  src/route_json.cpp)
target_include_directories(metro_core PUBLIC include)
target_link_libraries(metro_core PUBLIC nlohmann_json::nlohmann_json)
if(MSVC)
  target_compile_options(metro_core PRIVATE /W4)
else()
  target_compile_options(metro_core PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

set(METRO_DATA_FILE "${CMAKE_CURRENT_SOURCE_DIR}/data/tehran_metro.json")

if(EMSCRIPTEN)
  # emcmake cmake -B build-wasm && cmake --build build-wasm  ->  site/metro.js + site/metro.wasm
  add_executable(metro_wasm web/bindings.cpp)
  target_link_libraries(metro_wasm PRIVATE metro_core)
  set_target_properties(metro_wasm PROPERTIES
    OUTPUT_NAME metro
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/site")
  target_link_options(metro_wasm PRIVATE
    -sMODULARIZE=1 -sEXPORT_NAME=createMetroModule -sENVIRONMENT=web,node
    -sALLOW_MEMORY_GROWTH=1 -sDISABLE_EXCEPTION_CATCHING=0
    -sEXPORTED_FUNCTIONS=_mt_init,_mt_route,_mt_search,_mt_last_error
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap)
  target_compile_options(metro_core PRIVATE -fexceptions)
  target_compile_options(metro_wasm PRIVATE -fexceptions)
  target_link_options(metro_wasm PRIVATE -fexceptions)
  # The page loads the same data file the CLI uses.
  configure_file("${METRO_DATA_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/site/data/tehran_metro.json" COPYONLY)
  return()
endif()

add_executable(metro src/main.cpp)
target_link_libraries(metro PRIVATE metro_core)
target_compile_definitions(metro PRIVATE METRO_DATA_FILE="${METRO_DATA_FILE}")

option(METRO_BUILD_TESTS "Build the unit tests" ON)
if(METRO_BUILD_TESTS)
  enable_testing()
  FetchContent_Declare(Catch2
    URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.7.1.tar.gz
    URL_HASH SHA256=c991b247a1a0d7bb9c39aa35faf0fe9e19764213f28ffba3109388e62ee0269c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(Catch2)

  add_executable(metro_tests
    tests/test_network.cpp
    tests/test_dijkstra.cpp
    tests/test_fuzzy.cpp
    tests/test_tehran.cpp
    web/bindings.cpp)
  target_link_libraries(metro_tests PRIVATE metro_core Catch2::Catch2WithMain)
  target_compile_definitions(metro_tests PRIVATE METRO_DATA_FILE="${METRO_DATA_FILE}")

  list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
  include(Catch)
  catch_discover_tests(metro_tests)

  # End-to-end checks of the real command-line program.
  add_test(NAME cli_route_line1 COMMAND metro route Tajrish "Imam Khomeini")
  set_tests_properties(cli_route_line1 PROPERTIES
    PASS_REGULAR_EXPRESSION "Line 1 +Tajrish → Emam Khomeini.*0 changes of line")
  add_test(NAME cli_route_transfer COMMAND metro route Tajrish Azadi)
  set_tests_properties(cli_route_transfer PROPERTIES PASS_REGULAR_EXPRESSION "change at")
  add_test(NAME cli_unknown_station COMMAND metro route Tajrish "xqzzv")
  set_tests_properties(cli_unknown_station PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_json COMMAND metro route tajrish "teatr shahr" --json)
  set_tests_properties(cli_json PROPERTIES PASS_REGULAR_EXPRESSION "\"ok\":true")
endif()
