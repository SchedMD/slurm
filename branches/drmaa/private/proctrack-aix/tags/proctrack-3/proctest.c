/*****************************************************************************\
 *  proctest.c - Process tracking kernel extension test for AIX. 
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
 *  the terms of the GNU General Pub0lic License as published by the Free
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


#include "proctrack.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/errno.h>

pid_t cpid;
int cpidStatus;

pid_t _start_and_register_child(uint32_t jobid);
pid_t _start_and_register_child_with_grandchildren(uint32_t jobid);
int _print_pids(uint32_t jobid);

main (int argc, char **argv) {
        int mypid = getpid();
	uint32_t jobid = (uint32_t)mypid;
        int rc;
        int i;
	pid_t child1, child2, child3;
        
        fprintf(stderr, "Mainline in %s\n", argv[0], mypid);
	fprintf(stderr, "proctrack version is %u\n", proctrack_version());
        
	child1 = _start_and_register_child(jobid);
	child2 = _start_and_register_child_with_grandchildren(jobid);
	child3 = _start_and_register_child(jobid);

        proctrack_dump_records();

	while ((rc = proctrack_job_unreg(&mypid)) == -1) {
		fprintf(stderr, "proctrack_job_unreg: rc = %d\n", rc);
		if (_print_pids(jobid) == -1)
			break;
		sleep(1);
	}
	fprintf(stderr, "proctrack_job_unreg: rc = %d\n", rc);

	fprintf(stderr, "Reaping child process %d\n", child1);
	waitpid(child1, NULL, 0);
	fprintf(stderr, "Reaping child process %d\n", child2);
	waitpid(child2, NULL, 0);
	fprintf(stderr, "Reaping child process %d\n", child3);
	waitpid(child3, NULL, 0);

        fprintf(stderr, "All children have exited (%d)\n", cpidStatus);
        proctrack_dump_records();
}

int _print_pids(uint32_t jobid)
{
	int npids = 8;
	int32_t *pids = NULL;
	int rc;
	int i;
	int len;

	len = sizeof(int32_t) * npids;
	fprintf(stderr, "len = %d\n", len);
	pids = (int32_t *)malloc(len);
	if (pids == NULL) {
		fprintf(stderr, "malloc FAILED!\n");
		return -1;
	}

	rc = proctrack_get_pids(jobid, npids, pids);
	if (rc == -1) {
		switch(errno) {
		case EFAULT:
			fprintf(stderr, "proctrack_get_pids failed: EFAULT\n");
			break;
		case EIO:
			fprintf(stderr, "proctrack_get_pids failed: EIO\n");
			break;
		case ENOMEM:
			fprintf(stderr, "proctrack_get_pids failed: ENOMEM\n");
			break;
		case ENOSPC:
			fprintf(stderr, "proctrack_get_pids failed: ENOSPC\n");
			break;
		default:
			perror("proctrack_get_pids failed");
			break;
		}
		free(pids);
		return -1;
	} 

	fprintf(stderr, "%d pids in job %u\n", rc, jobid);
	for (i = 0; i < rc && i < npids; i++) {
		fprintf(stderr, "  pids[%d] = %u\n", i, pids[i]);
	}

	free(pids);
	return 0;
}

pid_t _start_and_register_child(uint32_t jobid)
{
	int cpid;

        if (cpid = fork()) {
                fprintf(stderr, "Started child %d\n", cpid);
		proctrack_job_reg_pid((int *)&jobid, &cpid);
        } else {
		/* child */
		int pid = (int)getpid();
                sleep(5);
                exit(0);
	}

	return (pid_t)cpid;
}

pid_t _start_and_register_child_with_grandchildren(uint32_t jobid)
{
	int cpid;

        if (cpid = fork()) {
                fprintf(stderr, "Started child %d\n", cpid);
		proctrack_job_reg_pid((int *)&jobid, &cpid);
        } else {
		/* child */
		int pid;
		
		sleep(1);
		if (cpid = fork()) {
			fprintf(stderr, "Started grandchild %d\n", cpid);
			pid = (int)getpid();
			sleep(5);
			exit(0);
		} else {
			/* grandchild */
			pid = (int)getpid();
			sleep(10);
			exit(0);
		}

	}

	return (pid_t)cpid;
}
