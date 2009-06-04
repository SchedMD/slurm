/*****************************************************************************\
 *  sbcast.c - Broadcast a file to allocated nodes
 *****************************************************************************
 *  Copyright (C) 2006-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
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

#include "src/common/forward.h"
#include "src/common/hostlist.h"
#include "src/common/log.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_interface.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/sbcast/sbcast.h"

/* global variables */
int fd;					/* source file descriptor */
struct sbcast_parameters params;	/* program parameters */
struct stat f_stat;			/* source file stats */
job_sbcast_cred_msg_t *sbcast_cred;	/* job alloc info and sbcast cred */

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
/*	slurm_free_sbcast_cred_msg(sbcast_cred); */

	exit(0);
}

/* get details about this slurm job: jobid and allocated node */
static void _get_job_info(void)
{
	char *jobid_str;
	uint32_t jobid;

	jobid_str = getenv("SLURM_JOB_ID");
	if (!jobid_str) {
		error("Command only valid from within SLURM job");
		exit(1);
	}
	jobid = (uint32_t) atol(jobid_str);
	verbose("jobid      = %u", jobid);

	if (slurm_sbcast_lookup(jobid, &sbcast_cred) != SLURM_SUCCESS) {
		error("SLURM jobid %u lookup error: %s",
		      jobid, slurm_strerror(slurm_get_errno()));
		exit(1);
	}

	verbose("node_cnt   = %u", sbcast_cred->node_cnt);
	verbose("node_list  = %s", sbcast_cred->node_list);
	/* also see sbcast_cred->node_addr (array) */

	if (params.verbose)
		print_sbcast_cred(sbcast_cred->sbcast_cred);

	/* do not bother to release the return message,
	 * we need to preserve and use most of the information later */
}

/* load a buffer with data from the file to broadcast, 
 * return number of bytes read, zero on end of file */
static ssize_t _get_block(char *buffer, size_t buf_size)
{
	static int fd = 0;
	ssize_t buf_used = 0, rc;

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

/* read and broadcast the file */
static void _bcast_file(void)
{
	int buf_size;
	ssize_t size_read = 0;
	file_bcast_msg_t bcast_msg;
	char *buffer;

	if (params.block_size)
		buf_size = MIN(params.block_size, f_stat.st_size);
	else
		buf_size = MIN((512 * 1024), f_stat.st_size);

	bcast_msg.fname		= params.dst_fname;
	bcast_msg.block_no	= 1;
	bcast_msg.last_block	= 0;
	bcast_msg.force		= params.force;
	bcast_msg.modes		= f_stat.st_mode;
	bcast_msg.uid		= f_stat.st_uid;
	bcast_msg.gid		= f_stat.st_gid;
	buffer			= xmalloc(buf_size);
	bcast_msg.block		= buffer;
	bcast_msg.block_len	= 0;

	if (params.preserve) {
		bcast_msg.atime     = f_stat.st_atime;
		bcast_msg.mtime     = f_stat.st_mtime;
	} else {
		bcast_msg.atime     = 0;
		bcast_msg.mtime     = 0;
	}

	while (1) {
		bcast_msg.block_len = _get_block(buffer, buf_size);
		debug("block %d, size %u", bcast_msg.block_no,
		      bcast_msg.block_len);
		size_read += bcast_msg.block_len;
		if (size_read >= f_stat.st_size)
			bcast_msg.last_block = 1;
			
		send_rpc(&bcast_msg, sbcast_cred);
		if (bcast_msg.last_block)
			break;	/* end of file */
		bcast_msg.block_no++;
	}

	xfree(buffer);
}
