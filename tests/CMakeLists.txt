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

add_executable(test_mdns test_mdns.c)
target_link_libraries(test_mdns PRIVATE rcp::rcp unity)
target_compile_options(test_mdns PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_mdns COMMAND test_mdns)

add_executable(test_tls test_tls.c)
target_link_libraries(test_tls PRIVATE rcp::rcp unity)
target_compile_options(test_tls PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tls COMMAND test_tls)

add_executable(test_shmem test_shmem.c)
target_link_libraries(test_shmem PRIVATE rcp::rcp unity)
target_compile_options(test_shmem PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_shmem COMMAND test_shmem)

add_executable(test_loan test_loan.c)
target_link_libraries(test_loan PRIVATE rcp::rcp unity)
target_compile_options(test_loan PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_loan COMMAND test_loan)

add_executable(test_tsn test_tsn.c)
target_link_libraries(test_tsn PRIVATE rcp::rcp unity)
target_compile_options(test_tsn PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tsn COMMAND test_tsn)

add_executable(test_watchdog test_watchdog.c)
target_link_libraries(test_watchdog PRIVATE rcp::rcp unity)
target_compile_options(test_watchdog PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_watchdog COMMAND test_watchdog)
set_tests_properties(rcp_watchdog PROPERTIES TIMEOUT 30)

add_executable(test_deadline test_deadline.c)
target_link_libraries(test_deadline PRIVATE rcp::rcp unity)
target_compile_options(test_deadline PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_deadline COMMAND test_deadline)
set_tests_properties(rcp_deadline PROPERTIES TIMEOUT 30)

add_executable(test_powerstate test_powerstate.c)
target_link_libraries(test_powerstate PRIVATE rcp::rcp unity)
target_compile_options(test_powerstate PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_powerstate COMMAND test_powerstate)
set_tests_properties(rcp_powerstate PROPERTIES TIMEOUT 30)

add_executable(test_e2e test_e2e.c)
target_link_libraries(test_e2e PRIVATE rcp::rcp unity)
target_compile_options(test_e2e PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_e2e COMMAND test_e2e)

add_executable(test_prioqueue test_prioqueue.c)
target_link_libraries(test_prioqueue PRIVATE rcp::rcp unity)
target_compile_options(test_prioqueue PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_prioqueue COMMAND test_prioqueue)
set_tests_properties(rcp_prioqueue PROPERTIES TIMEOUT 30)

add_executable(test_ratelimit test_ratelimit.c)
target_link_libraries(test_ratelimit PRIVATE rcp::rcp unity)
target_compile_options(test_ratelimit PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ratelimit COMMAND test_ratelimit)

add_executable(test_sim test_sim.c)
target_link_libraries(test_sim PRIVATE rcp::rcp unity)
target_compile_options(test_sim PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_sim COMMAND test_sim)
set_tests_properties(rcp_sim PROPERTIES TIMEOUT 30)

add_executable(test_faultinject test_faultinject.c)
target_link_libraries(test_faultinject PRIVATE rcp::rcp unity)
target_compile_options(test_faultinject PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_faultinject COMMAND test_faultinject)

add_executable(test_authz test_authz.c)
target_link_libraries(test_authz PRIVATE rcp::rcp unity)
target_compile_options(test_authz PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_authz COMMAND test_authz)
set_tests_properties(rcp_authz PROPERTIES TIMEOUT 30)

add_executable(test_firmware test_firmware.c)
target_link_libraries(test_firmware PRIVATE rcp::rcp unity)
target_compile_options(test_firmware PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_firmware COMMAND test_firmware)

add_executable(test_zonegroup test_zonegroup.c)
target_link_libraries(test_zonegroup PRIVATE rcp::rcp unity)
target_compile_options(test_zonegroup PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_zonegroup COMMAND test_zonegroup)
set_tests_properties(rcp_zonegroup PROPERTIES TIMEOUT 30)

add_executable(test_proxy test_proxy.c)
target_link_libraries(test_proxy PRIVATE rcp::rcp unity)
target_compile_options(test_proxy PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_proxy COMMAND test_proxy)

add_executable(test_redundancy test_redundancy.c)
target_link_libraries(test_redundancy PRIVATE rcp::rcp unity)
target_compile_options(test_redundancy PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_redundancy COMMAND test_redundancy)

add_executable(test_federation test_federation.c)
target_link_libraries(test_federation PRIVATE rcp::rcp unity)
target_compile_options(test_federation PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_federation COMMAND test_federation)

add_executable(test_observe test_observe.c)
target_link_libraries(test_observe PRIVATE rcp::rcp unity)
target_compile_options(test_observe PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_observe COMMAND test_observe)
set_tests_properties(rcp_observe PROPERTIES TIMEOUT 30)

add_executable(test_admin test_admin.c)
target_link_libraries(test_admin PRIVATE rcp::rcp unity)
target_compile_options(test_admin PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_admin COMMAND test_admin)
set_tests_properties(rcp_admin PROPERTIES TIMEOUT 30)

add_executable(test_record test_record.c)
target_link_libraries(test_record PRIVATE rcp::rcp unity)
target_compile_options(test_record PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_record COMMAND test_record)
set_tests_properties(rcp_record PROPERTIES TIMEOUT 30)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE rcp::rcp unity)
target_compile_options(test_config PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_config COMMAND test_config)
