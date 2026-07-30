/* SPDX-License-Identifier: MPL-2.0 */
/* c-rcp -- RELAY-conformant CLI entry point (RELAY spec §11, §13.2).
 *
 * Thin wrapper around rcp_cli_run(); all logic lives in <rcp/cli.h> so it
 * can be unit-tested without spawning a subprocess.
 */
#include <rcp/cli.h>

#include <stdio.h>

//cfusa:req REQ-CLI-006
int main(int argc, char **argv)
{
    return rcp_cli_run(argc - 1, argv + 1, stdout, stderr);
}
