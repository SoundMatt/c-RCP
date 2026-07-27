cmake_minimum_required(VERSION 3.16)

project(c-rcp
    VERSION 0.34.0
    DESCRIPTION "Remote Control Protocol for automotive zonal architecture (C)"
    HOMEPAGE_URL "https://github.com/SoundMatt/c-RCP"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags are applied per-target (not via global add_compile_options)
# so they never leak into vendored dependencies fetched via FetchContent
# (e.g. Unity) — CMake's directory-scoped COMPILE_OPTIONS would otherwise
# apply to every target configured afterward, including third-party sources
# we have no business holding to our own standard.
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    set(RCP_WARNING_FLAGS
        -Wall -Wextra -Wpedantic
        -Wshadow -Wcast-align -Wunused
        -Wstrict-prototypes -Wmissing-prototypes
        -Wno-unused-parameter
    )
elseif(MSVC)
    set(RCP_WARNING_FLAGS
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
    src/wire.c
    src/udp.c
    src/mdns.c
    src/tls.c
    src/shmem.c
    src/loan.c
    src/tsn.c
    src/watchdog.c
    src/deadline.c
    src/powerstate.c
    src/e2e.c
    src/prioqueue.c
    src/ratelimit.c
    src/sim.c
    src/faultinject.c
    src/authz.c
    src/firmware.c
    src/zonegroup.c
    src/proxy.c
    src/redundancy.c
    src/federation.c
    src/observe.c
    src/admin.c
    src/record.c
    src/config.c
    src/dyndata.c
    src/grpcbridge.c
    src/restbridge.c
    src/someipbr.c
    src/canbr.c
)
add_library(rcp::rcp ALIAS rcp)

target_include_directories(rcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rcp PUBLIC Threads::Threads)
target_compile_options(rcp PRIVATE ${RCP_WARNING_FLAGS})

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
