/*****************************************************************************\
 *  process.c - process functions for sacct
 *
 *  $Id: process.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

#include "sacct.h"

job_rec_t *_find_job_record(acct_header_t header);
step_rec_t *_find_step_record(job_rec_t *job, long jobstep);
job_rec_t *_init_job_rec(acct_header_t header, int lc);
int _parse_line(char *f[], void **data);

job_rec_t *_find_job_record(acct_header_t header)
{
	int	i;
	job_rec_t *job = NULL;
	ListIterator itr = list_iterator_create(jobs);

	while((job = (job_rec_t *)list_next(itr)) != NULL) {
		if (job->header.jobnum == header.jobnum) {
			if(job->header.job_start == BATCH_JOB_TIMESTAMP) {
				job->header.job_start = header.job_start;
				break;
			}
			
			if(job->header.job_start == header.job_start)
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

step_rec_t *_find_step_record(job_rec_t *job, long stepnum)
{
	int	i;
	step_rec_t *step = NULL;
	ListIterator itr = NULL;

	if(!list_count(job->steps))
		return step;
	
	itr = list_iterator_create(job->steps);
	while((step = (step_rec_t *)list_next(itr)) != NULL) {
		if (step->stepnum == stepnum)
			break;
	}
	list_iterator_destroy(itr);
	return step;
}

job_rec_t *_init_job_rec(acct_header_t header, int lc)
{
	long	j;
	char	*p;

	job_rec_t *job = xmalloc(sizeof(job_rec_t));

	job->header.jobnum = header.jobnum;
	job->header.partition = xstrdup(header.partition);
	job->header.job_start = header.job_start;
	job->header.timestamp = header.timestamp;
	job->header.uid = header.uid;
	job->header.gid = header.gid;
	job->job_start_seen = 0;
	job->job_step_seen = 0;
	job->job_terminated_seen = 0;
	job->jobnum_superseded = 0;
	job->jobname = xstrdup("(unknown)");
	job->status = JOB_PENDING;
	job->tot_cpu_sec = 0;
	job->tot_cpu_usec = 0;
	job->rusage.ru_utime.tv_sec = 0;
	job->rusage.ru_utime.tv_usec += 0;
	job->rusage.ru_stime.tv_sec += 0;
	job->rusage.ru_stime.tv_usec += 0;
	job->rusage.ru_inblock += 0;
	job->rusage.ru_oublock += 0;
	job->rusage.ru_msgsnd += 0;
	job->rusage.ru_msgrcv += 0;
	job->rusage.ru_nsignals += 0;
	job->rusage.ru_nvcsw += 0;
	job->rusage.ru_nivcsw += 0;
	job->rusage.ru_maxrss = 0;
	job->rusage.ru_ixrss = 0;
	job->rusage.ru_idrss = 0;
	job->rusage.ru_isrss = 0;
	job->rusage.ru_minflt = 0;
	job->rusage.ru_majflt = 0;
	job->rusage.ru_nswap = 0;
	job->vsize = 0;
	job->psize = 0;
	job->error = 0;
	job->steps = list_create(destroy_step);
	return job;
}



step_rec_t *_init_step(long jobnum, char *f[], int lc)
{
	long	j;
	char	*p;
	step_rec_t *step = NULL;

	return step;
}

int _parse_header(char *f[], acct_header_t *header)
{
	header->jobnum = atoi(f[F_JOB]);
	header->partition = xstrdup(f[F_PARTITION]);
	header->job_start = atoi(f[F_JOB_START]);
	header->timestamp = atoi(f[F_TIMESTAMP]);
	header->uid = atoi(f[F_UID]);
	header->gid = atoi(f[F_GID]);
	return SLURM_SUCCESS;
}

int _parse_line(char *f[], void **data)
{
	char *p = NULL;
	int i = atoi(f[F_RECTYPE]);
	job_rec_t **job = (job_rec_t **)data;
	step_rec_t **step = (step_rec_t **)data;
	
	switch(i) {
	case JOB_START:
		*job = xmalloc(sizeof(job_rec_t));
		_parse_header(f, &(*job)->header);
		(*job)->jobname = xstrdup(f[F_JOBNAME]);
		(*job)->track_steps = atoi(f[F_TRACK_STEPS]);
		(*job)->priority = atoi(f[F_PRIORITY]);
		(*job)->ncpus = atoi(f[F_NCPUS]);
		(*job)->nodes = xstrdup(f[F_NODES]);
		for (i=0; (*job)->nodes[i]; i++)  /* discard trailing <CR> */
			if (isspace((*job)->nodes[i]))
				(*job)->nodes[i] = 0;
		if (!strcmp((*job)->nodes, "(null)")) {
			xfree((*job)->nodes);
			(*job)->nodes = xstrdup("unknown");
		}
		break;
	case JOB_STEP:
		*step = xmalloc(sizeof(step_rec_t));
		_parse_header(f, &(*step)->header);
		(*step)->stepnum = atoi(f[F_JOBSTEP]);
		(*step)->status = atoi(f[F_STATUS]);
		(*step)->error = atoi(f[F_ERROR]);
		(*step)->ntasks = atoi(f[F_NTASKS]);
		(*step)->ncpus = atoi(f[F_NCPUS]);
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
		(*step)->vsize = atoi(f[F_VSIZE]);
		(*step)->psize = atoi(f[F_PSIZE]);
		(*step)->stepname = xstrdup(f[F_STEPNAME]);
		break;
	case JOB_SUSPEND:
	case JOB_TERMINATED:
		*job = xmalloc(sizeof(job_rec_t));
		_parse_header(f, &(*job)->header);
		(*job)->elapsed = atoi(f[F_TOT_ELAPSED]);
		(*job)->status = atoi(f[F_STATUS]);		
		break;
	default:
		printf("UNKOWN TYPE %d",i);
		break;
	}
}

void process_start(char *f[], int lc)
{
	int	i;
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp);
	job = _find_job_record(temp->header);
	if (job) {	/* Hmmm... that's odd */
		fprintf(stderr,
			"Conflicting JOB_START for job %ld at"
			" line %ld -- ignoring it\n",
			job->header.jobnum, lc);
		inputError++;
		destroy_job(temp);
		return;
	}
	
	job = _init_job_rec(temp->header, lc);
	list_append(jobs, job);
	job->job_start_seen = 1;
	job->header.uid = temp->header.uid;
	job->header.gid = temp->header.gid;
	xfree(job->jobname);
	job->jobname = xstrdup(temp->jobname);
	job->priority = temp->priority;
	job->track_steps = temp->track_steps;
	job->ncpus = temp->ncpus;
	xfree(job->nodes);
	job->nodes = xstrdup(temp->nodes);
	destroy_job(temp);
}

void process_step(char *f[], int lc)
{
	job_rec_t *job = NULL;
	
	step_rec_t *step = NULL;
	step_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp);

	job = _find_job_record(temp->header);
	
	if (temp->stepnum == -2) {
		destroy_step(temp);
		return;
	}
	if (!job) {	/* fake it for now */
		job = _init_job_rec(temp->header, lc);
		if ((params.opt_verbose > 1) 
		    && (params.opt_jobstep_list==NULL)) 
			fprintf(stderr, 
				"Note: JOB_STEP record %ld.%ld preceded "
				"JOB_START record at line %ld\n",
				temp->header.jobnum, temp->stepnum, lc);
	}
	if ((step = _find_step_record(job, temp->stepnum))) {
		
		if (temp->status == JOB_RUNNING) {
			destroy_step(temp);
			return;/* if "R" record preceded by F or CD; unusual */
		}
		if (step->status != JOB_RUNNING) { /* if not JOB_RUNNING */
			fprintf(stderr,
				"Conflicting JOB_STEP record for "
				"jobstep %ld.%ld at line %ld "
				"-- ignoring it\n",
				step->header.jobnum, 
				step->stepnum, lc);
			inputError++;
			
			destroy_step(temp);
			return;
		}
		step->status = temp->status;
		step->error = temp->error;
		step->ntasks = temp->ntasks;
		step->ncpus = temp->ncpus;
		step->elapsed = temp->elapsed;
		step->tot_cpu_sec = temp->tot_cpu_sec;
		step->tot_cpu_usec = temp->tot_cpu_usec;
		memcpy(&step->rusage, &temp->rusage, sizeof(struct rusage));
		step->vsize = temp->vsize;
		step->psize = temp->psize;
		xfree(step->stepname);
		step->stepname = xstrdup(temp->stepname);
		goto got_step;
	}
	step = temp;
	temp = NULL;
	list_append(job->steps, step);
	
	job->job_step_seen = 1;
	job->ntasks += step->ntasks;
got_step:
	destroy_step(temp);
	
	if (job->job_terminated_seen == 0) {	/* If the job is still running,
						   this is the most recent
						   status */
		job->header.timestamp = step->header.timestamp;
		job->status = JOB_RUNNING;
		if ( job->error == 0 )
			job->error = step->error;
		job->elapsed = time(NULL) - job->header.timestamp;
	}
	/* now aggregate the aggregatable */
	job->tot_cpu_sec += step->tot_cpu_sec;
	job->tot_cpu_usec += step->tot_cpu_usec;
	job->rusage.ru_utime.tv_sec += step->rusage.ru_utime.tv_sec;
	job->rusage.ru_utime.tv_usec += step->rusage.ru_utime.tv_usec;
	job->rusage.ru_stime.tv_sec += step->rusage.ru_stime.tv_sec;
	job->rusage.ru_stime.tv_usec += step->rusage.ru_stime.tv_usec;
	job->rusage.ru_inblock += step->rusage.ru_inblock;
	job->rusage.ru_oublock += step->rusage.ru_oublock;
	job->rusage.ru_msgsnd += step->rusage.ru_msgsnd;
	job->rusage.ru_msgrcv += step->rusage.ru_msgrcv;
	job->rusage.ru_nsignals += step->rusage.ru_nsignals;
	job->rusage.ru_nvcsw += step->rusage.ru_nvcsw;
	job->rusage.ru_nivcsw += step->rusage.ru_nivcsw;
		
	/* and finally the maximums for any process */
	job->rusage.ru_maxrss = MAX(job->rusage.ru_maxrss, 
				    step->rusage.ru_maxrss);
	job->rusage.ru_ixrss = MAX(job->rusage.ru_ixrss,
				   step->rusage.ru_ixrss);
	job->rusage.ru_idrss = MAX(job->rusage.ru_idrss,
				   step->rusage.ru_idrss);
	job->rusage.ru_isrss = MAX(job->rusage.ru_isrss,
				   step->rusage.ru_isrss);
	job->rusage.ru_minflt = MAX(job->rusage.ru_minflt,
				    step->rusage.ru_minflt);
	job->rusage.ru_majflt = MAX(job->rusage.ru_majflt,
				    step->rusage.ru_majflt);
	job->rusage.ru_nswap = MAX(job->rusage.ru_nswap,
				   step->rusage.ru_nswap);
	job->psize = MAX(job->psize, step->psize);
	job->vsize = MAX(job->vsize, step->vsize);
	job->ncpus = MAX(job->ncpus, step->ncpus);
}

void process_suspend(char *f[], int lc)
{
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp);
	job = _find_job_record(temp->header);
	if (!job)    
		job = _init_job_rec(temp->header, lc);
	
	if (job->status == JOB_SUSPENDED) 
		job->elapsed -= temp->elapsed;

	job->header.timestamp = temp->header.timestamp;
	job->status = temp->status;
	destroy_job(temp);
}
	
void process_terminated(char *f[], int lc)
{
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp);
	job = _find_job_record(temp->header);
	if (!job) {	/* fake it for now */
		job = _init_job_rec(temp->header, lc);
		if (params.opt_verbose > 1) 
			fprintf(stderr, "Note: JOB_TERMINATED record for job "
				"%ld preceded "
				"other job records at line %ld\n",
				temp->header.jobnum, lc);
	} else if (job->job_terminated_seen) {
		if (temp->status == JOB_NODE_FAIL) {
			/* multiple node failures - extra TERMINATED records */
			if (params.opt_verbose > 1)
				fprintf(stderr, 
					"Note: Duplicate JOB_TERMINATED "
					"record (nf) for job %ld at "
					"line %ld\n", 
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
			"job %ld at line %ld -- ignoring it\n",
			decode_status_int(temp->status), job, lc);
		inputError++;
		goto finished;
	}
	job->job_terminated_seen = 1;
	job->elapsed = temp->elapsed;
	job->header.timestamp = temp->header.timestamp;
	job->status = temp->status;
finished:
	destroy_job(temp);
}

void destroy_job(void *object)
{
	job_rec_t *job = (job_rec_t *)object;
	if (job) {
		if(job->steps)
			list_destroy(job->steps);
		xfree(job->header.partition);
		xfree(job->jobname);
		xfree(job->nodes);
		xfree(job);
	}
}

void destroy_step(void *object)
{
	step_rec_t *step = (step_rec_t *)object;
	if (step) {
		xfree(step->header.partition);
		xfree(step->stepname);
		xfree(step);
	}
}
