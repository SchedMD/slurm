/*****************************************************************************\
 *  power_common.c - Common logic for power management
 *
 *  NOTE: These functions are designed so they can be used by multiple power
 *  management plugins at the same time, so the state information is largely in
 *  the individual plugin and passed as a pointer argument to these functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#define _GNU_SOURCE	/* For POLLRDHUP */
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__FreeBSD__) || defined(__NetBSD__)
#define POLLRDHUP POLLHUP
#include <signal.h>
#endif

#include "slurm/slurm.h"

#include "src/common/list.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/timers.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/slurmctld.h"

#include "power_common.h"

static void _job_power_del(void *x)
{
	xfree(x);
}

/* For all nodes in a cluster
 * 1) set default values and
 * 2) return global power allocation/consumption information */
extern void get_cluster_power(struct node_record *node_record_table_ptr,
			      int node_record_count,
			      uint32_t *alloc_watts, uint32_t *used_watts)
{
	uint64_t debug_flag = slurm_get_debug_flags();
	int i;
	struct node_record *node_ptr;

	*alloc_watts = 0;
	*used_watts  = 0;
	if ((debug_flag & DEBUG_FLAG_POWER) == 0)
		return;

	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		if (node_ptr->power) {
			if (!node_ptr->power->cap_watts) {	/* No limit */
				if (!node_ptr->power->max_watts)
					continue;	/* No node data */
				node_ptr->power->cap_watts =
					node_ptr->power->max_watts;
			}
			if (!node_ptr->power->current_watts) { /* No data yet */
				if (node_ptr->energy &&
				    node_ptr->energy->current_watts) {
					node_ptr->power->current_watts +=
						node_ptr->energy->current_watts;
				} else {
					node_ptr->power->current_watts =
						node_ptr->power->cap_watts;
				}
			}
			*alloc_watts += node_ptr->power->cap_watts;
			*used_watts += node_ptr->power->current_watts;
		}	
	}
}

/* For each running job, return power allocation/use information in a List
 * containing elements of type power_by_job_t.
 * NOTE: Job data structure must be locked on function entry
 * NOTE: Call list_delete() to free return value
 * NOTE: This function is currently unused. */
extern List get_job_power(List job_list,
			  struct node_record *node_record_table_ptr)
{
	struct node_record *node_ptr;
	struct job_record *job_ptr;
	ListIterator job_iterator;
	power_by_job_t *power_ptr;
	int i, i_first, i_last;
	uint64_t debug_flag = slurm_get_debug_flags();
	List job_power_list = list_create(_job_power_del);
	time_t now = time(NULL);

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr))
			continue;
		power_ptr = xmalloc(sizeof(power_by_job_t));
		power_ptr->job_id = job_ptr->job_id;
		power_ptr->start_time = job_ptr->start_time;
		list_append(job_power_list, power_ptr);
		if (!job_ptr->node_bitmap) {
			error("%s: %pJ node_bitmap is NULL",
			      __func__, job_ptr);
			continue;
		}
		i_first = bit_ffs(job_ptr->node_bitmap);
		if (i_first < 0)
			continue;
		i_last = bit_fls(job_ptr->node_bitmap);
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(job_ptr->node_bitmap, i))
				continue;
			node_ptr = node_record_table_ptr + i;
			if (node_ptr->power) {
				power_ptr->alloc_watts +=
					node_ptr->power->cap_watts;
			}
			if (node_ptr->energy) {
				power_ptr->used_watts +=
					node_ptr->energy->current_watts;
			}
		}
		if (debug_flag & DEBUG_FLAG_POWER) {
			info("%s: %pJ Age=%ld(sec) AllocWatts=%u UsedWatts=%u",
			     __func__, job_ptr,
			     (long int) difftime(now, power_ptr->start_time),
			     power_ptr->alloc_watts, power_ptr->used_watts);
		}
	}
	list_iterator_destroy(job_iterator);

	return job_power_list;
}

/* Execute a script, wait for termination and return its stdout.
 * script_name IN - Name of program being run (e.g. "StartStageIn")
 * script_path IN - Fully qualified program of the program to execute
 * script_args IN - Arguments to the script
 * max_wait IN - Maximum time to wait in milliseconds,
 *		 -1 for no limit (asynchronous)
 * data_in IN - data to use as program STDIN (NULL if not STDIN)
 * status OUT - Job exit code
 * Return stdout+stderr of spawned program, value must be xfreed. */
extern char *power_run_script(char *script_name, char *script_path,
			      char **script_argv, int max_wait, char *data_in,
			      int *status)
{
	int i, new_wait, resp_size = 0, resp_offset = 0;
	int send_size = 0, send_offset = 0;
	pid_t cpid;
	char *resp = NULL;
	int fd_stdout[2] = { -1, -1 };
	int fd_stdin[2] = { -1, -1 };

	if ((script_path == NULL) || (script_path[0] == '\0')) {
		error("%s: no script specified", __func__);
		*status = 127;
		resp = xstrdup("Slurm burst buffer configuration error");
		return resp;
	}
	if (slurm_get_debug_flags() & DEBUG_FLAG_POWER) {
		for (i = 0; i < 10; i++) {
			if (!script_argv[i])
				break;
		}
		if (i == 0) {
			info("%s:", __func__);
		} else if (i == 1) {
			info("%s: %s", __func__, script_name);
		} else if (i == 2) {
			info("%s: %s %s", __func__, script_name,
			     script_argv[1]);
		} else if (i == 3) {
			info("%s: %s %s %s", __func__, script_name,
			     script_argv[1], script_argv[2]);
		} else if (i == 4) {
			info("%s: %s %s %s %s", __func__, script_name,
			     script_argv[1], script_argv[2], script_argv[3]);
		} else if (i == 5) {
			info("%s: %s %s %s %s %s", __func__, script_name,
			     script_argv[1], script_argv[2], script_argv[3],
			     script_argv[4]);
		} else if (i == 6) {
			info("%s: %s %s %s %s %s %s", __func__, script_name,
			     script_argv[1], script_argv[2], script_argv[3],
			     script_argv[4], script_argv[5]);
		} else if (i == 7) {
			info("%s: %s %s %s %s %s %s %s", __func__,
			     script_name, script_argv[1], script_argv[2],
			     script_argv[3], script_argv[4], script_argv[5],
			     script_argv[6]);
		} else {	/* 8 or more args here, truncate as needed */
			info("%s: %s %s %s %s %s %s %s %s", __func__,
			     script_name, script_argv[1], script_argv[2],
			     script_argv[3], script_argv[4], script_argv[5],
			     script_argv[6], script_argv[7]);
		}
		if (data_in)
			info("%s: %s", __func__, data_in);
	}
	if (script_path[0] != '/') {
		error("%s: %s is not fully qualified pathname (%s)",
		      __func__, script_name, script_path);
		*status = 127;
		resp = xstrdup("Slurm burst buffer configuration error");
		return resp;
	}
	if (access(script_path, R_OK | X_OK) < 0) {
		error("%s: %s can not be executed (%s) %m",
		      __func__, script_name, script_path);
		*status = 127;
		resp = xstrdup("Slurm burst buffer configuration error");
		return resp;
	}
	if (data_in) {
		if (pipe(fd_stdin) != 0) {
			error("%s: pipe(): %m", __func__);
			*status = 127;
			resp = xstrdup("System error");
			return resp;
		}
	}
	if (max_wait != -1) {
		if (pipe(fd_stdout) != 0) {
			error("%s: pipe(): %m", __func__);
			*status = 127;
			resp = xstrdup("System error");
			return resp;
		}
	}
	if ((cpid = fork()) == 0) {
		int cc;

		cc = sysconf(_SC_OPEN_MAX);
		if (data_in)
			dup2(fd_stdin[0], STDIN_FILENO);
		if (max_wait != -1) {
			dup2(fd_stdout[1], STDERR_FILENO);
			dup2(fd_stdout[1], STDOUT_FILENO);
			for (i = 0; i < cc; i++) {
				if ((i != STDERR_FILENO) &&
				    (i != STDIN_FILENO)  &&
				    (i != STDOUT_FILENO))
					close(i);
			}
		} else {
			for (i = 0; i < cc; i++) {
				if (!data_in || (i != STDERR_FILENO))
					close(i);
			}
			if ((cpid = fork()) < 0)
				exit(127);
			else if (cpid > 0)
				exit(0);
		}
		setpgid(0, 0);
		execv(script_path, script_argv);
		error("%s: execv(%s): %m", __func__, script_path);
		exit(127);
	} else if (cpid < 0) {
		if (data_in) {
			close(fd_stdin[0]);
			close(fd_stdin[1]);
		}
		if (max_wait != -1) {
			close(fd_stdout[0]);
			close(fd_stdout[1]);
		}
		error("%s: fork(): %m", __func__);
	} else if (max_wait != -1) {
		struct pollfd fds;
		time_t start_time = time(NULL);
		if (data_in) {
			close(fd_stdin[0]);
			send_size = strlen(data_in);
			while (send_size > send_offset) {
				i = write(fd_stdin[1], data_in + send_offset,
					 send_size - send_offset);
				if (i == 0) {
					break;
				} else if (i < 0) {
					if (errno == EAGAIN)
						continue;
					error("%s: write(%s): %m", __func__,
					      script_path);
					break;
				} else {
					send_offset += i;
				}
			}
			close(fd_stdin[1]);
		}
		resp_size = 1024;
		resp = xmalloc(resp_size);
		close(fd_stdout[1]);
		while (1) {
			fds.fd = fd_stdout[0];
			fds.events = POLLIN | POLLHUP | POLLRDHUP;
			fds.revents = 0;
			if (max_wait <= 0) {
				new_wait = -1;
			} else {
				new_wait = (time(NULL) - start_time) * 1000
					   + max_wait;
				if (new_wait <= 0)
					break;
			}
			i = poll(&fds, 1, new_wait);
			if (i == 0) {
				error("%s: %s poll timeout",
				      __func__, script_name);
				break;
			} else if (i < 0) {
				error("%s: %s poll:%m", __func__, script_name);
				break;
			}
			if ((fds.revents & POLLIN) == 0)
				break;
			i = read(fd_stdout[0], resp + resp_offset,
				 resp_size - resp_offset);
			if (i == 0) {
				break;
			} else if (i < 0) {
				if (errno == EAGAIN)
					continue;
				error("%s: read(%s): %m", __func__,
				      script_path);
				break;
			} else {
				resp_offset += i;
				if (resp_offset + 1024 >= resp_size) {
					resp_size *= 2;
					resp = xrealloc(resp, resp_size);
				}
			}
		}
		killpg(cpid, SIGKILL);
		waitpid(cpid, status, 0);
		close(fd_stdout[0]);
	} else {
		waitpid(cpid, status, 0);
	}
	return resp;
}

/* For a newly starting job, set "new_job_time" in each of it's nodes
 * NOTE: The job and node data structures must be locked on function entry */
extern void set_node_new_job(struct job_record *job_ptr,
			     struct node_record *node_record_table_ptr)
{
	int i, i_first, i_last;
	struct node_record *node_ptr;
	time_t now = time(NULL);

	if (!job_ptr || !job_ptr->node_bitmap) {
		error("%s: job_ptr node_bitmap is NULL", __func__);
		return;
	}

	i_first = bit_ffs(job_ptr->node_bitmap);
	if (i_first >= 0)
		i_last = bit_fls(job_ptr->node_bitmap);
	else
		i_last = i_first - 1;
	for (i = i_first; i <= i_last; i++) {
		if (!bit_test(job_ptr->node_bitmap, i))
			continue;
		node_ptr = node_record_table_ptr + i;
		if (node_ptr->power)
			node_ptr->power->new_job_time = now;
	}
}
