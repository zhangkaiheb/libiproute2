/* SPDX-License-Identifier: GPL-2.0 */
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "iprt.h"
#include "utils.h"

int cmd_exec(const char *cmd, char **argv, bool do_fork)
{
	fflush(stdout);
	if (do_fork) {
		int status;
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			perror("fork");
			iprt_exit(1);
		}

		if (pid != 0) {
			/* Parent  */
			if (waitpid(pid, &status, 0) < 0) {
				perror("waitpid");
				iprt_exit(1);
			}

			if (WIFEXITED(status)) {
				return WEXITSTATUS(status);
			}

			iprt_exit(1);
		}
	}

	if (execvp(cmd, argv)  < 0)
		fprintf(stderr, "exec of \"%s\" failed: %s\n",
				cmd, strerror(errno));
	_iprt_exit(1);
}
