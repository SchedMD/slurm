/*****************************************************************************\
 *  sbcast.c - Broadcast a file to allocated nodes
 *
 *  $Id: sbcast.c 6965 2006-01-04 23:31:07Z jette $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  UCRL-CODE-217948.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <slurm/slurm_errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/sbcast/sbcast.h"

/* global variables */
int fd;						/* source file descriptor */
struct sbcast_parameters params;		/* program parameters */
struct stat f_stat;				/* source file stats */
resource_allocation_response_msg_t *alloc_resp;	/* job specification */

static void _bcast_file(void);
static void _get_job_info(void);


int main(int argc, char *argv[])
{
	parse_command_line(argc, argv);

	/* validate the source file */
	if ((fd = open(params.src_fname, O_RDONLY)) < 0) {
		fprintf(stderr, "Can't open `%s`: %s\n", params.src_fname, 
			strerror(errno));
		exit(1);
	}
	if (fstat(fd, &f_stat)) {
		fprintf(stderr, "Can't stat `%s`: %s\n", params.src_fname,
			strerror(errno));
		exit(1);
	}
	if (params.verbose) {
		printf("modes    = %o\n", (unsigned int) f_stat.st_mode);
		printf("uid      = %d\n", (int) f_stat.st_uid);
		printf("gid      = %d\n", (int) f_stat.st_gid);
		printf("atime    = %s", ctime(&f_stat.st_atime));
		printf("mtime    = %s", ctime(&f_stat.st_mtime));
		printf("ctime    = %s", ctime(&f_stat.st_ctime));
		printf("size     = %ld\n", (long) f_stat.st_size);
		printf("-----------------------------\n");
	}

	/* identify the nodes allocated to the job */
	_get_job_info();

	/* transmit the file */
	_bcast_file();

	exit(0);
}

/* get details about this slurm job: jobid and allocated node */
static void _get_job_info(void)
{
	char *jobid_str;
	uint32_t jobid;

	jobid_str = getenv("SLURM_JOBID");
	if (!jobid_str) {
		fprintf(stderr, "Command only valid from within SLURM job\n");
		exit(1);
	}
	jobid = (uint32_t) atol(jobid_str);

	if (slurm_allocation_lookup(jobid, &alloc_resp) != SLURM_SUCCESS) {
		fprintf(stderr, "SLURM jobid %u lookup error: %s\n",
			jobid, slurm_strerror(slurm_get_errno()));
		exit(1);
	}

	if (params.verbose) {
		printf("node_list  = %s\n", alloc_resp->node_list);
		printf("node_cnt   = %u\n", alloc_resp->node_cnt);
		/* also see alloc_resp->node_addr (array) */
	}

	/* do not bother to release the return message,
	 * we need to preserve and use most of the information later */
}

/* broadcast the file */
static void _bcast_file(void)
{
	int buf_size, size_read;
	char *buffer;

	buf_size = MIN(SSIZE_MAX, (16 * 1024));
	buffer = xmalloc(buf_size);

	size_read = read(fd, buffer, buf_size);
	/* get source for compress and re-use */
}
