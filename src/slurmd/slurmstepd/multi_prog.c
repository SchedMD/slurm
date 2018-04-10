/*****************************************************************************\
 *  multi_prog.c - Find the argv array for each task when multi-prog is enabled.
 *
 *  NOTE: This code could be moved into the API if desired. That would mean the
 *  logic would be executed once per job instead of once per task. This would
 *  require substantial modifications to the srun, slurmd, slurmstepd, and
 *  communications logic; so we'll stick with the simple solution for now.
 *****************************************************************************
 *  Produced at National University of Defense Technology (China)
 *  Written by Hongjia Cao <hjcao@nudt.edu.cn>
 *  and
 *  Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>,
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "src/common/log.h"
#include "src/common/strlcpy.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "multi_prog.h"

#define MAX_ARGC 128
#define _DEBUG   0

/*
 * Test if the specified rank is included in the supplied task range
 * IN rank    - this task's rank
 * IN spec    - a line from the configuration file
 * OUT offset - the task's offset within rank range of the configuration file
 * RET 1 if within range, 0 otherwise
 */
static int
_in_range(int rank, char* spec, int *offset)
{
	char* range;
	char* p;
	char* upper;
	int high_num, low_num, passed = 0;

	xassert(offset);

	if (spec[0] == '*' && spec[1] == '\0') {
		*offset = rank;
		return 1;
	}

	for (range = strtok (spec, ","); range != NULL;
			range = strtok (NULL, ",")) {
		p = range;
		while (*p != '\0' && isdigit (*p))
			p ++;
		if (*p == '\0') { /* single rank */
			if (rank == atoi (range)) {
				*offset = passed;
				return 1;
			}
			passed ++;

		} else if (*p == '-') { /* lower-upper */
			upper = ++ p;
			while (isdigit (*p))
				p ++;
			if (*p != '\0') {
				error ("Invalid task range specification (%s) "
					"ignored.", range);
				continue;
			};
			low_num  = atoi (range);
			high_num = atoi (upper);
			if ((rank >= low_num) && (rank <= high_num)) {
				*offset = passed + (rank - low_num);
				return 1;
			} else
				passed += (1 + high_num - low_num);

		} else {
			error ("Invalid task range specification (%s) ignored.",
				range);
		}
	}
	return 0;
}

/*
 * FIXME - It would be nice to parse the multi-prog array just once
 *	to retrieve the argv arrays for each task on this node, rather
 *	than calling multi_prog_get_argv once for each task.
 */
extern int multi_prog_get_argv(char *config_data, char **prog_env,
			       int task_rank, uint32_t *argc, char ***argv,
			       int global_argc, char **global_argv)
{
	char *line = NULL;
	int i, line_num = 0;
	int task_offset;
	char *p = NULL, *ptrptr = NULL;
	char *rank_spec = NULL, *args_spec = NULL;
	int prog_argc = 0;
	char **prog_argv = NULL;
	char *local_data = NULL;
	size_t tmp_buf_len = 256;
	char tmp_buf[tmp_buf_len];
	char *arg_buf = NULL;
	bool last_line_break = false, line_break = false;
	int line_len;


	prog_argv = (char **)xmalloc(sizeof(char *) * MAX_ARGC);

	if (task_rank < 0) {
		error("Invalid task rank %d", task_rank);
		*argc = 1;
		*argv = prog_argv;
		return -1;
	}

	local_data = xstrdup(config_data);
	line = strtok_r(local_data, "\n", &ptrptr);
	while (line) {
		if (line_num > 0)
			line = strtok_r(NULL, "\n", &ptrptr);
		if (line == NULL) {
			error("No executable program specified for this task");
			goto fail;
		}
		line_num++;
		line_len = strlen(line);
		if ((line_len > 0) && (line[line_len - 1] == '\\'))
			line_break = true;
		else
			line_break = false;
		if (last_line_break) {
			last_line_break = line_break;
			continue;
		}
		last_line_break = line_break;
		p = line;
		while ((*p != '\0') && isspace (*p)) /* remove leading spaces */
			p++;

		if (*p == '#') /* only whole-line comments handled */
			continue;

		if (*p == '\0') /* blank line ignored */
			continue;

		rank_spec = p;

		while ((*p != '\0') && !isspace (*p))
			p++;
		if (*p == '\0') {
			error("Invalid MPMD configuration line %d", line_num);
			goto fail;
		}
		*p++ = '\0';

		if (!_in_range(task_rank, rank_spec, &task_offset))
			continue;

		/* skip all whitspace after the range spec */
		while ((*p != '\0') && isspace (*p))
			p++;

		args_spec = p;
		while (*args_spec != '\0') {
			/* Only simple quote and escape supported */
			if (arg_buf) {
				prog_argv[prog_argc++] = arg_buf;
				arg_buf=NULL;
			}
			if ((prog_argc + 1) >= MAX_ARGC) {
				info("Exceeded multi-prog argc limit");
				break;
			}
		CONT:	p = args_spec;
			while ((*args_spec != '\0') && (*args_spec != '\\') &&
			       (*args_spec != '%')  && (*args_spec != '\'') &&
			       !isspace(*args_spec)) {
				args_spec++;
			}
			xstrncat(arg_buf, p, (args_spec - p));
			if (*args_spec == '\0') {
				/* the last argument */
				break;

			} else if (*args_spec == '%') {
				args_spec++;
				if (*args_spec == 't') {
					/* task rank */
					snprintf(tmp_buf, tmp_buf_len, "%d",
						 task_rank);
					xstrcat(arg_buf, tmp_buf);
				} else if (*args_spec == 'o') {
					/* task offset */
					snprintf(tmp_buf, tmp_buf_len, "%d",
						 task_offset);
					xstrcat(arg_buf, tmp_buf);
				}
				args_spec++;
				goto CONT;

			} else if (*args_spec == '\\') {
				/* escape, just remove the backslash */
				args_spec++;
				if (*args_spec != '\0') {
					xstrcatchar(arg_buf, *args_spec);
					args_spec++;
				} else {
					line = strtok_r(NULL, "\n", &ptrptr);
					if (!line)
						break;
					line_num++;
					args_spec = line;
				}
				goto CONT;

			} else if (*args_spec == '\'') {
				/* single quote,
				 * preserve all characters quoted. */
				p = ++args_spec;
		LINE_BREAK:	while ((*args_spec != '\0') &&
				       (*args_spec != '\'')) {
					args_spec++;
				}
				if (*args_spec == '\0') {
					/* closing quote not found */
					if (*(args_spec - 1) == '\\') {
						line = strtok_r(NULL, "\n",
								&ptrptr);
						if (line) {
							line_num++;
							args_spec = line;
							goto LINE_BREAK;
						}
					}
					error("Program arguments specification format invalid: %s.",
					      prog_argv[prog_argc - 1]);
					goto fail;
				}
				xstrncat(arg_buf, p, (args_spec - p));
				args_spec++;
				goto CONT;

			} else {
				/* space */
				while ((*args_spec != '\0') &&
				       isspace(*args_spec)) {
					args_spec++;
				}
			}

		}
		if (arg_buf) {
			prog_argv[prog_argc++] = arg_buf;
			arg_buf = NULL;
		}

		for (i = 2; i < global_argc; i++) {
			if ((prog_argc + 1) >= MAX_ARGC) {
				info("Exceeded multi-prog argc limit");
				break;
			}
			prog_argv[prog_argc++] = xstrdup(global_argv[i]);
		}
		prog_argv[prog_argc] = NULL;

		*argc = prog_argc;
		*argv = prog_argv;
		xfree(local_data);
		return 0;
	}

	error("Program for task rank %d not specified.", task_rank);
fail:
	xfree(local_data);
	*argc = 1;
	prog_argv[0] = NULL;
	*argv = prog_argv;
	return -1;
}

/*
 * Parse an MPMD file and determine count and layout of each task for use
 * with Cray systems. Builds the mpmd_set structure in the job record.
 *
 * IN/OUT job - job step details, builds mpmd_set structure
 * IN gtid - Array of global task IDs, indexed by node_id and task
 */
extern void multi_prog_parse(stepd_step_rec_t *job, uint32_t **gtid)
{
	int i, j, line_num = 0, rank_id, total_ranks = 0;
	char *line = NULL, *local_data = NULL;
	char *end_ptr = NULL, *save_ptr = NULL, *tmp_str;
	char *rank_spec = NULL, *cmd_spec = NULL, *args_spec = NULL;
	char *p = NULL;
	char **tmp_args, **tmp_cmd, *one_rank;
	uint32_t *ranks_node_id = NULL;	/* Node ID for each rank */
	uint32_t *node_id2nid = NULL;	/* Map Slurm node ID to Cray NID name */
	bool last_line_break = false, line_break = false;
	char *last_rank_spec = NULL;
	int args_len, line_len;
	hostlist_t hl;

	tmp_args = xmalloc(sizeof(char *) * job->ntasks);
	tmp_cmd = xmalloc(sizeof(char *) * job->ntasks);
	node_id2nid = xmalloc(sizeof(uint32_t) * job->nnodes);
	ranks_node_id = xmalloc(sizeof(uint32_t) * job->ntasks);
	local_data = xstrdup(job->argv[1]);
	while (1) {
		if (line_num)
			line = strtok_r(NULL, "\n", &save_ptr);
		else
			line = strtok_r(local_data, "\n", &save_ptr);
		if (!line)
			break;
		line_num++;
		line_len = strlen(line);
		if ((line_len > 0) && (line[line_len - 1] == '\\'))
			line_break = true;
		else
			line_break = false;
		if (last_line_break && last_rank_spec) {
			tmp_str = xmalloc(strlen(last_rank_spec) + 3);
			sprintf(tmp_str, "[%s]", last_rank_spec);
			hl = hostlist_create(tmp_str);
			xfree(tmp_str);
			if (!hl)
				goto fail;

			while ((one_rank = hostlist_pop(hl))) {
				rank_id = strtol(one_rank, &end_ptr, 10);
				if ((end_ptr[0] != '\0') || (rank_id < 0) ||
				    (rank_id >= job->ntasks)) {
					free(one_rank);
					hostlist_destroy(hl);
					goto fail;
				}
				free(one_rank);
				args_len = strlen(tmp_args[rank_id]);
				if (!tmp_args[rank_id] ||
				    tmp_args[rank_id][args_len - 1] != '\\') {
					hostlist_destroy(hl);
					goto fail;
				}
				tmp_args[rank_id][args_len -1] = '\0';
				xstrcat(tmp_args[rank_id], line);
			}
			hostlist_destroy(hl);
			last_line_break = line_break;
			continue;
		}
		last_line_break = line_break;

		p = line;
		while ((*p != '\0') && isspace(*p)) /* remove leading spaces */
			p++;
		if (*p == '#')	/* only whole-line comments handled */
			continue;
		if (*p == '\0') /* blank line ignored */
			continue;

		rank_spec = p;	/* Rank specification for this line */
		while ((*p != '\0') && !isspace(*p))
			p++;
		if (*p == '\0')
			goto fail;
		*p++ = '\0';

		while ((*p != '\0') && isspace(*p)) /* remove leading spaces */
			p++;
		if (*p == '\0') /* blank line ignored */
			continue;

		cmd_spec = p;	/* command only */
		while ((*p != '\0') && !isspace(*p))
			p++;
		if (isspace(*p))
			*p++ = '\0';

		while ((*p != '\0') && isspace(*p)) /* remove leading spaces */
			p++;
		if (*p == '\0')
			args_spec = NULL;	/* no arguments */
		else
			args_spec = p;		/* arguments string */

		tmp_str = xmalloc(strlen(rank_spec) + 3);
		sprintf(tmp_str, "[%s]", rank_spec);
		hl = hostlist_create(tmp_str);
		xfree(tmp_str);
		if (!hl)
			goto fail;
		while ((one_rank = hostlist_pop(hl))) {
			rank_id = strtol(one_rank, &end_ptr, 10);
			if ((end_ptr[0] != '\0') || (rank_id < 0) ||
			    (rank_id >= job->ntasks)) {
				free(one_rank);
				hostlist_destroy(hl);
				goto fail;
			}
			free(one_rank);
			if (tmp_args[rank_id])	/* duplicate record for rank */
				xfree(tmp_args[rank_id]);
			if (tmp_cmd[rank_id])	/* duplicate record for rank */
				xfree(tmp_cmd[rank_id]);
			else
				total_ranks++;
			tmp_args[rank_id] = xstrdup(args_spec);
			tmp_cmd[rank_id] = xstrdup(cmd_spec);
		}
		hostlist_destroy(hl);
		if (line_break)
			last_rank_spec = rank_spec;
	}
	if (total_ranks != job->ntasks)
		goto fail;

	if (job->msg->complete_nodelist &&
	    ((hl = hostlist_create(job->msg->complete_nodelist)))) {
		i = 0;
		while ((one_rank = hostlist_shift(hl))) {
			if (i >= job->nnodes) {
				error("MPMD more nodes in nodelist than count "
				      "(cnt:%u nodelist:%s)", job->nnodes,
				      job->msg->complete_nodelist);
			}
			for (j = 0; one_rank[j] && !isdigit(one_rank[j]); j++)
				;
			node_id2nid[i++] = strtol(one_rank + j, &end_ptr, 10);
			free(one_rank);
		}
		hostlist_destroy(hl);
	}

	for (i = 0; i < job->nnodes; i++) {
		if (!job->task_cnts) {
			error("MPMD job->task_cnts is NULL");
			break;
		}
		if (!job->task_cnts[i]) {
			error("MPMD job->task_cnts[%d] is NULL", i);
			break;
		}
		if (!gtid) {
			error("MPMD gtid is NULL");
			break;
		}
		if (!gtid[i]) {
			error("MPMD gtid[%d] is NULL", i);
			break;
		}
		for (j = 0; j < job->task_cnts[i]; j++) {
			if (gtid[i][j] >= job->ntasks) {
				error("MPMD gtid[%d][%d] is invalid (%u >= %u)",
				      i, j, gtid[i][j], job->ntasks);
				break;
			}
			ranks_node_id[gtid[i][j]] = i;
		}
	}

	job->mpmd_set = xmalloc(sizeof(mpmd_set_t));
	job->mpmd_set->apid      = SLURM_ID_HASH(job->jobid, job->stepid);
	job->mpmd_set->args      = xmalloc(sizeof(char *) * job->ntasks);
	job->mpmd_set->command   = xmalloc(sizeof(char *) * job->ntasks);
	job->mpmd_set->first_pe  = xmalloc(sizeof(int) * job->ntasks);
	job->mpmd_set->start_pe  = xmalloc(sizeof(int) * job->ntasks);
	job->mpmd_set->total_pe  = xmalloc(sizeof(int) * job->ntasks);
	job->mpmd_set->placement = xmalloc(sizeof(int) * job->ntasks);
	for (i = 0, j = 0; i < job->ntasks; i++) {
		job->mpmd_set->placement[i] = node_id2nid[ranks_node_id[i]];
		if (i == 0) {
			job->mpmd_set->num_cmds++;
			if (ranks_node_id[i] == job->nodeid)
				job->mpmd_set->first_pe[j] = i;
			else
				job->mpmd_set->first_pe[j] = -1;
			job->mpmd_set->args[j] = xstrdup(tmp_args[i]);
			job->mpmd_set->command[j] = xstrdup(tmp_cmd[i]);
			job->mpmd_set->start_pe[j] = i;
			job->mpmd_set->total_pe[j]++;
		} else if (!xstrcmp(tmp_cmd[i-1],  tmp_cmd[i]) &&
			   !xstrcmp(tmp_args[i-1], tmp_args[i]) &&
			   !xstrchr(tmp_args[i-1], '%')) {
			if ((ranks_node_id[i] == job->nodeid) &&
			    (job->mpmd_set->first_pe[j] == -1))
				job->mpmd_set->first_pe[j] = i;
			job->mpmd_set->total_pe[j]++;
		} else {
			j++;
			if (ranks_node_id[i] == job->nodeid)
				job->mpmd_set->first_pe[j] = i;
			else
				job->mpmd_set->first_pe[j] = -1;
			job->mpmd_set->num_cmds++;
			job->mpmd_set->args[j] = xstrdup(tmp_args[i]);
			job->mpmd_set->command[j] = xstrdup(tmp_cmd[i]);
			job->mpmd_set->start_pe[j] = i;
			job->mpmd_set->total_pe[j]++;
		}
	}
#if _DEBUG
	info("MPMD Apid:%"PRIu64"", job->mpmd_set->apid);
	info("MPMD NumPEs:%u", job->ntasks);		/* Total rank count */
	info("MPMD NumPEsHere:%u", job->node_tasks);	/* Node's rank count */
	info("MPMD NumCmds:%d", job->mpmd_set->num_cmds);
	for (i = 0; i < job->mpmd_set->num_cmds; i++) {
		info("MPMD Cmd:%s Args:%s FirstPE:%d StartPE:%d TotalPEs:%d ",
		     job->mpmd_set->command[i],  job->mpmd_set->args[i],
		     job->mpmd_set->first_pe[i], job->mpmd_set->start_pe[i],
		     job->mpmd_set->total_pe[i]);
	}
	for (i = 0; i < job->ntasks; i++) {
		info("MPMD Placement[%d]:nid%5.5d",
		     i, job->mpmd_set->placement[i]);
	}
#endif

fini:	for (i = 0; i < job->ntasks; i++) {
		xfree(tmp_args[i]);
		xfree(tmp_cmd[i]);
	}
	xfree(tmp_args);
	xfree(tmp_cmd);
	xfree(local_data);
	xfree(node_id2nid);
	xfree(ranks_node_id);
	return;

fail:	error("Invalid MPMD configuration line %d", line_num);
	goto fini;
}

/* Free memory associated with a job's MPMD data structure built by
 * multi_prog_parse() and used for Cray system. */
extern void mpmd_free(stepd_step_rec_t *job)
{
	int i;

	if (!job->mpmd_set)
		return;

	if (job->mpmd_set->args) {
		for (i = 0; i < job->ntasks; i++)
			xfree(job->mpmd_set->args[i]);
		xfree(job->mpmd_set->args);
	}
	if (job->mpmd_set->command) {
		for (i = 0; i < job->ntasks; i++)
			xfree(job->mpmd_set->command[i]);
		xfree(job->mpmd_set->command);
	}
	xfree(job->mpmd_set->first_pe);
	xfree(job->mpmd_set->placement);
	xfree(job->mpmd_set->start_pe);
	xfree(job->mpmd_set->total_pe);
	xfree(job->mpmd_set);
}
