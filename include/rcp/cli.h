/*
 * RELAY-conformant CLI for c-RCP (RELAY spec §11 CLI contract, §12
 * capability docs).
 *
 * Implements the three mandatory commands -- version, capabilities,
 * status -- emitting JSON that conforms to the RELAY spec/schemas/cli-*.json
 * schemas so `relay conform` can validate the binary.
 *
 * The logic lives here (rcp_cli_run()) so it is unit-testable without
 * spawning a subprocess; cli/main.c is a thin wrapper around it.
 *
 * Binary name: c-rcp (§13.2). Build with -DRELAY_BUILD_CLI=ON.
 *
 * Scope note: cpp-RCP's own cli.hpp additionally implements an optional
 * `send --format json` streaming command (a full hand-rolled JSON parser
 * plus base64 decoder, for the §11.2 crossbar-spoke use case). That
 * command is not part of RELAY's mandatory conformance surface -- only
 * version/capabilities/status are required (§17 requirement 7) -- so this
 * port implements just those three plus --help, matching the actual P0
 * defect this milestone closes rather than importing unrequested scope.
 * capabilities' "commands" list reflects this honestly (no "send" entry).
 */
#ifndef RCP_CLI_H
#define RCP_CLI_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exit codes per RELAY spec §11.3. */
enum {
    RCP_CLI_OK           = 0,
    RCP_CLI_ERROR        = 1,
    RCP_CLI_INVALID_ARGS = 2,
};

/* Executes the CLI. argv[0] is the subcommand name (i.e. argv from main()
 * with argv[0]/the program name already stripped); argc is the number of
 * elements in argv. Writes the requested document to out, diagnostics to
 * err. Returns an exit code (RCP_CLI_* above). */
int rcp_cli_run(int argc, char **argv, FILE *out, FILE *err);

#ifdef __cplusplus
}
#endif

#endif /* RCP_CLI_H */
