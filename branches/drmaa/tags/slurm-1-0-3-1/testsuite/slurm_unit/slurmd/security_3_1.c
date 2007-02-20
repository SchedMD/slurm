/*****************************************************************************\
 *  security_3_1.c - Spawn a bogus batch job launch request (not from 
 *  privileged user). Make sure that slurmd rejects the request and logs it.
 *  Much of this code is lifted from slurmctld/job_scheduler.c:_launch_job
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et.al.
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include "config.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"

static void _detailed_logs(char *prog_name);
static int  _report_results(slurm_msg_t *response_msg_ptr);
static int  _send_launch_msg(batch_job_launch_msg_t *launch_msg_ptr);
static void _usage(char *prog_name);

int main(int argc, char *argv[])
{
	batch_job_launch_msg_t launch_msg;
	int uid;
	uint32_t jid, cpu_arr[1];

	if (argc != 2) {
		_usage(argv[0]);
		exit(1);
	}

	_detailed_logs(argv[0]);
	uid = getuid();
	jid = 0xffffff;
	printf("Trying to run job %d on node %s as user %u\n",
		jid, argv[1], uid);

	/* Initialization of data structures */
	launch_msg.job_id	= jid;
	launch_msg.uid		= uid;
	launch_msg.nodes	= argv[1];
	launch_msg.num_cpu_groups	= 1;
	cpu_arr[0]			= 1;
	launch_msg.cpus_per_node	= cpu_arr;
	launch_msg.cpu_count_reps	= cpu_arr;
	launch_msg.err		= "/dev/null";
	launch_msg.in		= "/dev/null";
	launch_msg.out		= "/dev/null";
	launch_msg.work_dir	= "/tmp";
	launch_msg.argc		= 0;
	launch_msg.argv		= NULL;
	launch_msg.script	= "/bin/hostname\n";
	launch_msg.envc 	= 0;
	launch_msg.environment 	= NULL;

	if (_send_launch_msg(&launch_msg) == SLURM_SUCCESS) {
		printf("Now check SlurmdLog for an error message.\n");
		exit(0);
	} else
		exit(1);
}

static int _send_launch_msg(batch_job_launch_msg_t *launch_msg_ptr)
{
	short int slurmd_port = slurm_get_slurmd_port();
	slurm_addr slurm_address;
	slurm_fd sockfd;
	slurm_msg_t request_msg;
	slurm_msg_t response_msg;
	int msg_size = 0;

	if (slurm_api_set_default_config() != SLURM_SUCCESS) {
		slurm_perror("slurm_api_set_default_config");
		return SLURM_ERROR;
	}
	slurm_set_addr(&slurm_address, slurmd_port, launch_msg_ptr->nodes);

	/* init message connection for message communication */
	if ((sockfd = slurm_open_msg_conn(&slurm_address))
	    == SLURM_SOCKET_ERROR) {
		slurm_perror("slurm_open_msg_conn");
		return SLURM_ERROR;
	}

	/* send request message */
	request_msg.msg_type	= REQUEST_BATCH_JOB_LAUNCH;
	request_msg.data	= launch_msg_ptr;
	request_msg.address	= slurm_address;
	if (slurm_send_node_msg(sockfd, &request_msg) == SLURM_SOCKET_ERROR) {
		slurm_perror("slurm_send_node_msg");
		return SLURM_ERROR;
	}

	/* receive message */
	if ((msg_size = slurm_receive_msg(sockfd, &response_msg, 0))
	     == SLURM_SOCKET_ERROR) {
		slurm_perror("slurm_receive_msg");
		return SLURM_ERROR;
	}

	/* shutdown message connection */
	if (slurm_shutdown_msg_conn(sockfd) == SLURM_SOCKET_ERROR) {
		slurm_perror("slurm_shutdown_msg_conn");
		return SLURM_ERROR;
	}

	return _report_results(&response_msg);
}

static int _report_results(slurm_msg_t *response_msg_ptr)
{
	return_code_msg_t *slurm_rc_msg_ptr;

	if (response_msg_ptr->msg_type != RESPONSE_SLURM_RC) {
		fprintf(stderr, "Wrong response type: %u\n", 
			response_msg_ptr->msg_type);
		return SLURM_ERROR;
	}

	slurm_rc_msg_ptr = (return_code_msg_t *) response_msg_ptr->data;
	if (slurm_rc_msg_ptr->return_code != ESLURM_USER_ID_MISSING) {
		fprintf(stderr, "Wrong response code: %u\n", 
			slurm_rc_msg_ptr->return_code);
		return SLURM_ERROR;
	}

	printf("Authentication failure (as expected).\n");
	return SLURM_SUCCESS;
}

static void _detailed_logs(char *prog_name) 
{
#if DEBUG
	log_options_t logopts = LOG_OPTS_STDERR_ONLY;
	logopts.stderr_level = LOG_LEVEL_DEBUG3;
	log_init(prog_name, logopts, SYSLOG_FACILITY_DAEMON, NULL);
#endif
}

static void _usage(char *prog_name)
{
	printf("Usage: %s host_name\n", prog_name);
}
