add_executable(test_rcp test_rcp.c)
target_link_libraries(test_rcp PRIVATE rcp::rcp unity)
add_test(NAME rcp_core COMMAND test_rcp)

add_executable(test_mock test_mock.c)
target_link_libraries(test_mock PRIVATE rcp::rcp unity)
add_test(NAME rcp_mock COMMAND test_mock)
