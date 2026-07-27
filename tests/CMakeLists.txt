# Test binaries get the same warning strictness as the library, minus C4127
# ("conditional expression is constant"): Unity's TEST_ASSERT_* macros wrap
# every assertion in an if(), and comparisons between compile-time constants
# (e.g. TEST_ASSERT_TRUE(RCP_PRIORITY_HIGH > RCP_PRIORITY_NORMAL) over two
# enum literals) are exactly what unit tests are for — MSVC's C4127 flags
# that pattern unconditionally, so it's noise here rather than signal.
set(RCP_TEST_WARNING_FLAGS ${RCP_WARNING_FLAGS})
if(MSVC)
    list(APPEND RCP_TEST_WARNING_FLAGS /wd4127)
endif()

add_executable(test_rcp test_rcp.c)
target_link_libraries(test_rcp PRIVATE rcp::rcp unity)
target_compile_options(test_rcp PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_core COMMAND test_rcp)

add_executable(test_mock test_mock.c)
target_link_libraries(test_mock PRIVATE rcp::rcp unity)
target_compile_options(test_mock PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_mock COMMAND test_mock)

add_executable(bench_mock bench_mock.c)
target_link_libraries(bench_mock PRIVATE rcp::rcp unity)
target_compile_options(bench_mock PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_bench COMMAND bench_mock)

add_executable(command_latency_test command_latency_test.c)
target_link_libraries(command_latency_test PRIVATE rcp::rcp unity)
target_compile_options(command_latency_test PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_latency COMMAND command_latency_test)
set_tests_properties(rcp_latency PROPERTIES TIMEOUT 60)

add_executable(test_wire test_wire.c)
target_link_libraries(test_wire PRIVATE rcp::rcp unity)
target_compile_options(test_wire PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_wire COMMAND test_wire)

add_executable(test_udp test_udp.c)
target_link_libraries(test_udp PRIVATE rcp::rcp unity)
target_compile_options(test_udp PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_udp COMMAND test_udp)
set_tests_properties(rcp_udp PROPERTIES TIMEOUT 30)
