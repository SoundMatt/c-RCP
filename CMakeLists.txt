cmake_minimum_required(VERSION 3.16)

project(c-rcp
    VERSION 0.231.0
    DESCRIPTION "OPEN Alliance TC18 Remote Control Protocol (C)"
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
    src/errors.c
    src/mock.c
    src/avtp.c
    src/acf.c
    src/fragment.c
    src/lifecycle.c
    src/server.c
    src/regmap.c
    src/respqueue.c
    src/discovery.c
    src/ep_gpio.c
    src/ep_spi.c
    src/ep_i2c.c
    src/ep_uart.c
    src/ep_pwm.c
    src/ep_adc.c
    src/ep_lin.c
    src/ep_can.c
    src/ep_iseled.c
    src/ep_mdio.c
    src/ep_wakeup.c
    src/power.c
    src/request_sequencer.c
    src/request_compound.c
    src/request_triggered.c
    src/request_chained.c
    src/request_timed.c
    src/request_cancel.c
    src/scheduler.c
    src/udp.c
    src/l2.c
    src/mdns.c
    src/shmem.c
    src/loan.c
    src/tsn.c
    src/watchdog.c
    src/deadline.c
    src/powerstate.c
    src/e2e.c
    src/ratelimit.c
    src/faultinject.c
    src/authz.c
    src/observe.c
    src/admin.c
    src/recorder.c
    src/config.c
    src/grpcbridge.c
    src/restbridge.c
    src/someipbr.c
    src/canbr.c
    src/ddsbr.c
    src/mqttbr.c
    src/linbr.c
    src/udsbr.c
    src/doipbr.c
    src/relay.c
    src/adapt.c
    src/cli.c
)
add_library(rcp::rcp ALIAS rcp)

target_include_directories(rcp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rcp PUBLIC Threads::Threads)
target_compile_options(rcp PRIVATE ${RCP_WARNING_FLAGS})

# ── RELAY-conformant CLI (spec §11, §13.2) ───────────────────────────────────
#
# rcp_cli_run()'s logic (src/cli.c) is always compiled into the rcp library
# above, so it's unit-testable without this option. This option gates only
# the standalone c-rcp BINARY target, per spec §17.7: "A library that does
# not ship a standalone binary by default MUST still expose them through a
# CLI target built with -DRELAY_BUILD_CLI=ON (or the language's equivalent
# build option)."
option(RELAY_BUILD_CLI "Build the c-rcp RELAY-conformant CLI (spec §11)" OFF)
if(RELAY_BUILD_CLI)
    add_executable(c-rcp cli/main.c)
    target_link_libraries(c-rcp PRIVATE rcp::rcp)
    target_compile_options(c-rcp PRIVATE ${RCP_WARNING_FLAGS})
    set_target_properties(c-rcp PROPERTIES OUTPUT_NAME c-rcp)
endif()

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
