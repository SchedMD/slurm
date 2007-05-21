/*
 *	Convex SPP
 *	Copyright 1995 Convex Computer Corp.
 *	$CHeader: cnxCxdb.c 1.1 1995/11/08 13:59:56 $
 *
 *	Function:	- start SPP debugger
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>

/*
 * global variables
 */
volatile int		MPI_DEBUG_CONT = 0;

/*
 * external variables
 */
extern int		cnx_debug;

/*
 *	cnx_start_tool
 *
 *	Function:	- start tool (typically cxdb)
 *			- abort if fork/exec errors
 *			- not a generic function yet, have
 *			  to generalize the tool arguments
 *	Accepts:	- tool
 *			- program
 *	Returns:	- 0
 */
int
cnx_start_tool(tool, prog)

char			*tool;
char			*prog;

{
	int		pid;
	int		i;
	char		tempstring[20];
	char		*args[6];

	pid = fork();

	if (pid < 0) {
		perror("cnx_start_tool (fork)");
		exit(1);
	}
	else if (pid > 0) {				/* parent */
/*
 * For now, put a hack for cxdb.
 */
		if (strcmp(tool, "cxdb") == 0) {
			args[0] = "/usr/convex/bin/cxdb";
			args[1] = "-a";
			sprintf(tempstring, "%ld", pid);
			args[2] = tempstring;
			args[3] = "-e";
			args[4] = prog;
			args[5] = 0;
		} else {
			args[0] = tool;
			args[1] = prog;
			args[2] = 0;
		}

		if (cnx_debug) {
			fprintf(stderr, "starting %s with args: ", tool);
			for (i = 0; args[i]; ++i) {
				fprintf(stderr, "%s ", args[i]);
			}
			fprintf(stderr, "\n");
		}

		if (execvp(args[0], args) < 0) {
			perror("cnx_start_tool (execvp)");
			kill(pid, 9);
			exit(1);
		}
	}
	else {						/* child */
/*
 * Catch the process here.
 */
		while (MPI_DEBUG_CONT == 0);
	}

	return(0);
}
