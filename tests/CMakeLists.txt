# Test binaries get the same warning strictness as the library, minus C4127
# ("conditional expression is constant"): Unity's TEST_ASSERT_* macros wrap
# every assertion in an if(), and comparisons between compile-time constants
# (e.g. TEST_ASSERT_TRUE(RCP_PRIORITY_HIGH > RCP_PRIORITY_NORMAL) over two
# enum literals) are exactly what unit tests are for — MSVC's C4127 flags
# that pattern unconditionally, so it's noise here rather than signal.
# C4310 ("cast truncates constant value") is the same category of noise:
# TEST_ASSERT_EQUAL_HEX8(0xAA, ...) intentionally narrows an int literal
# >0x7F to uint8_t inside Unity's own macro expansion.
set(RCP_TEST_WARNING_FLAGS ${RCP_WARNING_FLAGS})
if(MSVC)
    list(APPEND RCP_TEST_WARNING_FLAGS /wd4127 /wd4310)
endif()

add_executable(test_core test_core.c)
target_link_libraries(test_core PRIVATE rcp::rcp unity)
target_compile_options(test_core PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_core COMMAND test_core)

add_executable(test_errors test_errors.c)
target_link_libraries(test_errors PRIVATE rcp::rcp unity)
target_compile_options(test_errors PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_errors COMMAND test_errors)

# test_platform reaches into the internal src/platform.h (not an installed
# public header) to unit-test the mutex/condvar/thread primitives directly,
# since no public rcp/*.h re-exports them.
add_executable(test_platform test_platform.c)
target_include_directories(test_platform PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_platform PRIVATE rcp::rcp unity)
target_compile_options(test_platform PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_platform COMMAND test_platform)
set_tests_properties(rcp_platform PROPERTIES TIMEOUT 30)

add_executable(test_mock test_mock.c)
target_link_libraries(test_mock PRIVATE rcp::rcp unity)
target_compile_options(test_mock PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_mock COMMAND test_mock)

add_executable(test_avtp test_avtp.c)
target_link_libraries(test_avtp PRIVATE rcp::rcp unity)
target_compile_options(test_avtp PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_avtp COMMAND test_avtp)

add_executable(test_acf test_acf.c)
target_link_libraries(test_acf PRIVATE rcp::rcp unity)
target_compile_options(test_acf PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_acf COMMAND test_acf)

add_executable(test_fragment test_fragment.c)
target_link_libraries(test_fragment PRIVATE rcp::rcp unity)
target_compile_options(test_fragment PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_fragment COMMAND test_fragment)

add_executable(test_lifecycle test_lifecycle.c)
target_link_libraries(test_lifecycle PRIVATE rcp::rcp unity)
target_compile_options(test_lifecycle PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_lifecycle COMMAND test_lifecycle)

add_executable(test_server test_server.c)
target_link_libraries(test_server PRIVATE rcp::rcp unity)
target_compile_options(test_server PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_server COMMAND test_server)

add_executable(test_regmap test_regmap.c)
target_link_libraries(test_regmap PRIVATE rcp::rcp unity)
target_compile_options(test_regmap PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_regmap COMMAND test_regmap)

add_executable(test_discovery test_discovery.c)
target_link_libraries(test_discovery PRIVATE rcp::rcp unity)
target_compile_options(test_discovery PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_discovery COMMAND test_discovery)

add_executable(test_ep_gpio test_ep_gpio.c)
target_link_libraries(test_ep_gpio PRIVATE rcp::rcp unity)
target_compile_options(test_ep_gpio PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_gpio COMMAND test_ep_gpio)

add_executable(test_ep_spi test_ep_spi.c)
target_link_libraries(test_ep_spi PRIVATE rcp::rcp unity)
target_compile_options(test_ep_spi PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_spi COMMAND test_ep_spi)

add_executable(test_ep_i2c test_ep_i2c.c)
target_link_libraries(test_ep_i2c PRIVATE rcp::rcp unity)
target_compile_options(test_ep_i2c PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_i2c COMMAND test_ep_i2c)

add_executable(test_ep_uart test_ep_uart.c)
target_link_libraries(test_ep_uart PRIVATE rcp::rcp unity)
target_compile_options(test_ep_uart PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_uart COMMAND test_ep_uart)

add_executable(test_ep_pwm test_ep_pwm.c)
target_link_libraries(test_ep_pwm PRIVATE rcp::rcp unity)
target_compile_options(test_ep_pwm PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_pwm COMMAND test_ep_pwm)

add_executable(test_ep_adc test_ep_adc.c)
target_link_libraries(test_ep_adc PRIVATE rcp::rcp unity)
target_compile_options(test_ep_adc PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_adc COMMAND test_ep_adc)

add_executable(test_ep_lin test_ep_lin.c)
target_link_libraries(test_ep_lin PRIVATE rcp::rcp unity)
target_compile_options(test_ep_lin PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_lin COMMAND test_ep_lin)

add_executable(test_ep_can test_ep_can.c)
target_link_libraries(test_ep_can PRIVATE rcp::rcp unity)
target_compile_options(test_ep_can PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_can COMMAND test_ep_can)

add_executable(test_ep_iseled test_ep_iseled.c)
target_link_libraries(test_ep_iseled PRIVATE rcp::rcp unity)
target_compile_options(test_ep_iseled PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_iseled COMMAND test_ep_iseled)

add_executable(test_ep_mdio test_ep_mdio.c)
target_link_libraries(test_ep_mdio PRIVATE rcp::rcp unity)
target_compile_options(test_ep_mdio PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_mdio COMMAND test_ep_mdio)

add_executable(test_power test_power.c)
target_link_libraries(test_power PRIVATE rcp::rcp unity)
target_compile_options(test_power PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_power COMMAND test_power)

add_executable(test_ep_wakeup test_ep_wakeup.c)
target_link_libraries(test_ep_wakeup PRIVATE rcp::rcp unity)
target_compile_options(test_ep_wakeup PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ep_wakeup COMMAND test_ep_wakeup)

add_executable(test_request_sequencer test_request_sequencer.c)
target_link_libraries(test_request_sequencer PRIVATE rcp::rcp unity)
target_compile_options(test_request_sequencer PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_sequencer COMMAND test_request_sequencer)

add_executable(test_request_compound test_request_compound.c)
target_link_libraries(test_request_compound PRIVATE rcp::rcp unity)
target_compile_options(test_request_compound PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_compound COMMAND test_request_compound)

add_executable(test_request_triggered test_request_triggered.c)
target_link_libraries(test_request_triggered PRIVATE rcp::rcp unity)
target_compile_options(test_request_triggered PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_triggered COMMAND test_request_triggered)

add_executable(test_request_chained test_request_chained.c)
target_link_libraries(test_request_chained PRIVATE rcp::rcp unity)
target_compile_options(test_request_chained PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_chained COMMAND test_request_chained)

add_executable(test_request_timed test_request_timed.c)
target_link_libraries(test_request_timed PRIVATE rcp::rcp unity)
target_compile_options(test_request_timed PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_timed COMMAND test_request_timed)

add_executable(test_request_cancel test_request_cancel.c)
target_link_libraries(test_request_cancel PRIVATE rcp::rcp unity)
target_compile_options(test_request_cancel PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_request_cancel COMMAND test_request_cancel)

add_executable(test_scheduler test_scheduler.c)
target_link_libraries(test_scheduler PRIVATE rcp::rcp unity)
target_compile_options(test_scheduler PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_scheduler COMMAND test_scheduler)

# End-to-end conditional-request dispatch: exercises the compound /
# compound-wait / triggered / timed / chained / cancellation request kinds
# through the real rcp_mock_server_dispatch()/_tick() path rather than
# through each module's own encode/decode functions in isolation.
add_executable(test_conditional_dispatch test_conditional_dispatch.c)
target_link_libraries(test_conditional_dispatch PRIVATE rcp::rcp unity)
target_compile_options(test_conditional_dispatch PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_conditional_dispatch COMMAND test_conditional_dispatch)

add_executable(test_udp test_udp.c)
target_link_libraries(test_udp PRIVATE rcp::rcp unity)
target_compile_options(test_udp PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_udp COMMAND test_udp)
set_tests_properties(rcp_udp PROPERTIES TIMEOUT 30)

add_executable(test_l2 test_l2.c)
target_link_libraries(test_l2 PRIVATE rcp::rcp unity)
target_compile_options(test_l2 PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_l2 COMMAND test_l2)

# l2_veth_roundtrip is a real-socket, real-privilege verification program,
# deliberately NOT wired into ctest (see its own file header): it needs
# CAP_NET_RAW/root and a pre-existing veth0/veth1 pair, neither of which
# every ctest environment has. Built here so it's compiled by every CI
# matrix leg (catching any compile regression immediately); only actually
# *run* by ci.yml's own dedicated, privileged "L2 transport (veth)" job.
add_executable(l2_veth_roundtrip l2_veth_roundtrip_test.c)
target_link_libraries(l2_veth_roundtrip PRIVATE rcp::rcp)
target_compile_options(l2_veth_roundtrip PRIVATE ${RCP_TEST_WARNING_FLAGS})

add_executable(test_mdns test_mdns.c)
target_link_libraries(test_mdns PRIVATE rcp::rcp unity)
target_compile_options(test_mdns PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_mdns COMMAND test_mdns)

add_executable(test_shmem test_shmem.c)
target_link_libraries(test_shmem PRIVATE rcp::rcp unity)
target_compile_options(test_shmem PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_shmem COMMAND test_shmem)

# test_loan no longer links legacy_mock.c: its milestone-80 ADAPT rebind
# drops the controller-wrapper shape entirely (loan.h's own file header),
# so it needs no legacy zone-controller double any more (contrast the
# not-yet-migrated satellites below, which still do).
add_executable(test_loan test_loan.c)
target_link_libraries(test_loan PRIVATE rcp::rcp unity)
target_compile_options(test_loan PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_loan COMMAND test_loan)

add_executable(test_tsn test_tsn.c)
target_link_libraries(test_tsn PRIVATE rcp::rcp unity)
target_compile_options(test_tsn PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tsn COMMAND test_tsn)


# test_watchdog/test_deadline/test_powerstate no longer link legacy_mock.c:
# their milestone-79 REPLACE rebinds all three off rcp_controller_t/
# rcp_zone_t entirely (watchdog.h/deadline.h/powerstate.h's own file
# headers), so none of them need the legacy zone-controller double any
# more (contrast the 14 not-yet-migrated satellites below, which still
# do).
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

# test_ratelimit/test_faultinject/test_authz/test_observe/test_admin/
# test_recorder no longer link a zone-controller double: milestone 80's
# own ADAPT rebind moves all six off rcp_controller_t/rcp_zone_t entirely
# (each module's own file header). (prioqueue/firmware/zonegroup/proxy/
# redundancy/federation -- the six satellites that still needed the
# legacy mock -- were DEPRECATE-removed outright at milestone 83 rather
# than migrated, per the Satellite Disposition table; adapt.c REPLACEd its
# own rcp_controller_t dependency at milestone 84 and moved off it too.
# The mock double itself (tests/legacy_mock.*) and its own
# tests/benchmarks (test_rcp, test_wire, test_sim, test_legacy_mock,
# bench_mock, command_latency_test) were removed entirely along with the
# retired rcp.h object model and the wire.c/sim.c placeholder codec and
# simulator they backed -- see ROADMAP.md's retired-protocol-removal
# milestone.)
add_executable(test_ratelimit test_ratelimit.c)
target_link_libraries(test_ratelimit PRIVATE rcp::rcp unity)
target_compile_options(test_ratelimit PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ratelimit COMMAND test_ratelimit)

add_executable(test_faultinject test_faultinject.c)
target_link_libraries(test_faultinject PRIVATE rcp::rcp unity)
target_compile_options(test_faultinject PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_faultinject COMMAND test_faultinject)

add_executable(test_authz test_authz.c)
target_link_libraries(test_authz PRIVATE rcp::rcp unity)
target_compile_options(test_authz PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_authz COMMAND test_authz)
set_tests_properties(rcp_authz PROPERTIES TIMEOUT 30)

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

add_executable(test_recorder test_recorder.c)
target_link_libraries(test_recorder PRIVATE rcp::rcp unity)
target_compile_options(test_recorder PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_recorder COMMAND test_recorder)
set_tests_properties(rcp_recorder PROPERTIES TIMEOUT 30)

add_executable(test_config test_config.c)
target_link_libraries(test_config PRIVATE rcp::rcp unity)
target_compile_options(test_config PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_config COMMAND test_config)

add_executable(test_grpcbridge test_grpcbridge.c)
target_link_libraries(test_grpcbridge PRIVATE rcp::rcp unity)
target_compile_options(test_grpcbridge PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_grpcbridge COMMAND test_grpcbridge)

add_executable(test_restbridge test_restbridge.c)
target_link_libraries(test_restbridge PRIVATE rcp::rcp unity)
target_compile_options(test_restbridge PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_restbridge COMMAND test_restbridge)

add_executable(test_someipbr test_someipbr.c)
target_link_libraries(test_someipbr PRIVATE rcp::rcp unity)
target_compile_options(test_someipbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_someipbr COMMAND test_someipbr)

add_executable(test_canbr test_canbr.c)
target_link_libraries(test_canbr PRIVATE rcp::rcp unity)
target_compile_options(test_canbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_canbr COMMAND test_canbr)

add_executable(test_ddsbr test_ddsbr.c)
target_link_libraries(test_ddsbr PRIVATE rcp::rcp unity)
target_compile_options(test_ddsbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_ddsbr COMMAND test_ddsbr)

add_executable(test_mqttbr test_mqttbr.c)
target_link_libraries(test_mqttbr PRIVATE rcp::rcp unity)
target_compile_options(test_mqttbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_mqttbr COMMAND test_mqttbr)

add_executable(test_linbr test_linbr.c)
target_link_libraries(test_linbr PRIVATE rcp::rcp unity)
target_compile_options(test_linbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_linbr COMMAND test_linbr)

add_executable(test_udsbr test_udsbr.c)
target_link_libraries(test_udsbr PRIVATE rcp::rcp unity)
target_compile_options(test_udsbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_udsbr COMMAND test_udsbr)

add_executable(test_doipbr test_doipbr.c)
target_link_libraries(test_doipbr PRIVATE rcp::rcp unity)
target_compile_options(test_doipbr PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_doipbr COMMAND test_doipbr)

# test_adapt no longer links legacy_mock.c: milestone 84's own REPLACE
# rebinds adapt.c off rcp_controller_t/rcp_zone_t entirely (adapt.h's own
# file header), onto rcp_avtp_transport_t (avtp.h) and shmem.h's
# rcp_shmem_avtp_pair_new() test double instead.
add_executable(test_adapt test_adapt.c)
target_link_libraries(test_adapt PRIVATE rcp::rcp unity)
target_compile_options(test_adapt PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_adapt COMMAND test_adapt)
set_tests_properties(rcp_adapt PROPERTIES TIMEOUT 30)

add_executable(test_cli test_cli.c)
target_link_libraries(test_cli PRIVATE rcp::rcp unity)
target_compile_options(test_cli PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_cli COMMAND test_cli)

# ── TC18 requirements-corpus completeness suites (v0.105.0) ──────────────────
#
# One suite per area of the spec-coverage-gap sweep that produced the
# `status`-bearing entries in .fusa-reqs.json. Two kinds of test live here:
# spec-literal assertions for clauses c-RCP does implement but never
# catalogued, and deviation-pinning assertions for clauses it does not --
# the latter assert the behaviour the code actually has, with a comment
# naming the TC18 clause it fails to meet, so a gap is a tested fact rather
# than an absence nobody can see.
add_executable(test_tc18_gaps_regmap test_tc18_gaps_regmap.c)
target_link_libraries(test_tc18_gaps_regmap PRIVATE rcp::rcp unity)
target_compile_options(test_tc18_gaps_regmap PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tc18_gaps_regmap COMMAND test_tc18_gaps_regmap)

add_executable(test_tc18_gaps_server test_tc18_gaps_server.c)
target_link_libraries(test_tc18_gaps_server PRIVATE rcp::rcp unity)
target_compile_options(test_tc18_gaps_server PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tc18_gaps_server COMMAND test_tc18_gaps_server)

add_executable(test_tc18_gaps_ep test_tc18_gaps_ep.c)
target_link_libraries(test_tc18_gaps_ep PRIVATE rcp::rcp unity)
target_compile_options(test_tc18_gaps_ep PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tc18_gaps_ep COMMAND test_tc18_gaps_ep)

add_executable(test_tc18_gaps_ep2 test_tc18_gaps_ep2.c)
target_link_libraries(test_tc18_gaps_ep2 PRIVATE rcp::rcp unity)
target_compile_options(test_tc18_gaps_ep2 PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tc18_gaps_ep2 COMMAND test_tc18_gaps_ep2)

add_executable(test_tc18_gaps_e2e test_tc18_gaps_e2e.c)
target_link_libraries(test_tc18_gaps_e2e PRIVATE rcp::rcp unity)
target_compile_options(test_tc18_gaps_e2e PRIVATE ${RCP_TEST_WARNING_FLAGS})
add_test(NAME rcp_tc18_gaps_e2e COMMAND test_tc18_gaps_e2e)
