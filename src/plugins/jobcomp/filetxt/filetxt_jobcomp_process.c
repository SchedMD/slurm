/*****************************************************************************\
 *  filetxt_jobcomp_process.c - functions the processing of
 *                               information from the filetxt jobcomp
 *                               database.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "src/interfaces/jobcomp.h"
#include "src/common/xmalloc.h"
#include "src/common/parse_time.h"
#include "filetxt_jobcomp_process.h"

#define BUFFER_SIZE 4096

typedef struct {
	char *name;
	char *val;
} filetxt_jobcomp_info_t;


static void _destroy_filetxt_jobcomp_info(void *object)
{
	filetxt_jobcomp_info_t *jobcomp_info =
		(filetxt_jobcomp_info_t *)object;
	if (jobcomp_info) {
		xfree(jobcomp_info);
	}
}


/* _open_log_file() -- find the current or specified log file, and open it
 *
 * IN:		Nothing
 * RETURNS:	Nothing
 *
 * Side effects:
 * 	- Sets opt_filein to the current system accounting log unless
 * 	  the user specified another file.
 */

static FILE *_open_log_file(char *logfile)
{
	FILE *fd = fopen(logfile, "r");
	if (fd == NULL) {
		perror(logfile);
		exit(1);
	}
	return fd;
}

static jobcomp_job_rec_t *_parse_line(List job_info_list)
{
	ListIterator itr = NULL;
	filetxt_jobcomp_info_t *jobcomp_info = NULL;
	jobcomp_job_rec_t *job = xmalloc(sizeof(jobcomp_job_rec_t));
	char *temp = NULL;
	time_t start_time = 0;
	time_t end_time = 0;

	itr = list_iterator_create(job_info_list);
	while ((jobcomp_info = list_next(itr))) {
		if (!xstrcasecmp("JobId", jobcomp_info->name)) {
			job->jobid = atoi(jobcomp_info->val);
		} else if (!xstrcasecmp("Partition", jobcomp_info->name)) {
			job->partition = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("StartTime", jobcomp_info->name)) {
			job->start_time = xstrdup(jobcomp_info->val);
			start_time = parse_time(job->start_time, 1);
		} else if (!xstrcasecmp("EndTime", jobcomp_info->name)) {
			job->end_time = xstrdup(jobcomp_info->val);
			end_time = parse_time(job->end_time, 1);
		} else if (!xstrcasecmp("UserId", jobcomp_info->name)) {
			temp = strstr(jobcomp_info->val, "(");
			if (!temp) {
				job->uid = atoi(jobcomp_info->val);
				error("problem getting correct uid from %s",
				      jobcomp_info->val);
			} else {
				job->uid = atoi(temp + 1);
				job->uid_name = xstrdup(jobcomp_info->val);
			}
		} else if (!xstrcasecmp("GroupId", jobcomp_info->name)) {
			temp = strstr(jobcomp_info->val, "(");
			if (!temp) {
				job->gid = atoi(jobcomp_info->val);
				error("problem getting correct gid from %s",
				      jobcomp_info->val);
			} else {
				job->gid = atoi(temp + 1);
				job->gid_name = xstrdup(jobcomp_info->val);
			}
		} else if (!xstrcasecmp("Name", jobcomp_info->name)) {
			job->jobname = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("NodeList", jobcomp_info->name)) {
			job->nodelist = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("NodeCnt", jobcomp_info->name)) {
			job->node_cnt = atoi(jobcomp_info->val);
		} else if (!xstrcasecmp("ProcCnt", jobcomp_info->name)) {
			job->proc_cnt = atoi(jobcomp_info->val);
		} else if (!xstrcasecmp("JobState", jobcomp_info->name)) {
			job->state = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Timelimit", jobcomp_info->name)) {
			job->timelimit = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Workdir", jobcomp_info->name)) {
			job->work_dir = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("ReservationName", jobcomp_info->name)) {
			job->resv_name = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Gres", jobcomp_info->name)) {
			job->tres_fmt_req_str = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Tres", jobcomp_info->name)) {
			job->tres_fmt_req_str = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Account", jobcomp_info->name)) {
			job->account = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("QOS", jobcomp_info->name)) {
			job->qos_name = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("WcKey", jobcomp_info->name)) {
			job->wckey = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("Cluster", jobcomp_info->name)) {
			job->cluster = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("SubmitTime", jobcomp_info->name)) {
			job->submit_time = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("EligibleTime", jobcomp_info->name)) {
			job->eligible_time = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("DerivedExitCode", jobcomp_info->name)) {
			job->derived_ec = xstrdup(jobcomp_info->val);
		} else if (!xstrcasecmp("ExitCode", jobcomp_info->name)) {
			job->exit_code = xstrdup(jobcomp_info->val);
		} else {
			error("Unknown type %s: %s", jobcomp_info->name,
			      jobcomp_info->val);
		}
	}
	job->elapsed_time = end_time - start_time;
	list_iterator_destroy(itr);

	return job;
}

extern List filetxt_jobcomp_process_get_jobs(slurmdb_job_cond_t *job_cond)
{
	char line[BUFFER_SIZE];
	char *fptr = NULL;
	int jobid = 0;
	char *partition = NULL;
	FILE *fd = NULL;
	jobcomp_job_rec_t *job = NULL;
	slurm_selected_step_t *selected_step = NULL;
	char *selected_part = NULL;
	ListIterator itr = NULL;
	List job_info_list = NULL;
	filetxt_jobcomp_info_t *jobcomp_info = NULL;
	List job_list = list_create(jobcomp_destroy_job);

	fd = _open_log_file(slurm_conf.job_comp_loc);

	while (fgets(line, BUFFER_SIZE, fd)) {
		fptr = line;	/* break the record into NULL-
				   terminated strings */
		FREE_NULL_LIST(job_info_list);
		jobid = 0;
		partition = NULL;
		job_info_list = list_create(_destroy_filetxt_jobcomp_info);
		while (fptr) {
			jobcomp_info =
				xmalloc(sizeof(filetxt_jobcomp_info_t));
			list_append(job_info_list, jobcomp_info);
			jobcomp_info->name = fptr;
			fptr = strstr(fptr, "=");
			if (!fptr)
				break;
			*fptr++ = 0;
			jobcomp_info->val = fptr;
			fptr = strstr(fptr, " ");
			if (!xstrcasecmp("JobId", jobcomp_info->name))
				jobid = atoi(jobcomp_info->val);
			else if (!xstrcasecmp("Partition", jobcomp_info->name))
				partition = jobcomp_info->val;


			if (!fptr) {
				fptr = strstr(jobcomp_info->val, "\n");
				if (fptr)
					*fptr = 0;
				break;
			} else {
				*fptr++ = 0;
				if (*fptr == '\n') {
					*fptr = 0;
					break;
				}
			}
		}

		if (job_cond->step_list && list_count(job_cond->step_list)) {
			if (!jobid)
				continue;
			itr = list_iterator_create(job_cond->step_list);
			while ((selected_step = list_next(itr))) {
				if (selected_step->step_id.job_id != jobid)
					continue;
				/* job matches */
				list_iterator_destroy(itr);
				goto foundjob;
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundjob:

		if (job_cond->partition_list
		    && list_count(job_cond->partition_list)) {
			if (!partition)
				continue;
			itr = list_iterator_create(job_cond->partition_list);
			while ((selected_part = list_next(itr)))
				if (!xstrcasecmp(selected_part, partition)) {
					list_iterator_destroy(itr);
					goto foundp;
				}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundp:

		job = _parse_line(job_info_list);

		if (job)
			list_append(job_list, job);
	}

	FREE_NULL_LIST(job_info_list);

	if (ferror(fd)) {
		perror(slurm_conf.job_comp_loc);
		exit(1);
	}
	fclose(fd);

	return job_list;
}
