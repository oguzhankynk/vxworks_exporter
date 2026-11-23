/*
 * vxrun_launcher - runs one RTP and reports how it ended.
 *
 * The VxWorks "cmd" shell does not expose the exit status of the process it
 * launches, so scripts/vxrun starts this launcher instead. The launcher spawns
 * the real program with the same arguments and environment, waits for it, and
 * prints a single machine-readable line that vxrun parses:
 *
 *     VXRUN-EXIT code=<n>       the program returned or called exit(n)
 *     VXRUN-EXIT signal=<n>     the program was killed by signal n
 *     VXRUN-EXIT spawn-failed   the program could not be started
 *
 * Usage (on the target): vxrun_launcher.vxe <program.vxe> [args...]
 */

#include <errno.h>
#include <rtpLib.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

extern char **environ;

#define VXRUN_PRIORITY 100
#define VXRUN_STACK_SIZE (1024 * 1024)

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <program.vxe> [args...]\n", argv[0]);
        printf("VXRUN-EXIT spawn-failed\n");
        return 2;
    }

    /* argv[1..] is the child's argv: the program path followed by its args. */
    RTP_ID child = rtpSpawn(argv[1], (const char **)&argv[1], (const char **)environ,
                            VXRUN_PRIORITY, VXRUN_STACK_SIZE, 0, 0);
    if (child == RTP_ID_ERROR) {
        fprintf(stderr, "vxrun_launcher: cannot spawn '%s': %s\n", argv[1], strerror(errno));
        printf("VXRUN-EXIT spawn-failed\n");
        return 1;
    }

    int status = 0;
    if (waitpid((pid_t)(long)child, &status, 0) < 0) {
        fprintf(stderr, "vxrun_launcher: waitpid failed: %s\n", strerror(errno));
        printf("VXRUN-EXIT spawn-failed\n");
        return 1;
    }

    fflush(stdout);
    if (WIFEXITED(status)) {
        printf("VXRUN-EXIT code=%d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        printf("VXRUN-EXIT signal=%d\n", WTERMSIG(status));
    } else {
        printf("VXRUN-EXIT code=%d\n", status != 0 ? 1 : 0);
    }
    return 0;
}
