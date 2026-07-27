cmake_minimum_required(VERSION 3.16)

project(c-rcp
    VERSION 0.1.0
    DESCRIPTION "Remote Control Protocol for automotive zonal architecture (C)"
    HOMEPAGE_URL "https://github.com/SoundMatt/c-RCP"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow -Wcast-align -Wunused
        -Wstrict-prototypes -Wmissing-prototypes
        -Wno-unused-parameter
    )
endif()
if(MSVC)
    add_compile_options(
        /W4 /WX
        /wd4100   # unreferenced formal parameter
        /wd4996
    )
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(Threads REQUIRED)

# ── Core library ──────────────────────────────────────────────────────────────
add_library(rcp STATIC
    src/clock.c
    src/platform.c
    src/rcp.c
    src/mock.c
)
add_library(rcp::rcp ALIAS rcp)

target_include_directories(rcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rcp PUBLIC Threads::Threads)

# ── Tests ─────────────────────────────────────────────────────────────────────
option(RCP_BUILD_TESTS "Build tests" ON)
if(RCP_BUILD_TESTS)
    include(cmake/FetchDeps.cmake)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(TARGETS rcp EXPORT rcpTargets)
install(EXPORT rcpTargets
    NAMESPACE rcp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rcp
)

message(STATUS "c-rcp v${PROJECT_VERSION} — C${CMAKE_C_STANDARD} / ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
