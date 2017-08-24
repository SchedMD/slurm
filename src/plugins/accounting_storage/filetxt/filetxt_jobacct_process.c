/*****************************************************************************\
 *  filetxt_jobacct_process.c - functions the processing of
 *                               information from the filetxt jobacct
 *                               storage.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include <sys/resource.h> /* for struct rusage */
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>

#include "src/common/slurm_xlator.h"
#include "filetxt_jobacct_process.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmdbd/read_config.h"
/* Map field names to positions */

/* slurmd uses "(uint32_t) -2" to track data for batch allocations
 * which have no logical jobsteps. */
#define BATCH_JOB_TIMESTAMP 0
#define EXPIRE_READ_LENGTH 10
#define MAX_RECORD_FIELDS 100

typedef struct expired_rec {  /* table of expired jobs */
	uint32_t job;
	time_t job_submit;
	char *line;
} expired_rec_t;

typedef struct header {
	uint32_t jobnum;
	char	*partition;
	char	*blockid;
	time_t 	job_submit;
	time_t 	timestamp;
	uint32_t uid;
	uint32_t gid;
} filetxt_header_t;

typedef struct {
	uint32_t job_start_seen,		/* useful flags */
		job_step_seen,
		job_terminated_seen,
		jobnum_superseded;	/* older jobnum was reused */
	filetxt_header_t header;
	uint16_t show_full;
	char	*nodes;
	char	*jobname;
	uint16_t track_steps;
	int32_t priority;
	uint32_t ncpus;
	uint32_t ntasks;
	uint32_t status; 		/* job state */
	int32_t	exitcode;
	uint32_t elapsed;
	time_t end;
	uint32_t tot_cpu_sec;
	uint32_t tot_cpu_usec;
	struct rusage rusage;
	slurmdb_stats_t stats;
	List    steps;
	char    *account;
	uint32_t requid;
} filetxt_job_rec_t;

typedef struct {
	filetxt_header_t   header;
	uint32_t	stepnum;	/* job's step number */
	char	        *nodes;
	char	        *stepname;
	uint32_t 	status; 	/* job state */
	int32_t	        exitcode;
	uint32_t	ntasks;
	uint32_t        ncpus;
	uint32_t	elapsed;
	time_t          end;
	uint32_t	tot_cpu_sec;
	uint32_t        tot_cpu_usec;
	struct rusage   rusage;
	slurmdb_stats_t stats;
	char            *account;
	uint32_t requid;
} filetxt_step_rec_t;

/* Fields common to all records */
enum {	F_JOB =	0,
	F_PARTITION,
	F_JOB_SUBMIT,
	F_TIMESTAMP,
	F_UID,
	F_GID,
	F_BLOCKID,
	F_RESERVED2,
	F_RECTYPE,
	HEADER_LENGTH
};

/* JOB_START fields */
enum {	F_JOBNAME = HEADER_LENGTH,
	F_TRACK_STEPS,
	F_PRIORITY,
	F_NCPUS,
	F_NODES,
	F_JOB_ACCOUNT,
	JOB_START_LENGTH
};

/* JOB_STEP fields */
enum {	F_JOBSTEP = HEADER_LENGTH,
	F_STATUS,
	F_EXITCODE,
	F_NTASKS,
	F_STEPNCPUS,
	F_ELAPSED,
	F_CPU_SEC,
	F_CPU_USEC,
	F_USER_SEC,
	F_USER_USEC,
	F_SYS_SEC,
	F_SYS_USEC,
	F_RSS,
	F_IXRSS,
	F_IDRSS,
	F_ISRSS,
	F_MINFLT,
	F_MAJFLT,
	F_NSWAP,
	F_INBLOCKS,
	F_OUBLOCKS,
	F_MSGSND,
	F_MSGRCV,
	F_NSIGNALS,
	F_NVCSW,
	F_NIVCSW,
	F_MAX_VSIZE,
	F_MAX_VSIZE_TASK,
	F_AVE_VSIZE,
	F_MAX_RSS,
	F_MAX_RSS_TASK,
	F_AVE_RSS,
	F_MAX_PAGES,
	F_MAX_PAGES_TASK,
	F_AVE_PAGES,
	F_MIN_CPU,
	F_MIN_CPU_TASK,
	F_AVE_CPU,
	F_STEPNAME,
	F_STEPNODES,
	F_MAX_VSIZE_NODE,
	F_MAX_RSS_NODE,
	F_MAX_PAGES_NODE,
	F_MIN_CPU_NODE,
	F_STEP_ACCOUNT,
	F_STEP_REQUID,
	JOB_STEP_LENGTH
};

/* JOB_TERM / JOB_SUSPEND fields */
enum {	F_TOT_ELAPSED = HEADER_LENGTH,
	F_TERM_STATUS,
	F_JOB_REQUID,
	F_JOB_EXITCODE,
	JOB_TERM_LENGTH
};

static void _destroy_exp(void *object)
{
	expired_rec_t *exp_rec = (expired_rec_t *)object;
	if (exp_rec) {
		xfree(exp_rec->line);
		xfree(exp_rec);
	}
}

static void _free_filetxt_header(void *object)
{
	filetxt_header_t *header = (filetxt_header_t *)object;
	if (header) {
		xfree(header->partition);
#ifdef HAVE_BG
		xfree(header->blockid);
#endif
	}
}

static void _destroy_filetxt_job_rec(void *object)
{
	filetxt_job_rec_t *job = (filetxt_job_rec_t *)object;
	if (job) {
		FREE_NULL_LIST(job->steps);
		_free_filetxt_header(&job->header);
		xfree(job->jobname);
		xfree(job->account);
		xfree(job->nodes);
		xfree(job);
	}
}

static void _destroy_filetxt_step_rec(void *object)
{
	filetxt_step_rec_t *step = (filetxt_step_rec_t *)object;
	if (step) {
		_free_filetxt_header(&step->header);
		xfree(step->stepname);
		xfree(step->nodes);
		xfree(step->account);
		xfree(step);
	}
}

static slurmdb_step_rec_t *_slurmdb_create_step_rec(
	filetxt_step_rec_t *filetxt_step)
{
	slurmdb_step_rec_t *slurmdb_step = slurmdb_create_step_rec();

	slurmdb_step->elapsed = filetxt_step->elapsed;
	slurmdb_step->end = filetxt_step->end;
	slurmdb_step->exitcode = filetxt_step->exitcode;
	slurmdb_step->tres_alloc_str = xstrdup_printf(
		"cpu=%u", filetxt_step->ncpus);

	if (filetxt_step->nodes) {
		hostlist_t hl = hostlist_create(filetxt_step->nodes);
		slurmdb_step->nnodes = hostlist_count(hl);
		hostlist_destroy(hl);
	}
	slurmdb_step->nodes = xstrdup(filetxt_step->nodes);
	slurmdb_step->requid = filetxt_step->requid;
	memcpy(&slurmdb_step->stats, &filetxt_step->stats,
	       sizeof(slurmdb_stats_t));
	slurmdb_step->start = slurmdb_step->end - slurmdb_step->elapsed;
	slurmdb_step->state = filetxt_step->status;
	slurmdb_step->stepid = filetxt_step->stepnum;
	slurmdb_step->stepname = xstrdup(filetxt_step->stepname);
	slurmdb_step->sys_cpu_sec = filetxt_step->rusage.ru_stime.tv_sec;
	slurmdb_step->sys_cpu_usec = filetxt_step->rusage.ru_stime.tv_usec;
	slurmdb_step->tot_cpu_sec = filetxt_step->tot_cpu_sec;
	slurmdb_step->tot_cpu_usec = filetxt_step->tot_cpu_usec;
	slurmdb_step->user_cpu_sec = filetxt_step->rusage.ru_utime.tv_sec;
	slurmdb_step->user_cpu_usec = filetxt_step->rusage.ru_utime.tv_usec;

	return slurmdb_step;
}

static slurmdb_job_rec_t *_slurmdb_create_job_rec(
	filetxt_job_rec_t *filetxt_job, slurmdb_job_cond_t *job_cond)
{
	slurmdb_job_rec_t *slurmdb_job = NULL;
	ListIterator itr = NULL;
	filetxt_step_rec_t *filetxt_step = NULL;

	if (!job_cond)
		goto no_cond;

	if (job_cond->state_list
	    && list_count(job_cond->state_list)) {
		char *object = NULL;
		itr = list_iterator_create(job_cond->state_list);
		while((object = list_next(itr))) {
			if (atoi(object) == filetxt_job->status) {
				list_iterator_destroy(itr);
				goto foundstate;
			}
		}
		list_iterator_destroy(itr);
		return NULL;	/* no match */
	}

foundstate:

no_cond:
	slurmdb_job = slurmdb_create_job_rec();
	slurmdb_job->associd = 0;
	slurmdb_job->account = xstrdup(filetxt_job->account);
	slurmdb_job->blockid = xstrdup(filetxt_job->header.blockid);
	slurmdb_job->cluster = NULL;
	slurmdb_job->elapsed = filetxt_job->elapsed;
	slurmdb_job->eligible = filetxt_job->header.job_submit;
	slurmdb_job->end = filetxt_job->end;
	slurmdb_job->exitcode = filetxt_job->exitcode;
	slurmdb_job->gid = filetxt_job->header.gid;
	slurmdb_job->jobid = filetxt_job->header.jobnum;
	slurmdb_job->jobname = xstrdup(filetxt_job->jobname);
	slurmdb_job->partition = xstrdup(filetxt_job->header.partition);
	slurmdb_job->req_cpus = filetxt_job->ncpus;
	slurmdb_job->tres_alloc_str = xstrdup_printf(
		"cpu=%u", filetxt_job->ncpus);

	if (filetxt_job->nodes) {
		hostlist_t hl = hostlist_create(filetxt_job->nodes);
		slurmdb_job->alloc_nodes = hostlist_count(hl);
		hostlist_destroy(hl);
	}
	slurmdb_job->nodes = xstrdup(filetxt_job->nodes);
	slurmdb_job->priority = filetxt_job->priority;
	slurmdb_job->requid = filetxt_job->requid;
	memcpy(&slurmdb_job->stats, &filetxt_job->stats,
	       sizeof(slurmdb_stats_t));
	slurmdb_job->show_full = filetxt_job->show_full;
	slurmdb_job->start = slurmdb_job->end - slurmdb_job->elapsed;
	slurmdb_job->state = filetxt_job->status;

	slurmdb_job->steps = list_create(slurmdb_destroy_step_rec);
	if (filetxt_job->steps) {
		itr = list_iterator_create(filetxt_job->steps);
		while((filetxt_step = list_next(itr))) {
			slurmdb_step_rec_t *step =
				_slurmdb_create_step_rec(filetxt_step);
			if (step) {
				step->job_ptr = slurmdb_job;
				if (!slurmdb_job->first_step_ptr)
					slurmdb_job->first_step_ptr = step;
				list_append(slurmdb_job->steps, step);
			}
		}
		list_iterator_destroy(itr);
	}
	slurmdb_job->submit = filetxt_job->header.job_submit;

	slurmdb_job->sys_cpu_sec = filetxt_job->rusage.ru_stime.tv_sec;
	slurmdb_job->sys_cpu_usec = filetxt_job->rusage.ru_stime.tv_usec;
	slurmdb_job->tot_cpu_sec = filetxt_job->tot_cpu_sec;
	slurmdb_job->tot_cpu_usec = filetxt_job->tot_cpu_usec;
	slurmdb_job->track_steps = filetxt_job->track_steps;
	slurmdb_job->uid = filetxt_job->header.uid;
	slurmdb_job->user = NULL;
	slurmdb_job->user_cpu_sec = filetxt_job->rusage.ru_utime.tv_sec;
	slurmdb_job->user_cpu_usec = filetxt_job->rusage.ru_utime.tv_usec;

	return slurmdb_job;
}

static filetxt_job_rec_t *_create_filetxt_job_rec(filetxt_header_t header)
{
	filetxt_job_rec_t *job = xmalloc(sizeof(filetxt_job_rec_t));
	memcpy(&job->header, &header, sizeof(filetxt_header_t));
	memset(&job->rusage, 0, sizeof(struct rusage));
	memset(&job->stats, 0, sizeof(slurmdb_stats_t));
	job->stats.cpu_min = NO_VAL;
	job->job_start_seen = 0;
	job->job_step_seen = 0;
	job->job_terminated_seen = 0;
	job->jobnum_superseded = 0;
	job->jobname = NULL;
	job->status = JOB_PENDING;
	job->nodes = NULL;
	job->jobname = NULL;
	job->exitcode = 0;
	job->priority = 0;
	job->ntasks = 0;
	job->ncpus = 0;
	job->elapsed = 0;
	job->tot_cpu_sec = 0;
	job->tot_cpu_usec = 0;
	job->steps = list_create(_destroy_filetxt_step_rec);
	job->nodes = NULL;
	job->track_steps = 0;
	job->account = NULL;
	job->requid = -1;

      	return job;
}

static filetxt_step_rec_t *_create_filetxt_step_rec(filetxt_header_t header)
{
	filetxt_step_rec_t *step = xmalloc(sizeof(filetxt_job_rec_t));
	memcpy(&step->header, &header, sizeof(filetxt_header_t));
	memset(&step->rusage, 0, sizeof(struct rusage));
	memset(&step->stats, 0, sizeof(slurmdb_stats_t));
	step->stepnum = (uint32_t)NO_VAL;
	step->nodes = NULL;
	step->stepname = NULL;
	step->status = NO_VAL;
	step->exitcode = NO_VAL;
	step->ntasks = (uint32_t)NO_VAL;
	step->ncpus = (uint32_t)NO_VAL;
	step->elapsed = (uint32_t)NO_VAL;
	step->tot_cpu_sec = (uint32_t)NO_VAL;
	step->tot_cpu_usec = (uint32_t)NO_VAL;
	step->account = NULL;
	step->requid = -1;

	return step;
}

/* prefix_filename() -- insert a filename prefix into a path
 *
 * IN:	path = fully-qualified path+file name
 *      prefix = the prefix to insert into the file name
 * RETURNS: pointer to the updated path+file name
 */

static char *_prefix_filename(char *path, char *prefix) {
	char	*out;
	int     i,
		plen;

	plen = strlen(path);
	out = xmalloc(plen+strlen(prefix)+1);
	for (i=plen-1; i>=0; i--)
		if (path[i]=='/') {
			break;
		}
	i++;
	*out = 0;
	strncpy(out, path, i);
	out[i] = 0;
	strcat(out, prefix);
	strcat(out, path+i);
	return(out);
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

static int _cmp_jrec(const void *a1, const void *a2) {
	expired_rec_t *j1 = *(expired_rec_t **) a1;
	expired_rec_t *j2 = *(expired_rec_t **) a2;

	if (j1->job <  j2->job)
		return -1;
	else if (j1->job == j2->job) {
		if (j1->job_submit == j2->job_submit)
			return 0;
		else
			return 1;
	}
	return 1;
}

static void _show_rec(char *f[])
{
	int 	i;
	fprintf(stderr, "rec>");
	for (i=0; f[i]; i++)
		fprintf(stderr, " %s", f[i]);
	fprintf(stderr, "\n");
	return;
}

static filetxt_job_rec_t *_find_job_record(List job_list,
					   filetxt_header_t header,
					   int type)
{
	filetxt_job_rec_t *job = NULL;
	ListIterator itr = list_iterator_create(job_list);

	while((job = (filetxt_job_rec_t *)list_next(itr)) != NULL) {
		if (job->header.jobnum == header.jobnum) {
			if (job->header.job_submit == 0 && type == JOB_START) {
				list_remove(itr);
				_destroy_filetxt_job_rec(job);
				job = NULL;
				break;
			}

			if (job->header.job_submit == BATCH_JOB_TIMESTAMP) {
				job->header.job_submit = header.job_submit;
				break;
			}

			if (job->header.job_submit == header.job_submit)
				break;
			else {
				/* If we're looking for a later
				 * record with this job number, we
				 * know that this one is an older,
				 * duplicate record.
				 *   We assume that the newer record
				 * will be created if it doesn't
				 * already exist. */
				job->jobnum_superseded = 1;
			}
		}
	}
	list_iterator_destroy(itr);
	return job;
}

static filetxt_step_rec_t *_find_step_record(filetxt_job_rec_t *job,
					     long stepnum)
{
	filetxt_step_rec_t *step = NULL;
	ListIterator itr = NULL;

	if (!list_count(job->steps))
		return step;

	itr = list_iterator_create(job->steps);
	while((step = (filetxt_step_rec_t *)list_next(itr)) != NULL) {
		if (step->stepnum == stepnum)
			break;
	}
	list_iterator_destroy(itr);
	return step;
}

static int _parse_header(char *f[], filetxt_header_t *header)
{
	header->jobnum = atoi(f[F_JOB]);
	header->partition = xstrdup(f[F_PARTITION]);
	header->job_submit = atoi(f[F_JOB_SUBMIT]);
	header->timestamp = atoi(f[F_TIMESTAMP]);
	header->uid = atoi(f[F_UID]);
	header->gid = atoi(f[F_GID]);
	header->blockid = xstrdup(f[F_BLOCKID]);

	return SLURM_SUCCESS;
}

static int _parse_line(char *f[], void **data, int len)
{
	int i = atoi(f[F_RECTYPE]);
	filetxt_job_rec_t **job = (filetxt_job_rec_t **)data;
	filetxt_step_rec_t **step = (filetxt_step_rec_t **)data;
	filetxt_header_t header;
	_parse_header(f, &header);

	switch(i) {
	case JOB_START:
		*job = _create_filetxt_job_rec(header);
		(*job)->jobname = xstrdup(f[F_JOBNAME]);
		(*job)->track_steps = atoi(f[F_TRACK_STEPS]);
		(*job)->priority = atoi(f[F_PRIORITY]);
		(*job)->ncpus = atoi(f[F_NCPUS]);
		(*job)->nodes = xstrdup(f[F_NODES]);

		for (i=0; (*job)->nodes[i]; i++) { /* discard trailing <CR> */
			if (isspace((*job)->nodes[i]))
				(*job)->nodes[i] = '\0';
		}
		if (!xstrcmp((*job)->nodes, "(null)")) {
			xfree((*job)->nodes);
			(*job)->nodes = xstrdup("(unknown)");
		}
		if (len > F_JOB_ACCOUNT) {
			(*job)->account = xstrdup(f[F_JOB_ACCOUNT]);
			for (i=0; (*job)->account[i]; i++) {
				/* discard trailing <CR> */
				if (isspace((*job)->account[i]))
					(*job)->account[i] = '\0';
			}
		}
		break;
	case JOB_STEP:
		*step = _create_filetxt_step_rec(header);
		(*step)->stepnum = atoi(f[F_JOBSTEP]);
		(*step)->status = atoi(f[F_STATUS]);
		(*step)->exitcode = atoi(f[F_EXITCODE]);
		(*step)->ntasks = atoi(f[F_NTASKS]);
		(*step)->ncpus = atoi(f[F_STEPNCPUS]);
		(*step)->elapsed = atoi(f[F_ELAPSED]);
		(*step)->tot_cpu_sec = atoi(f[F_CPU_SEC]);
		(*step)->tot_cpu_usec = atoi(f[F_CPU_USEC]);
		(*step)->rusage.ru_utime.tv_sec = atoi(f[F_USER_SEC]);
		(*step)->rusage.ru_utime.tv_usec = atoi(f[F_USER_USEC]);
		(*step)->rusage.ru_stime.tv_sec = atoi(f[F_SYS_SEC]);
		(*step)->rusage.ru_stime.tv_usec = atoi(f[F_SYS_USEC]);
		(*step)->rusage.ru_maxrss = atoi(f[F_RSS]);
		(*step)->rusage.ru_ixrss = atoi(f[F_IXRSS]);
		(*step)->rusage.ru_idrss = atoi(f[F_IDRSS]);
		(*step)->rusage.ru_isrss = atoi(f[F_ISRSS]);
		(*step)->rusage.ru_minflt = atoi(f[F_MINFLT]);
		(*step)->rusage.ru_majflt = atoi(f[F_MAJFLT]);
		(*step)->rusage.ru_nswap = atoi(f[F_NSWAP]);
		(*step)->rusage.ru_inblock = atoi(f[F_INBLOCKS]);
		(*step)->rusage.ru_oublock = atoi(f[F_OUBLOCKS]);
		(*step)->rusage.ru_msgsnd = atoi(f[F_MSGSND]);
		(*step)->rusage.ru_msgrcv = atoi(f[F_MSGRCV]);
		(*step)->rusage.ru_nsignals = atoi(f[F_NSIGNALS]);
		(*step)->rusage.ru_nvcsw = atoi(f[F_NVCSW]);
		(*step)->rusage.ru_nivcsw = atoi(f[F_NIVCSW]);
		(*step)->stats.vsize_max = atoi(f[F_MAX_VSIZE]);
		if (len > F_STEPNODES) {
			(*step)->stats.vsize_max_taskid =
				atoi(f[F_MAX_VSIZE_TASK]);
			(*step)->stats.vsize_ave = atof(f[F_AVE_VSIZE]);
			(*step)->stats.rss_max = atoi(f[F_MAX_RSS]);
			(*step)->stats.rss_max_taskid =
				atoi(f[F_MAX_RSS_TASK]);
			(*step)->stats.rss_ave = atof(f[F_AVE_RSS]);
			(*step)->stats.pages_max = atoi(f[F_MAX_PAGES]);
			(*step)->stats.pages_max_taskid =
				atoi(f[F_MAX_PAGES_TASK]);
			(*step)->stats.pages_ave = atof(f[F_AVE_PAGES]);
			(*step)->stats.cpu_min = atoi(f[F_MIN_CPU]);
			(*step)->stats.cpu_min_taskid =
				atoi(f[F_MIN_CPU_TASK]);
			(*step)->stats.cpu_ave = atof(f[F_AVE_CPU]);
			(*step)->stepname = xstrdup(f[F_STEPNAME]);
			(*step)->nodes = xstrdup(f[F_STEPNODES]);
		} else {
			(*step)->stats.vsize_max_taskid = (uint16_t)NO_VAL;
			(*step)->stats.vsize_ave = (float)NO_VAL;
			(*step)->stats.rss_max = NO_VAL;
			(*step)->stats.rss_max_taskid = (uint16_t)NO_VAL;
			(*step)->stats.rss_ave = (float)NO_VAL;
			(*step)->stats.pages_max = NO_VAL;
			(*step)->stats.pages_max_taskid = (uint16_t)NO_VAL;
			(*step)->stats.pages_ave = (float)NO_VAL;
			(*step)->stats.cpu_min = NO_VAL;
			(*step)->stats.cpu_min_taskid = (uint16_t)NO_VAL;
			(*step)->stats.cpu_ave =  (float)NO_VAL;
			(*step)->stepname = NULL;
			(*step)->nodes = NULL;
		}
		if (len > F_MIN_CPU_NODE) {
			(*step)->stats.vsize_max_nodeid =
				atoi(f[F_MAX_VSIZE_NODE]);
			(*step)->stats.rss_max_nodeid =
				atoi(f[F_MAX_RSS_NODE]);
			(*step)->stats.pages_max_nodeid =
				atoi(f[F_MAX_PAGES_NODE]);
			(*step)->stats.cpu_min_nodeid =
				atoi(f[F_MIN_CPU_NODE]);
		} else {
			(*step)->stats.vsize_max_nodeid = NO_VAL;
			(*step)->stats.rss_max_nodeid = NO_VAL;
			(*step)->stats.pages_max_nodeid = NO_VAL;
			(*step)->stats.cpu_min_nodeid = NO_VAL;
		}
		if (len > F_STEP_ACCOUNT)
			(*step)->account = xstrdup(f[F_STEP_ACCOUNT]);
		if (len > F_STEP_REQUID)
			(*step)->requid = atoi(f[F_STEP_REQUID]);
		break;
	case JOB_SUSPEND:
	case JOB_TERMINATED:
		*job = _create_filetxt_job_rec(header);
		(*job)->elapsed = atoi(f[F_TOT_ELAPSED]);
		(*job)->status = atoi(f[F_STATUS]);
		if (len > F_JOB_REQUID)
			(*job)->requid = atoi(f[F_JOB_REQUID]);
		if (len > F_JOB_EXITCODE)
			(*job)->exitcode = atoi(f[F_JOB_EXITCODE]);
		break;
	default:
		error("UNKNOWN TYPE %d",i);
		break;
	}
	return SLURM_SUCCESS;
}

static void _process_start(List job_list, char *f[], int lc,
			   int show_full, int len)
{
	filetxt_job_rec_t *job = NULL;
	filetxt_job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);
	job = _find_job_record(job_list, temp->header, JOB_START);
	if (job) {
		/* in slurm we can get 2 start records one for submit
		 * and one for start, so look at the last one */
		xfree(job->jobname);
		job->jobname = xstrdup(temp->jobname);
		job->track_steps = temp->track_steps;
		job->priority = temp->priority;
		job->ncpus = temp->ncpus;
		xfree(job->nodes);
		job->nodes = xstrdup(temp->nodes);
		xfree(job->account);
		job->account = xstrdup(temp->account);

		_destroy_filetxt_job_rec(temp);
		return;
	}

	job = temp;
	job->show_full = show_full;
	list_append(job_list, job);
	job->job_start_seen = 1;

}

static void _process_step(List job_list, char *f[], int lc,
			  int show_full, int len)
{
	filetxt_job_rec_t *job = NULL;

	filetxt_step_rec_t *step = NULL;
	filetxt_step_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);

	job = _find_job_record(job_list, temp->header, JOB_STEP);

	if (temp->stepnum == -2) {
		_destroy_filetxt_step_rec(temp);
		return;
	}
	if (!job) {	/* fake it for now */
		job = _create_filetxt_job_rec(temp->header);
		job->jobname = xstrdup("(unknown)");
		debug2("Note: JOB_STEP record %u.%u preceded "
		       "JOB_START record at line %d\n",
		       temp->header.jobnum, temp->stepnum, lc);
	}
	job->show_full = show_full;

	if ((step = _find_step_record(job, temp->stepnum))) {

		if (temp->status == JOB_RUNNING) {
			_destroy_filetxt_step_rec(temp);
			return;/* if "R" record preceded by F or CD; unusual */
		}
		if (step->status != JOB_RUNNING) { /* if not JOB_RUNNING */
			fprintf(stderr,
				"Conflicting JOB_STEP record for "
				"jobstep %u.%u at line %d "
				"-- ignoring it\n",
				step->header.jobnum,
				step->stepnum, lc);
			_destroy_filetxt_step_rec(temp);
			return;
		}
		step->status = temp->status;
		step->exitcode = temp->exitcode;
		step->ntasks = temp->ntasks;
		step->ncpus = temp->ncpus;
		step->elapsed = temp->elapsed;
		step->tot_cpu_sec = temp->tot_cpu_sec;
		step->tot_cpu_usec = temp->tot_cpu_usec;
		job->requid = temp->requid;
		step->requid = temp->requid;
		memcpy(&step->rusage, &temp->rusage, sizeof(struct rusage));
		memcpy(&step->stats, &temp->stats, sizeof(slurmdb_stats_t));
		xfree(step->stepname);
		step->stepname = xstrdup(temp->stepname);
		step->end = temp->header.timestamp;
		_destroy_filetxt_step_rec(temp);
		goto got_step;
	}
	step = temp;
	temp = NULL;
	list_append(job->steps, step);
	if (!job->track_steps) {
		/* If we don't have track_steps we want to see
		   if we have multiple steps.  If we only have
		   1 step check the job name against the step
		   name in most all cases it will be
		   different.  If it is different print out
		   the step separate.
		*/
		if (list_count(job->steps) > 1)
			job->track_steps = 1;
		else if (step && step->stepname && job->jobname) {
			if (xstrcmp(step->stepname, job->jobname))
				job->track_steps = 1;
		}
	}

	if (job->header.timestamp == 0)
		job->header.timestamp = step->header.timestamp;
	job->job_step_seen = 1;
	job->ntasks += step->ntasks;
	if (!job->nodes || !xstrcmp(job->nodes, "(unknown)")) {
		xfree(job->nodes);
		job->nodes = xstrdup(step->nodes);
	}

got_step:

	if (job->job_terminated_seen == 0) {	/* If the job is still running,
						   this is the most recent
						   status */
		if ( job->exitcode == 0 )
			job->exitcode = step->exitcode;
		job->status = JOB_RUNNING;
		job->elapsed = step->header.timestamp - job->header.timestamp;
	}
}

static void _process_suspend(List job_list, char *f[], int lc,
			     int show_full, int len)
{
	filetxt_job_rec_t *job = NULL;
	filetxt_job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);
	job = _find_job_record(job_list, temp->header, JOB_SUSPEND);
	if (!job)  {	/* fake it for now */
		job = _create_filetxt_job_rec(temp->header);
		job->jobname = xstrdup("(unknown)");
	}

	job->show_full = show_full;
	if (job->status == JOB_SUSPENDED)
		job->elapsed -= temp->elapsed;

	//job->header.timestamp = temp->header.timestamp;
	job->status = temp->status;
	_destroy_filetxt_job_rec(temp);
}

static void _process_terminated(List job_list, char *f[], int lc,
				int show_full, int len)
{
	filetxt_job_rec_t *job = NULL;
	filetxt_job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);

	if (temp == NULL) {
		error("Unknown proccess terminated");
		return;
	}

	job = _find_job_record(job_list, temp->header, JOB_TERMINATED);
	if (!job) {	/* fake it for now */
		job = _create_filetxt_job_rec(temp->header);
		job->jobname = xstrdup("(unknown)");
		debug("Note: JOB_TERMINATED record for job "
		      "%u preceded "
		      "other job records at line %d\n",
		      temp->header.jobnum, lc);
	} else if (job->job_terminated_seen) {
		if (temp->status == JOB_NODE_FAIL) {
			/* multiple node failures - extra TERMINATED records */
			debug("Note: Duplicate JOB_TERMINATED "
			      "record (nf) for job %u at "
			      "line %d\n",
			      temp->header.jobnum, lc);
			/* JOB_TERMINATED/NF records may be preceded
			 * by a JOB_TERMINATED/CA record; NF is much
			 * more interesting.
			 */
			job->status = temp->status;
			goto finished;
		}

		fprintf(stderr,
			"Conflicting JOB_TERMINATED record (%s) for "
			"job %u at line %d -- ignoring it\n",
			job_state_string(temp->status),
			job->header.jobnum, lc);
		goto finished;
	}
	job->job_terminated_seen = 1;
	job->elapsed = temp->elapsed;
	job->end = temp->header.timestamp;
	job->status = temp->status;
	job->requid = temp->requid;
	job->exitcode = temp->exitcode;
	if (list_count(job->steps) > 1)
		job->track_steps = 1;
	job->show_full = show_full;

finished:
	_destroy_filetxt_job_rec(temp);
}

extern List filetxt_jobacct_process_get_jobs(slurmdb_job_cond_t *job_cond)
{
	char line[BUFFER_SIZE];
	char *f[MAX_RECORD_FIELDS+1];    /* End list with null entry and,
					    possibly, more data than we
					    expected */
	char *fptr = NULL, *filein = NULL;
	int i;
	FILE *fd = NULL;
	int lc = 0;
	int rec_type = -1;
	int job_id = 0, step_id = 0, uid = 0, gid = 0;
	filetxt_job_rec_t *filetxt_job = NULL;
	slurmdb_selected_step_t *selected_step = NULL;
	char *object = NULL;
	ListIterator itr = NULL, itr2 = NULL;
	int show_full = 0;
	List ret_job_list = list_create(slurmdb_destroy_job_rec);
	List job_list = list_create(_destroy_filetxt_job_rec);

	filein = slurm_get_accounting_storage_loc();

	if (job_cond) {
		if (!job_cond->duplicates)
			itr2 = list_iterator_create(ret_job_list);
	}

	fd = _open_log_file(filein);

	while (fgets(line, BUFFER_SIZE, fd)) {
		lc++;
		fptr = line;	/* break the record into NULL-
				   terminated strings */
		for (i = 0; i < MAX_RECORD_FIELDS; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL) {
				fptr = strstr(f[i], "\n");
				if (fptr)
					*fptr = 0;
				break;
			} else {
				*fptr++ = 0;
			}
		}
		if (i < MAX_RECORD_FIELDS)
			i++;
		f[i] = 0;

		if (i < HEADER_LENGTH) {
			continue;
		}

		rec_type = atoi(f[F_RECTYPE]);
		job_id = atoi(f[F_JOB]);
		uid = atoi(f[F_UID]);
		gid = atoi(f[F_GID]);

		if (rec_type == JOB_STEP)
			step_id = atoi(f[F_JOBSTEP]);
		else
			step_id = NO_VAL;

		if (!job_cond) {
			show_full = 1;
			goto no_cond;
		}

		if (job_cond->userid_list
		    && list_count(job_cond->userid_list)) {
			itr = list_iterator_create(job_cond->userid_list);
			while((object = list_next(itr))) {
				if (atoi(object) == uid) {
					list_iterator_destroy(itr);
					goto founduid;
				}
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	founduid:

		if (job_cond->groupid_list
		    && list_count(job_cond->groupid_list)) {
			itr = list_iterator_create(job_cond->groupid_list);
			while((object = list_next(itr))) {
				if (atoi(object) == gid) {
					list_iterator_destroy(itr);
					goto foundgid;
				}
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundgid:

		if ((rec_type == JOB_START) && job_cond->jobname_list
		    && list_count(job_cond->jobname_list)) {
			itr = list_iterator_create(job_cond->jobname_list);
			while((object = list_next(itr))) {
				if (!xstrcasecmp(f[F_JOBNAME], object)) {
					list_iterator_destroy(itr);
					goto foundjobname;
				}
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundjobname:

		if (job_cond->step_list
		    && list_count(job_cond->step_list)) {
			itr = list_iterator_create(job_cond->step_list);
			while((selected_step = list_next(itr))) {
				if (selected_step->jobid != job_id)
					continue;
				/* job matches; does the step? */
				if (selected_step->stepid == NO_VAL) {
					show_full = 1;
					list_iterator_destroy(itr);
					goto foundjob;
				} else if (rec_type != JOB_STEP
					   || selected_step->stepid
					   == step_id) {
					list_iterator_destroy(itr);
					goto foundjob;
				}
			}
			list_iterator_destroy(itr);
			continue;	/* no match */
		} else {
			show_full = 1;
		}
	foundjob:

		if ((rec_type == JOB_START) && job_cond->partition_list
		    && list_count(job_cond->partition_list)) {
			itr = list_iterator_create(job_cond->partition_list);
			while((object = list_next(itr)))
				if (!xstrcasecmp(f[F_PARTITION], object)) {
					list_iterator_destroy(itr);
					goto foundp;
				}
			list_iterator_destroy(itr);
			continue;	/* no match */
		}
	foundp:

	no_cond:

		/* Build suitable tables with all the data */
		switch(rec_type) {
		case JOB_START:
			if (i < F_JOB_ACCOUNT) {
				error("Bad data on a Job Start");
				_show_rec(f);
			} else
				_process_start(job_list, f, lc, show_full, i);
			break;
		case JOB_STEP:
			if (i < F_MAX_VSIZE) {
				error("Bad data on a Step entry");
				_show_rec(f);
			} else
				_process_step(job_list, f, lc, show_full, i);
			break;
		case JOB_SUSPEND:
			if (i < F_JOB_REQUID) {
				error("Bad data on a Suspend entry");
				_show_rec(f);
			} else
				_process_suspend(job_list, f, lc,
						 show_full, i);
			break;
		case JOB_TERMINATED:
			if (i < F_JOB_REQUID) {
				error("Bad data on a Job Term");
				_show_rec(f);
			} else
				_process_terminated(job_list, f, lc,
						    show_full, i);
			break;
		default:
			debug("Invalid record at line %d of input file", lc);
			_show_rec(f);
			break;
		}
	}

	if (ferror(fd)) {
		perror(filein);
		exit(1);
	}
	fclose(fd);

	itr = list_iterator_create(job_list);

	while((filetxt_job = list_next(itr))) {
		slurmdb_job_rec_t *slurmdb_job =
			_slurmdb_create_job_rec(filetxt_job, job_cond);
		if (slurmdb_job) {
			slurmdb_job_rec_t *curr_job = NULL;
			if (itr2) {
				list_iterator_reset(itr2);
				while((curr_job = list_next(itr2))) {
					if (curr_job->jobid ==
					    slurmdb_job->jobid) {
						list_delete_item(itr2);
						debug3("removing duplicate "
						       "of job %d",
						       slurmdb_job->jobid);
						break;
					}
				}
			}
			list_append(ret_job_list, slurmdb_job);
		}
	}

	if (itr2)
		list_iterator_destroy(itr2);

	list_iterator_destroy(itr);
	FREE_NULL_LIST(job_list);

	xfree(filein);

	return ret_job_list;
}

extern int filetxt_jobacct_process_archive(slurmdb_archive_cond_t *arch_cond)
{
	char	line[BUFFER_SIZE],
		*f[EXPIRE_READ_LENGTH],
		*fptr = NULL,
		*logfile_name = NULL,
		*old_logfile_name = NULL,
		*filein = NULL,
		*object = NULL;
	int	file_err=0,
		new_file,
		i = 0,
		rc = SLURM_ERROR;
	expired_rec_t *exp_rec = NULL;
	expired_rec_t *exp_rec2 = NULL;
	List keep_list = list_create(_destroy_exp);
	List exp_list = list_create(_destroy_exp);
	List other_list = list_create(_destroy_exp);
	struct	stat statbuf;
	mode_t	prot = 0600;
	uid_t	uid;
	gid_t	gid;
	FILE	*expired_logfile = NULL,
		*new_logfile = NULL;
	FILE *fd = NULL;
	int lc=0;
	int rec_type = -1;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_job_cond_t *job_cond = NULL;

	/* Figure out our expiration date */
	time_t		expiry;

	if (!arch_cond || !arch_cond->job_cond) {
		error("no job_cond was given for archive");
		return SLURM_ERROR;
	}

	job_cond = arch_cond->job_cond;

	if (!arch_cond->archive_script)
		filein = slurm_get_accounting_storage_loc();
	else
		filein = arch_cond->archive_script;

	expiry = time(NULL) - job_cond->usage_end;

	debug("Purging jobs completed prior to %d", (int)expiry);

	/* Open the current or specified logfile, or quit */
	fd = _open_log_file(filein);
	if (stat(filein, &statbuf)) {
		perror("stat'ing logfile");
		goto finished;
	}
	if ((statbuf.st_mode & S_IFLNK) == S_IFLNK) {
		error("%s is a symbolic link; --expire requires "
		      "a hard-linked file name", filein);
		goto finished;
	}
	if (!(statbuf.st_mode & S_IFREG)) {
		error("%s is not a regular file; --expire "
			"only works on accounting log files",
			filein);
		goto finished;
	}
	prot = statbuf.st_mode & 0777;
	gid  = statbuf.st_gid;
	uid  = statbuf.st_uid;
	old_logfile_name = _prefix_filename(filein, ".old.");
	if (stat(old_logfile_name, &statbuf)) {
		if (errno != ENOENT) {
			fprintf(stderr,"Error checking for %s: ",
				old_logfile_name);
			perror("");
			goto finished;
		}
	} else {
		error("Warning! %s exists -- please remove "
			"or rename it before proceeding",
			old_logfile_name);
		goto finished;
	}

	/* create our initial buffer */
	while (fgets(line, BUFFER_SIZE, fd)) {
		lc++;
		fptr = line;	/* break the record into NULL-
				   terminated strings */
		exp_rec = xmalloc(sizeof(expired_rec_t));
		exp_rec->line = xstrdup(line);

		for (i = 0; i < EXPIRE_READ_LENGTH; i++)
			f[i] = fptr;	/* Initialization for bad data read */
		for (i = 0; i < EXPIRE_READ_LENGTH; i++) {
			f[i] = fptr;
			fptr = strstr(fptr, " ");
			if (fptr == NULL)
				break;
			else
				*fptr++ = 0;
		}

		exp_rec->job = atoi(f[F_JOB]);
		exp_rec->job_submit = atoi(f[F_JOB_SUBMIT]);

		rec_type = atoi(f[F_RECTYPE]);
		/* Odd, but complain some other time */
		if (rec_type == JOB_TERMINATED) {
			if (expiry < atoi(f[F_TIMESTAMP])) {
				list_append(keep_list, exp_rec);
				continue;
			}
			if ((rec_type == JOB_START) && job_cond->partition_list
			    && list_count(job_cond->partition_list)) {
				itr = list_iterator_create(
					job_cond->partition_list);
				while((object = list_next(itr)))
					if (!xstrcasecmp(f[F_PARTITION],
							 object))
						break;

				list_iterator_destroy(itr);
				if (!object)
					continue;	/* no match */
			}

			list_append(exp_list, exp_rec);
			debug2("Selected: %8d %d",
			       exp_rec->job,
			       (int)exp_rec->job_submit);
		} else {
			list_append(other_list, exp_rec);
		}
	}
	if (!list_count(exp_list)) {
		debug3("No job records were purged.");
		goto finished;
	}
	logfile_name = xmalloc(strlen(filein)+sizeof(".expired"));
	sprintf(logfile_name, "%s.expired", filein);
	new_file = stat(logfile_name, &statbuf);
	if ((expired_logfile = fopen(logfile_name, "a"))==NULL) {
		error("Error while opening %s",
			logfile_name);
		perror("");
		xfree(logfile_name);
		goto finished;
	}

	if (new_file) {  /* By default, the expired file looks like the log */
		chmod(logfile_name, prot);
		if (chown(logfile_name, uid, gid) == -1)
			error("Couldn't change ownership of %s to %u:%u",
			      logfile_name, uid, gid);
	}
	xfree(logfile_name);

	logfile_name = _prefix_filename(filein, ".new.");
	if ((new_logfile = fopen(logfile_name, "w"))==NULL) {
		error("Error while opening %s",
			logfile_name);
		perror("");
		fclose(expired_logfile);
		goto finished;
	}
	chmod(logfile_name, prot);     /* preserve file protection */
	if (chown(logfile_name, uid, gid) == -1)/* and ownership */
		error("2 Couldn't change ownership of %s to %u:%u",
		      logfile_name, uid, gid);
	/* Use line buffering to allow us to safely write
	 * to the log file at the same time as slurmctld. */
	if (setvbuf(new_logfile, NULL, _IOLBF, 0)) {
		perror("setvbuf()");
		fclose(expired_logfile);
		goto finished2;
	}

	list_sort(exp_list, (ListCmpF) _cmp_jrec);
	list_sort(keep_list, (ListCmpF) _cmp_jrec);

	/* if (params->opt_verbose > 2) { */
/* 		error("--- contents of exp_list ---"); */
/* 		itr = list_iterator_create(exp_list); */
/* 		while((exp_rec = list_next(itr))) */
/* 			error("%d", exp_rec->job); */
/* 		error("---- end of exp_list ---"); */
/* 		list_iterator_destroy(itr); */
/* 	} */
	/* write the expired file */
	itr = list_iterator_create(exp_list);
	while((exp_rec = list_next(itr))) {
		itr2 = list_iterator_create(other_list);
		while((exp_rec2 = list_next(itr2))) {
			if ((exp_rec2->job != exp_rec->job)
			   || (exp_rec2->job_submit != exp_rec->job_submit))
				continue;
			if (fputs(exp_rec2->line, expired_logfile)<0) {
				perror("writing expired_logfile");
				list_iterator_destroy(itr2);
				list_iterator_destroy(itr);
				fclose(expired_logfile);
				goto finished2;
			}
			list_remove(itr2);
			_destroy_exp(exp_rec2);
		}
		list_iterator_destroy(itr2);
		if (fputs(exp_rec->line, expired_logfile)<0) {
			perror("writing expired_logfile");
			list_iterator_destroy(itr);
			fclose(expired_logfile);
			goto finished2;
		}
	}
	list_iterator_destroy(itr);
	fclose(expired_logfile);

	/* write the new log */
	itr = list_iterator_create(keep_list);
	while((exp_rec = list_next(itr))) {
		itr2 = list_iterator_create(other_list);
		while((exp_rec2 = list_next(itr2))) {
			if (exp_rec2->job != exp_rec->job)
				continue;
			if (fputs(exp_rec2->line, new_logfile)<0) {
				perror("writing keep_logfile");
				list_iterator_destroy(itr2);
				list_iterator_destroy(itr);
				goto finished2;
			}
			list_remove(itr2);
			_destroy_exp(exp_rec2);
		}
		list_iterator_destroy(itr2);
		if (fputs(exp_rec->line, new_logfile)<0) {
			perror("writing keep_logfile");
			list_iterator_destroy(itr);
			goto finished2;
		}
	}
	list_iterator_destroy(itr);

	/* write records in other_list to new log */
	itr = list_iterator_create(other_list);
	while((exp_rec = list_next(itr))) {
		if (fputs(exp_rec->line, new_logfile)<0) {
			perror("writing keep_logfile");
			list_iterator_destroy(itr);
			goto finished2;
		}
	}
	list_iterator_destroy(itr);

	if (rename(filein, old_logfile_name)) {
		perror("renaming logfile to .old.");
		goto finished2;
	}
	if (rename(logfile_name, filein)) {
		perror("renaming new logfile");
		/* undo it? */
		if (!rename(old_logfile_name, filein))
			error("Please correct the problem "
				"and try again");
		else
			error("SEVERE ERROR: Current accounting "
				"log may have been renamed %s;\n"
				"please rename it to \"%s\" if necessary, "
			        "and try again",
				old_logfile_name, filein);
		goto finished2;
	}
	fflush(new_logfile);	/* Flush the buffers before forking */
	fflush(fd);

	file_err = slurm_reconfigure();
	if (file_err) {
		file_err = 1;
		error("Error: Attempt to reconfigure SLURM failed.");
		if (rename(old_logfile_name, filein)) {
			perror("renaming logfile from .old.");
			goto finished2;
		}

	}
	if (fseek(fd, 0, SEEK_CUR)) {	/* clear EOF */
		perror("looking for late-arriving records");
		goto finished2;
	}

	/* reopen new logfile in append mode, since slurmctld may write it */
	if ((new_logfile = freopen(filein, "a", new_logfile)) == NULL) {
		perror("reopening new logfile");
		goto finished2;
	}

	while (fgets(line, BUFFER_SIZE, fd)) {
		if (fputs(line, new_logfile) < 0) {
			perror("writing final records");
			goto finished2;
		}
	}
	rc = SLURM_SUCCESS;

	printf("%d jobs expired.\n", list_count(exp_list));
finished2:
	if (new_logfile)
		fclose(new_logfile);
	if (!file_err) {
		if (unlink(old_logfile_name) == -1)
			error("Unable to unlink old logfile %s: %m",
			      old_logfile_name);
	}
finished:
	xfree(filein);

	fclose(fd);
	FREE_NULL_LIST(exp_list);
	FREE_NULL_LIST(keep_list);
	FREE_NULL_LIST(other_list);
	xfree(old_logfile_name);
	xfree(logfile_name);

	return rc;
}
