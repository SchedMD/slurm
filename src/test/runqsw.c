/*
 * $Id$
 * $Source$
 *
 * Demo the routines in common/qsw.c
 * This can run mping on the local node (uses shared memory comms).
 * ./runqsw /usr/lib/mpi-test/mping 1 1024
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include <src/common/bitstring.h>
#include <src/common/qsw.h>

/*
 * Set a variable in the callers environment.  Args are printf style.
 * XXX Space is allocated on the heap and will never be reclaimed.
 * Example: setenvf("RMS_RANK=%d", rank);
 */
static int
setenvf(const char *fmt, ...) 
{
	va_list ap;
	char buf[BUFSIZ];
	char *bufcpy;
		    
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	bufcpy = strdup(buf);
	if (bufcpy == NULL)
		return -1;
	return putenv(bufcpy);
}

/*
 * Set environment variables needed by QSW MPICH / libelan.
 */
static int
do_env(int nodeid, int procid, int nprocs)
{
	if (setenvf("RMS_RANK=%d", procid) < 0)
		return -1;
	if (setenvf("RMS_NODEID=%d", nodeid) < 0)
		return -1;
	if (setenvf("RMS_PROCID=%d", procid) < 0)
		return -1;
	if (setenvf("RMS_NNODES=%d", 1) < 0)
		return -1;
	if (setenvf("RMS_NPROCS=%d", nprocs) < 0)
		return -1;
	return 0;
}

/*
 * Set up and run 'nprocs' copies of the parallel job.
 */
void
slurmd(qsw_jobinfo_t job, uid_t uid, int nodeid, int nprocs, char *cmdbuf)
{
	pid_t pid;
	int cpid[QSW_MAX_PROCS];
	int i;

	/* Process 1: */
	switch ((pid = fork())) {
		case -1:
			perror("fork");
			exit(1);
		case 0: /* child falls thru */
			break;
		default: /* parent */
			if (waitpid(pid, NULL, 0) < 0) {
				perror("wait");
				exit(1);
			}
			if (qsw_prog_reap(job) < 0) {
				perror("qsw_prog_reap");
				exit(1);
			}
			exit(0);
	}

	/* Process 2: */
	if (qsw_prog_init(job, uid) < 0) {
		perror("qsw_prog_init");
		exit(1);
	}
	for (i = 0; i < nprocs; i++) {
		cpid[i] = fork();
		if (cpid[i] < 0) {
			perror("fork");
			exit(1);
		} else if (cpid[i] == 0)
			break;
	}
	/* still in parent */
	if (i == nprocs) {
		int waiting = nprocs;
		int j;

		while (waiting > 0) {
			pid = waitpid(0, NULL, 0);
			if (pid < 0) {
				perror("waitpid");
				exit(1);
			}
			for (j = 0; j < nprocs; j++) {
				if (cpid[j] == pid)
					waiting--;
			}
		}
		exit(0);
	}

	/* Process 3: (there are nprocs instances of us) */
	if (qsw_attach(job, i) < 0) {
		perror("qsw_attach");
		exit(1);
	}
	if (do_env(i, nodeid, nprocs) < 0) {
		perror("do_env");
		exit(1);
	}

	pid = fork();
	switch (pid) {
		case -1:        /* error */
			perror("fork");
			exit(1);
		case 0:         /* child falls thru */
			break;
		default:        /* parent */
			if (waitpid(pid, NULL, 0) < 0) {
				perror("waitpid");
				exit(1);
			}
			exit(0);
	}
	/* child continues here */

	/* Process 4: execs the job */
	if (setuid(uid) < 0) {
		perror("setuid");
		exit(1);
	}
	execl("/bin/bash", "bash", "-c", cmdbuf, 0);
	perror("execl");
	exit(1);
}

/*
 * Print usage message and exit.
 */
void
usage(void)
{
	printf("Usage: runqsw [-u uid] [-i elanid] [-n nprocs] exec args\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	extern char *optarg;
	qsw_jobinfo_t job;
	int c;
	int nprocs = 0;
	int nodeid = qsw_getnodeid();
	uid_t uid = getuid();
	bitstr_t bit_decl(nodeset, 128);
	char cmdbuf[1024] = { 0 }; 

	/*
	 * Handle arguments.
	 */
	while ((c = getopt(argc, argv, "i:u:n:")) != EOF) {
		switch (c) {
			case 'n':
				nprocs = atoi(optarg);
				break;
			case 'u':
				uid = atoi(optarg);
				break;
			case 'i':
				nodeid = atoi(optarg);
				break;
			default:
				usage();
		}
	}
	if (optind == argc)
		usage();
	if (nprocs == 0)
		nprocs = 2;
	while (optind < argc)
		sprintf(cmdbuf + strlen(cmdbuf), "%s ", argv[optind++]);
	cmdbuf[strlen(cmdbuf) - 1] = '\0';
	bit_set(nodeset, nodeid);

	/*
	 * Set up 'job' to describe the parallel program.
	 * Srun would do this when running without slurmctld,
	 * otherwise slurmctld would do this after having calling
	 * qsw_init to establish a persistant state in the library.
	 */
	if (qsw_alloc_jobinfo(&job) < 0) {
		perror("qsw_alloc_jobinfo");
		exit(1);
	}
	if (qsw_setup_jobinfo(job, nprocs, nodeset, 0) < 0) {
		perror("qsw_setup_jobinfo");
		exit(1);
	}

	/* 
	 * Now execute the parallel job like slurmd will.
	 */
	slurmd(job, uid, nodeid, nprocs, cmdbuf);

	/*
	 * Free the 'job' information.
	 */
	qsw_free_jobinfo(job);

	exit(0);
}
