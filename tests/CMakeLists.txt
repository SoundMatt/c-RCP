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
