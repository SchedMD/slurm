/*****************************************************************************\
 *  start_proctrack.c - Process tracking kernel extension for AIX. 
 *  Keep track of process ancestry with respect to SLURM jobs.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/sysconfig.h>
#include <sys/file.h>
#include <errno.h>
#include "proctrack.h"
#include "proctrack_loader.h"

/* Extension loading data */
extern int sysconfig();
extern int errno;

#define NAME_SIZE 256
#define LIBPATH_SIZE 256
#define BUFLEN 4096
#define PROCTRACK_START 1
#define PROCTRACK_STOP 2

/* forward declarations */
int proctrack_cmd(int, mid_t, int);

void printUsage(char *p)
{
	fprintf(stderr,
		"Usage: %s: start -f[ilename] \"pathtofile\" -n[procs] N\n",
		p);
	fprintf(stderr, "           stop -f[ilename] \"pathtofile\"\n", p);
/* 	fprintf(stderr, "           version\n"); */
}

main(argc, argv)
int argc;
char *argv[];
{
	int rc, i;
	int foundStart = FALSE, foundStop = FALSE;
	int numprocs = 0;
	int int_cookie;		/* for sscanf */
	mid_t cookie = (mid_t) - 1;
	char *p = argv[0];
	char *filename = NULL;
	struct stat64 statBuf;

	/* 
	   parameters: start -f[ilename] "/path/to/proctrackect.ext" -n[procs] "intNumProcesses"
	   stop -f[ilename]"/path/to/proctrackect.ext"
	 */
	if (argc < 3) {
/* 		if ((argc == 2) && !strcmp(argv[1], "version")) { */
/* 			fprintf(stderr, */
/* 				"proctrack kernel extension version = %u\n", */
/* 				proctrack_version()); */
/* 			exit(0); */
/* 		} */
		printUsage(p);
		exit(-1);
	}
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "start")) {
			if ((filename != NULL) || numprocs != 0) {
				printUsage(p);
				exit(-1);
			}
			foundStart = TRUE;
			numprocs = 2048;
		} else if (!strcmp(argv[i], "stop")) {
			if (foundStart || (filename != NULL)
			    || numprocs != 0) {
				printUsage(p);
				exit(-1);
			}
			foundStop = TRUE;
		} else if (!strcmp(argv[i], "-f")
			   || !strcmp(argv[i], "-filename")) {
			if ((!foundStart && !foundStop) || i == argc) {
				fprintf(stderr,
					"%s: -f used before start/stop keyword\n",
					p);
				printUsage(p);
				exit(-1);
			}
			i++;
			filename = argv[i];
			if (lstat64(filename, &statBuf)) {
				perror(filename);
				printUsage(p);
				exit(-1);
			}
		} else if (!strcmp(argv[i], "-n")
			   || !strcmp(argv[i], "-nprocs")) {
			if (foundStart) {
				i++;
				if (sscanf(argv[i], "%d", &numprocs) != 1) {
					fprintf(stderr,
						"%s: Cannot interpred %s as number of proccesses\n",
						p, argv[i]);
					printUsage(p);
					exit(-1);
				}
			} else {
				printUsage(p);
				exit(-1);
			}
		} else {
			fprintf(stderr, "%s: don't grok: %s\n", p,
				argv[i]);
			printUsage(p);
			exit(-1);
		}
	}			/* for */
	if (foundStart) {
		if ((cookie = loadext(filename, TRUE, FALSE)) == 0) {
			perror(filename);
		} else {
			proctrack_cmd(PROCTRACK_START, cookie, numprocs);
		}
	} else if (foundStop) {
		cookie = loadext(filename, FALSE, TRUE);
		fprintf(stderr,
			"%s: looking up %s: cookie = %d (errno=%d)\n", p,
			filename, cookie, errno);
		proctrack_cmd(PROCTRACK_STOP, cookie, numprocs);
		cookie = loadext(filename, FALSE, FALSE);	/* unload */
	}
	exit(0);
}

int proctrack_cmd(int cmd, mid_t cookie, int numprocs)
{
	struct extparms extparms;
	struct cfg_kmod cfg_kmod;
	int rc;
	int buf[10];

	buf[0] = numprocs;

	/* init code */
	extparms.argc = 0;
	extparms.argv = (char **) NULL;

	extparms.buf = (char *) &buf[0];
	extparms.len = sizeof(int);

	cfg_kmod.kmid = cookie;
	cfg_kmod.cmd = cmd;	/* init */
	cfg_kmod.mdiptr = (char *) &extparms;
	cfg_kmod.mdilen = sizeof(extparms);

	if ((rc =
	     sysconfig(SYS_CFGKMOD, &cfg_kmod,
		       sizeof(cfg_kmod))) != CONF_SUCC)
		perror("proctract_cmd:");
	return (rc);
	fprintf(stderr, "proctrack_cmd: rc = %d\n");
}				/* proctract_cmd */

#if 0
/*
 * [UN]LOAD the kernel extension. - return the cookie
 */
int proctrack_kload(int cmd, mid_t cookie, char *path)
{
	char my_path[NAME_SIZE];
	char my_libpath[LIBPATH_SIZE];
	struct cfg_load cfg_load;
	int rc;

	memset(my_path, 0, sizeof(my_path));
	memset(my_libpath, 0, sizeof(my_libpath));
	strcpy(my_path, path);
	cfg_load.path = my_path;
	cfg_load.libpath = my_libpath;
	cfg_load.kmid = cookie;

	printf("proctrack_kload %d:%d:%s\n", cmd, cookie, path);
	return 0;
	if (sysconfig(cmd, &cfg_load, sizeof(cfg_load)) == CONF_SUCC) {
		printf
		    ("Kernel extension %s succesfully [un]loaded, kmid=%x\n",
		     cfg_load.path, cfg_load.kmid);
	} else {
		printf
		    ("Encountered errno=%d [un]loading kernel extension %s\n",
		     errno, cfg_load.path);
		perror("proctrack_kload");
		exit(errno);
	}
	return (int) cfg_load.kmid;
}				/* proctrack_kload */
#endif
