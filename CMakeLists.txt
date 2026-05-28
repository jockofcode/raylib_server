cmake_minimum_required(VERSION 3.15)
project(raylib_server C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS OFF)

include(FetchContent)

# ---------------------------------------------------------------------------
# raylib
# ---------------------------------------------------------------------------

FetchContent_Declare(
  raylib
  GIT_REPOSITORY https://github.com/raysan5/raylib.git
  GIT_TAG        5.0
  GIT_SHALLOW    TRUE
)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

# ---------------------------------------------------------------------------
# cJSON — bypass its CMakeLists to avoid the "uninstall" target conflict
# ---------------------------------------------------------------------------

FetchContent_Declare(
  cjson
  GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
  GIT_TAG        v1.7.18
  GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(cjson)
if(NOT cjson_POPULATED)
  FetchContent_Populate(cjson)
endif()

add_library(cjson STATIC ${cjson_SOURCE_DIR}/cJSON.c)
target_include_directories(cjson PUBLIC ${cjson_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Unity test framework — just the single source file
# ---------------------------------------------------------------------------

FetchContent_Declare(
  unity
  GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
  GIT_TAG        v2.5.2
  GIT_SHALLOW    TRUE
)
FetchContent_GetProperties(unity)
if(NOT unity_POPULATED)
  FetchContent_Populate(unity)
endif()

set(UNITY_SRC ${unity_SOURCE_DIR}/src/unity.c)

# ---------------------------------------------------------------------------
# Core library — no raylib dependency; usable from all tests
# ---------------------------------------------------------------------------

set(CORE_SOURCES
  src/queue.c
  src/protocol.c
  src/handle_registry.c
  src/color.c
  src/server.c
  src/rls_log.c
  src/b64.c
)

add_library(rls_core STATIC ${CORE_SOURCES})
target_include_directories(rls_core PUBLIC src)
target_link_libraries(rls_core PUBLIC cjson)
if(UNIX)
  target_link_libraries(rls_core PUBLIC pthread)
endif()

# ---------------------------------------------------------------------------
# Server executable
# ---------------------------------------------------------------------------

add_executable(raylib_server src/main.c src/commands.c)
target_include_directories(raylib_server PRIVATE src)
target_link_libraries(raylib_server PRIVATE rls_core raylib)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

enable_testing()

function(add_unit_test name source)
  add_executable(${name} tests/${source} ${UNITY_SRC})
  target_include_directories(${name} PRIVATE src ${unity_SOURCE_DIR}/src)
  target_link_libraries(${name} PRIVATE rls_core)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

add_unit_test(test_queue           test_queue.c)
add_unit_test(test_protocol        test_protocol.c)
add_unit_test(test_handle_registry test_handle_registry.c)
add_unit_test(test_b64             test_b64.c)

add_executable(test_integration
  tests/test_integration.c
  ${UNITY_SRC}
)
target_include_directories(test_integration PRIVATE src ${unity_SOURCE_DIR}/src)
target_link_libraries(test_integration PRIVATE rls_core)
add_test(NAME test_integration COMMAND test_integration)
set_tests_properties(test_integration PROPERTIES TIMEOUT 30)
