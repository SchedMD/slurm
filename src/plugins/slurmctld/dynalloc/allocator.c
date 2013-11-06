/*****************************************************************************\
 *  allocator.c  - dynamic resource allocation
 *****************************************************************************
 *  Copyright (C) 2012-2013 Los Alamos National Security, LLC.
 *  Written by Jimmy Cao <Jimmy.Cao@emc.com>, Ralph Castain <rhc@open-mpi.org>
 *  All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.h"
#include "allocate.h"
#include "info.h"
#include "argv.h"
#include "msg.h"
#include "constants.h"


static void _parse_job_params(const char *cmd, char *orte_jobid,
					char *return_flag,	size_t *job_timeout);

static void _parse_app_params(const char *cmd, char *appid,
					uint32_t *np, uint32_t *request_node_num,
					char *node_range_list, char *flag,
					char *cpu_bind, uint32_t *mem_per_cpu,
					uint32_t *resv_port_cnt);

static void _allocate_app_op(const char *msg_app,
							size_t app_timeout,
							char *app_resp_msg);

/*
 * Parse the job part of msg(cmd) to obtain job parameters
 *
 *	e.g., if a allocate request is like "allocate jobid=100
 *	return=all timeout=10:app=0 np=5 N=2 node_list=vm2,vm3
 *	flag=mandatory:app=1 N=2", then the job part of msg is
 *	"jobid=100 return=all timeout=10".
 *
 * IN:
 * 	cmd: job part of msg
 * OUT Parameter:
 * 	orte_jobid:
 * 	return_flag:
 * 	job_timeout: timeout of resource allocation for the whole job
 */
static void _parse_job_params(const char *cmd, char *orte_jobid,
					char *return_flag,	size_t *job_timeout)
{
	char *tmp = NULL;
	char *p_str = NULL;
	char *pos = NULL;

	tmp = xstrdup(cmd);
	p_str = strtok(tmp, " ");
	while (p_str) {
		if (strstr(p_str, "jobid")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
			strcpy(orte_jobid, pos);
		} else if (strstr(p_str, "return")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
			strcpy(return_flag, pos);
		} else if (strstr(p_str, "timeout")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
			*job_timeout = atol(pos);
		}
		p_str = strtok(NULL, " ");
	}

	/* cleanup */
	xfree(tmp);
}

/*
 * Parse the app part of msg(cmd) to obtain app parameters
 *
 *	e.g., if a allocate request is like "allocate jobid=100
 *	return=all timeout=10:app=0 np=5 N=2 node_list=vm2,vm3
 *	flag=mandatory:app=1 N=2", then the app part of msg is
 *	"app=0 np=5 N=2 node_list=vm2,vm3 flag=mandatory:app=1 N=2".
 *
 * IN:
 * 	cmd: app part of msg
 * OUT Parameter:
 * 	appid:
 * 	np: number of process
 * 	request_node_num:
 * 	node_range_list:
 * 	flag: mandatory or optional
 * 	cpu_bind: cpu bind type, e.g., cores
 * 	mem_per_cpu: memory per cpu (MB)
 */
static void _parse_app_params(const char *cmd, char *appid,
					uint32_t *np, uint32_t *request_node_num,
					char *node_range_list, char *flag,
					char *cpu_bind, uint32_t *mem_per_cpu,
					uint32_t *resv_port_cnt)
{
	char *tmp = NULL;
	char *p_str = NULL;
	char *pos = NULL;

	tmp = xstrdup(cmd);
	p_str = strtok(tmp, " ");
	while (p_str) {
		if (strstr(p_str, "app")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
			strcpy(appid, pos);
		} else if (strstr(p_str, "np")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
			*np = strtoul(pos, NULL, 10);
		} else if (strstr(p_str, "N=")) {
			pos =  strchr(p_str, '=');
			pos++;  /* step over the = */
			*request_node_num = strtoul(pos, NULL, 10);
		} else  if (strstr(p_str, "node_list")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
            strcpy(node_range_list, pos);
		} else  if (strstr(p_str, "flag")) {
			pos = strchr(p_str, '=');
			pos++;  /* step over the = */
            strcpy(flag, pos);
		} else if (strstr(p_str, "cpu_bind")) {
			pos = strchr(p_str, '=');
			pos++;
			strcpy(cpu_bind, pos);
		} else if (strstr(p_str, "mem_per_cpu")) {
			pos = strchr(p_str, '=');
			pos++;
			*mem_per_cpu = strtoul(pos, NULL, 10);
		} else if (strstr(p_str, "resv_port_cnt")) {
			pos = strchr(p_str, '=');
			pos++;
			*resv_port_cnt = strtoul(pos, NULL, 10);
		}
		p_str = strtok(NULL, " ");
	}

	/* cleanup */
	xfree(tmp);
}

/*
 * allocate resource for an app
 *
 * IN:
 * 	msg_app: cmd of allocation requirement
 * 	app_timeout:
 * OUT Parameter:
 * 	app_resp_msg: allocation result
 */
static void _allocate_app_op(const char *msg_app,
							size_t app_timeout,
							char *app_resp_msg)
{
	char appid[16];
	uint32_t  np = 0;
	uint32_t  request_node_num = 0;
	char node_range_list[SIZE] = "";
	char flag[16] = "mandatory";  /* if not specified, by default */

	char cpu_bind[32] = "";
	uint32_t mem_per_cpu = 0;
	uint32_t resv_port_cnt = 1;
	/* out params */
	uint32_t slurm_jobid;
	char resp_node_list[SIZE];
	char tasks_per_node[SIZE] = "";
	char resv_ports[SIZE] = "";
	int rc;

	_parse_app_params(msg_app, appid, &np, &request_node_num,
					node_range_list, flag, cpu_bind, &mem_per_cpu, &resv_port_cnt);

	rc = allocate_node(np, request_node_num, node_range_list, flag,
						app_timeout, cpu_bind, mem_per_cpu, resv_port_cnt,
						&slurm_jobid, resp_node_list, tasks_per_node, resv_ports);

	if (SLURM_SUCCESS == rc) {
		sprintf(app_resp_msg,
				"app=%s slurm_jobid=%u allocated_node_list=%s tasks_per_node=%s resv_ports=%s",
				appid, slurm_jobid, resp_node_list, tasks_per_node, resv_ports);
	} else {
		sprintf(app_resp_msg, "app=%s allocate_failure", appid);
	}
}

/*
 * allocate resources for a job.
 *
 * The job will consist of at least one app, e.g., "allocate
 * jobid=100 return=all timeout=10:app=0 np=5 N=2
 * node_list=vm2,vm3 flag=mandatory:app=1 N=2".
 *
 * IN:
 * 	new_fd: send allocation result to socket_fd
 * 	msg: resource requirement cmd
 */
extern void allocate_job_op(slurm_fd_t new_fd, const char *msg)
{
	char orte_jobid[16] = "";
	char return_flag[16] = "";
	size_t job_timeout = 15; /* if not specified, by default */

	char send_buf[SIZE];
	char **app_argv = NULL, **tmp_app_argv;
	size_t app_timeout;
	uint32_t app_count = 1;
	char app_resp_msg[SIZE];
	char **all_resp_msg_argv = NULL, **tmp_all_resp_msg_argv;

	app_argv = argv_split(msg, ':');
	/* app_count dose not include the first part (job info) */
	app_count = argv_count(app_argv) - 1;
	/* app_argv will be freed */
	tmp_app_argv = app_argv;
	while (*tmp_app_argv) {
		if (strstr(*tmp_app_argv, "allocate")) {
			_parse_job_params(*tmp_app_argv, orte_jobid,
								return_flag, &job_timeout);
		} else if (strstr(*tmp_app_argv, "app")) {
			app_timeout = job_timeout / app_count;

			_allocate_app_op(*tmp_app_argv, app_timeout, app_resp_msg);

			if (0 == strcmp(return_flag, "all")
					&& 0 != strlen(app_resp_msg)) {
				argv_append_nosize(&all_resp_msg_argv, app_resp_msg);
			} else if (0 != strlen(app_resp_msg)) {
				/* if return_flag != "all",
				 * each app's allocation will be sent individually */
				sprintf(send_buf, "jobid=%s:%s", orte_jobid, app_resp_msg);
				info("BBB: send to client: %s", send_buf);
				send_reply(new_fd, send_buf);
			}
		}
		tmp_app_argv++;
	}
	/* free app_argv */
	argv_free(app_argv);

	if (0 == strcmp(return_flag, "all")) {
		sprintf(send_buf, "jobid=%s", orte_jobid);
		/* all_resp_msg_argv will be freed */
		tmp_all_resp_msg_argv = all_resp_msg_argv;
		while (*tmp_all_resp_msg_argv) {
			sprintf(send_buf, "%s:%s", send_buf, *tmp_all_resp_msg_argv);
			tmp_all_resp_msg_argv++;
		}
		/* free all_resp_msg_argv */
		argv_free(all_resp_msg_argv);

		info("BBB: send to client: %s", send_buf);
		send_reply(new_fd, send_buf);
	}
}
