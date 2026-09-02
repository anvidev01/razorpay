cmake_minimum_required(VERSION 3.20)
project(rig LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# A printf format/argument mismatch in rig_riskeval compiled silently and segfaulted at
# runtime. Warnings are not optional in something that moves money.
add_compile_options(-Wall -Wextra -Wformat=2 -Wno-unused-parameter)

# --- dependency discovery -------------------------------------------------------
# Previously this hardcoded Homebrew prefixes, so the project simply did not build on
# Linux despite run.sh advertising Debian packages. Find them properly, then fall back
# to Homebrew only on macOS.
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

find_package(simdjson QUIET CONFIG)
if(NOT simdjson_FOUND)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SIMDJSON QUIET simdjson)
  endif()
endif()

if(APPLE AND NOT simdjson_FOUND AND NOT SIMDJSON_FOUND)
  execute_process(COMMAND brew --prefix simdjson
                  OUTPUT_VARIABLE SIMDJSON_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT SIMDJSON_PREFIX)
    set(SIMDJSON_PREFIX /opt/homebrew/opt/simdjson)
  endif()
endif()

add_library(rigcore STATIC
  src/kernel.cpp src/crypto.cpp src/wal.cpp src/parse.cpp src/pct.cpp src/gateway.cpp src/rail.cpp src/evidence.cpp src/intent.cpp src/dataset.cpp)
target_include_directories(rigcore PUBLIC include)
target_link_libraries(rigcore PUBLIC OpenSSL::Crypto CURL::libcurl)

if(simdjson_FOUND)                       # CMake config package (vcpkg, recent distros)
  target_link_libraries(rigcore PUBLIC simdjson::simdjson)
elseif(SIMDJSON_FOUND)                   # pkg-config (Debian/Ubuntu libsimdjson-dev)
  target_include_directories(rigcore PUBLIC ${SIMDJSON_INCLUDE_DIRS})
  target_link_directories(rigcore PUBLIC ${SIMDJSON_LIBRARY_DIRS})
  target_link_libraries(rigcore PUBLIC ${SIMDJSON_LIBRARIES})
else()                                   # Homebrew layout
  target_include_directories(rigcore PUBLIC ${SIMDJSON_PREFIX}/include)
  target_link_directories(rigcore PUBLIC ${SIMDJSON_PREFIX}/lib)
  target_link_libraries(rigcore PUBLIC simdjson)
endif()

foreach(tool rig_eval rig_replay rig_gateway rig_attack rig_load rig_evidence rig_audit rig_riskeval rig_intent rig_sign wal_dump)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/${tool}.cpp)
    string(REPLACE "_" "-" bin ${tool})
    add_executable(${bin} src/${tool}.cpp)
    target_link_libraries(${bin} PRIVATE rigcore)
  endif()
endforeach()

add_executable(rig-tests tests/test_kernel.cpp)
add_executable(rig-tests-intent tests/test_intent.cpp)
target_link_libraries(rig-tests-intent PRIVATE rigcore)
target_link_libraries(rig-tests PRIVATE rigcore)
enable_testing()
add_test(NAME kernel COMMAND rig-tests)
add_test(NAME intent COMMAND rig-tests-intent ${CMAKE_SOURCE_DIR}/../fixtures/catalog.json)

add_executable(bench-engine-kernel bench/bench_engine_kernel.cpp)
target_link_libraries(bench-engine-kernel PRIVATE rigcore)
