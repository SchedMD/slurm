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

#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
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
	log_options_t opts = LOG_OPTS_STDERR_ONLY;
	log_init("sbcast", opts, SYSLOG_FACILITY_DAEMON, NULL);

	parse_command_line(argc, argv);
	if (params.verbose) {
		opts.stderr_level += params.verbose;
		log_alter(opts, SYSLOG_FACILITY_DAEMON, NULL);
	}

	/* validate the source file */
	if ((fd = open(params.src_fname, O_RDONLY)) < 0) {
		error("Can't open `%s`: %s", params.src_fname, 
			strerror(errno));
		exit(1);
	}
	if (fstat(fd, &f_stat)) {
		error("Can't stat `%s`: %s", params.src_fname,
			strerror(errno));
		exit(1);
	}
	verbose("modes    = %o", (unsigned int) f_stat.st_mode);
	verbose("uid      = %d", (int) f_stat.st_uid);
	verbose("gid      = %d", (int) f_stat.st_gid);
	verbose("atime    = %s", ctime(&f_stat.st_atime));
	verbose("mtime    = %s", ctime(&f_stat.st_mtime));
	verbose("ctime    = %s", ctime(&f_stat.st_ctime));
	verbose("size     = %ld", (long) f_stat.st_size);
	verbose("-----------------------------");

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
		error("Command only valid from within SLURM job");
		exit(1);
	}
	jobid = (uint32_t) atol(jobid_str);

	if (slurm_allocation_lookup(jobid, &alloc_resp) != SLURM_SUCCESS) {
		error("SLURM jobid %u lookup error: %s",
			jobid, slurm_strerror(slurm_get_errno()));
		exit(1);
	}

	verbose("node_list  = %s", alloc_resp->node_list);
	verbose("node_cnt   = %u", alloc_resp->node_cnt);
	/* also see alloc_resp->node_addr (array) */

	/* do not bother to release the return message,
	 * we need to preserve and use most of the information later */
}

/* load a buffer with data from the file to broadcast, 
 * return number of bytes read, zero on end of file */
static int _get_block(char *buffer, size_t buf_size)
{
	static int fd = 0;
	int buf_used = 0, rc;

	if (!fd) {
		fd = open(params.src_fname, O_RDONLY);
		if (!fd) {
			error("Can't open `%s`: %s", 
				params.src_fname, strerror(errno));
			exit(1);
		}
	}

	while (buf_size) {
		rc = read(fd, buffer, buf_size);
		if (rc == -1) {
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;
			error("Can't read `%s`: %s",
				params.src_fname, strerror(errno));
			exit(1);
		} else if (rc == 0) {
			debug("end of file reached");
			break;
		}

		buffer   += rc;
		buf_size -= rc;
		buf_used += rc;
	}
	return buf_used;
}

/* issue the RPC to ship the file's data */
static void _send_rpc(file_bcast_msg_t *bcast_msg)
{
#if 1
	slurm_msg_t msg;
	int rc;

	msg.msg_type = REQUEST_FILE_BCAST;
	msg.address = alloc_resp->node_addr[0];
	msg.data = bcast_msg;

	if (slurm_send_recv_rc_msg_only_one(&msg, &rc, 0)) {
		error("slurm_send_recv_rc_msg_only_one: %m");
		exit(1);
	}
#else
// This code will handle message fanout to multiple slurmd, not implemented yet
	int i, rc;

	/* use static structures for persistent communcations data */
	static slurm_msg_t *msg = NULL; /* array of message structs, one per node */
	static int *rc_array;
	static int node_cnt;

	if (!msg) {
		node_cnt = alloc_resp->node_cnt;
		msg = xmalloc(sizeof(slurm_msg_t) * node_cnt);
		rc_array = xmalloc(sizeof(int) * node_cnt);
		for (i = 0; i < node_cnt; i++) {
			msg[i].msg_type = REQUEST_FILE_BCAST;
			msg[i].address = alloc_resp->node_addr[i];
		}
		slurm_free_resource_allocation_response_msg(alloc_resp);
	}
	for (i = 0; i < node_cnt; i++)
		msg[i].data = bcast_msg;

	verbose("sending block %u with %u bytes to %d nodes", 
		bcast_msg->block_no, bcast_msg->block_len, node_cnt);
	//_p_send_recv_rc_msg(alloc_resp->node_cnt, msg, rc_array, 10);

	rc = 0;
	for (i = 0; i < node_cnt; i++) {
		if (rc_array[i]) {
			rc = rc_array[i];
			break;
		}
	}
#endif

	if (rc) {
		error("REQUEST_FILE_BCAST: %s", slurm_strerror(rc));
		exit(1);
	}
}

/* read and broadcast the file */
static void _bcast_file(void)
{
	int buf_size, size_read;
	file_bcast_msg_t bcast_msg;
	char *buffer;

	buf_size = MIN(SSIZE_MAX, (64 * 1024));
	buf_size = MIN(buf_size, f_stat.st_size);
	buffer = xmalloc(buf_size);

	bcast_msg.fname     = params.dst_fname;
	bcast_msg.block_no  = 0;
	bcast_msg.force     = params.force;
	bcast_msg.modes     = f_stat.st_mode;
	bcast_msg.uid       = f_stat.st_uid;
	bcast_msg.gid       = f_stat.st_gid;
	bcast_msg.data      = buffer;
	if (params.preserve) {
		bcast_msg.atime     = f_stat.st_atime;
		bcast_msg.mtime     = f_stat.st_mtime;
	} else {
		bcast_msg.atime     = 0;
		bcast_msg.mtime     = 0;
	}

	while ((bcast_msg.block_len = _get_block(buffer, buf_size))) {
		bcast_msg.block_no++;
		_send_rpc(&bcast_msg);
		if (bcast_msg.block_len < buf_size)
			break;	/* end of file */
	}
}
