/*****************************************************************************\
 *  get_nodes.c - Process Wiki get node info request
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

#include "./msg.h"

/* RET 0 on success, -1 on failure */
extern int	get_nodes(char *cmd_ptr, slurm_fd fd)
{
#if 0
	char *arg_ptr, *task_ptr, *node_ptr, *tmp_char;
	int i;
	uint32_t jobid;
	hostlist_t hl;
	char host_string[1024];
	static char reply_msg[128];

	arg_ptr = strstr(cmd_ptr, "ARG=");
	if (arg_ptr == NULL) {
		*err_code = 300;
		*err_msg = "STARTJOB lacks ARG";
		error("wiki: STARTJOB lacks ARG");
		return -1;
	}
	jobid = strtol(arg_ptr+4, &tmp_char, 10);
	if (!isspace(tmp_char[0])) {
		*err_code = 300;
		*err_msg = "Invalid ARG value";
		error("wiki: STARTJOB has invalid jobid");
		return -1;
	}

	task_ptr = strstr(cmd_ptr, "TASKLIST=");
	if (task_ptr == NULL) {
		*err_code = 300;
		*err_msg = "STARTJOB lacks TASKLIST";
		error("wiki: STARTJOB lacks TASKLIST");
		return -1;
	}
	node_ptr = task_ptr + 9;
	for (i=0; node_ptr[i]!='\0'; i++) {
		if (node_ptr[i] == ':')
			node_ptr[i] = ',';
	}
	hl = hostlist_create(node_ptr);
	i = hostlist_ranged_string(hl, sizeof(host_string), host_string);
	hostlist_destroy(hl);
	if (i < 0) {
		*err_code = 300;
		*err_msg = "STARTJOB has invalid TASKLIST";
		error("wiki: STARTJOB has invalid TASKLIST");
		return -1;
	}
	if (sched_set_nodelist(jobid, host_string) != SLURM_SUCCESS) {
		*err_code = 734;
		*err_msg = "failed to assign nodes";
		error("wiki: failed to assign nodes to job %u", jobid);
		return -1;
	}

	if (sched_start_job(jobid, (uint32_t) 1) != SLURM_SUCCESS) {
		*err_code = 730;
		*err_msg = "failed to start job";
		error("wiki: failed to start job %u", jobid);
		return -1;
	}

	snprintf(reply_msg, sizeof(reply_msg), 
		"job %u started successfully", jobid);
	*err_msg = reply_msg;
#endif
	return 0;
}
