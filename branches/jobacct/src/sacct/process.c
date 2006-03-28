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

int _same_start(job_rec_t *job, job_rec_t *temp_rec);
int _same_step(step_rec_t *step, step_rec_t *temp_rec);
int _same_terminated(job_rec_t *job, job_rec_t *temp_rec);
job_rec_t *_find_job_record(acct_header_t header);
step_rec_t *_find_step_record(job_rec_t *job, long jobstep);
job_rec_t *_init_job_rec(acct_header_t header, int lc);
int _parse_line(char *f[], void **data);
/* Have we seen this data already?  */

int _same_start(job_rec_t *job, job_rec_t *temp_rec)
{
	if (job->header.uid != temp_rec->header.uid)
		return 0;
	if (job->header.gid != temp_rec->header.gid)
		return 0;
	if (strcmp(job->jobname, temp_rec->jobname))
		return 0;
	if (job->batch != temp_rec->batch)
		return 0;
	if (job->priority != temp_rec->priority)
		return 0;
	if (job->ncpus != temp_rec->ncpus)
		return 0;
	if (strcmp(job->nodes, temp_rec->nodes))
		return 0;
	return 1;
}

int _same_step(step_rec_t *step, step_rec_t *temp_rec)
{ 
	if (strcmp(step->finished, temp_rec->finished))
		return 0;
	if (step->status != temp_rec->status)
		return 0;
	if (step->error != temp_rec->error)
		return 0;
	if (step->ntasks != temp_rec->ntasks)
		return 0;
	if (step->ncpus != temp_rec->ncpus)
		return 0;
	if (step->elapsed != temp_rec->elapsed)
		return 0;
	if (temp_rec->tot_cpu_sec != step->tot_cpu_usec)
		return 0;
	if (temp_rec->tot_cpu_usec != step->tot_cpu_usec)
		return 0;
	if (temp_rec->rusage.ru_utime.tv_sec != step->rusage.ru_utime.tv_sec)
		return 0;
	if (temp_rec->rusage.ru_utime.tv_usec != step->rusage.ru_utime.tv_usec)
		return 0;
	if (temp_rec->rusage.ru_stime.tv_sec != step->rusage.ru_stime.tv_sec)
		return 0;
	if (temp_rec->rusage.ru_stime.tv_usec != step->rusage.ru_stime.tv_usec)
		return 0;
	if (temp_rec->rusage.ru_inblock != step->rusage.ru_inblock)
		return 0;
	if (temp_rec->rusage.ru_oublock != step->rusage.ru_oublock)
		return 0;
	if (temp_rec->rusage.ru_msgsnd != step->rusage.ru_msgsnd)
		return 0;
	if (temp_rec->rusage.ru_msgrcv != step->rusage.ru_msgrcv)
		return 0;
	if (temp_rec->rusage.ru_nsignals != step->rusage.ru_nsignals)
		return 0;
	if (temp_rec->rusage.ru_nvcsw != step->rusage.ru_nvcsw)
		return 0;
	if (temp_rec->rusage.ru_nivcsw != step->rusage.ru_nivcsw)
		return 0;
	if(temp_rec->rusage.ru_maxrss != step->rusage.ru_maxrss)
		return 0;
	if(temp_rec->rusage.ru_ixrss != step->rusage.ru_ixrss)
		return 0;
	if(temp_rec->rusage.ru_idrss != step->rusage.ru_idrss)
		return 0;
	if(temp_rec->rusage.ru_isrss != step->rusage.ru_isrss)
		return 0;
	if(temp_rec->rusage.ru_minflt != step->rusage.ru_minflt)
		return 0;
	if(temp_rec->rusage.ru_majflt != step->rusage.ru_majflt)
		return 0;
	if(temp_rec->rusage.ru_nswap != step->rusage.ru_nswap)
		return 0;
	if (temp_rec->psize != step->psize)
		return 0;
	if (temp_rec->vsize != step->vsize)
		return 0;

	return 1;		/* they are the same */ 
}

int _same_terminated(job_rec_t *job, job_rec_t *temp_rec)
{
	if (job->job_terminated_seen != 1)
		return 0;
	if (job->elapsed != temp_rec->elapsed)
		return 0;
	if (strcmp(job->finished, temp_rec->finished))
		return 0;
	if (job->status != temp_rec->status)
		return 0;
	return 1;
}

job_rec_t *_find_job_record(acct_header_t header)
{
	int	i;
	job_rec_t *job = NULL;
	ListIterator itr = list_iterator_create(jobs);

	while((job = (job_rec_t *)list_next(itr)) != NULL) {
		if (job->header.jobnum == header.jobnum) {
			if(!strcmp(job->header.submitted, BATCH_JOB_SUBMIT)) {
				strncpy(job->header.submitted, 
					header.submitted, TIMESTAMP_LENGTH);
				break;
			}
			
			if(!strcmp(job->header.submitted, header.submitted))
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
	strncpy(job->header.submitted, header.submitted, TIMESTAMP_LENGTH);
	job->header.starttime = header.starttime;
	job->header.uid = header.uid;
	job->header.gid = header.gid;
	job->job_start_seen = 0;
	job->job_step_seen = 0;
	job->job_terminated_seen = 0;
	job->jobnum_superseded = 0;
	job->first_jobstep = -1;
	job->jobname = xstrdup("(unknown)");
	job->status = JOB_RUNNING;
	strncpy(job->finished, "?", TIMESTAMP_LENGTH);
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

int _parse_line(char *f[], void **data)
{
	char *p = NULL;
	int i = atoi(f[F_RECTYPE]);
	job_rec_t **job = (job_rec_t **)data;
	step_rec_t **step = (step_rec_t **)data;
	
	switch(i) {
	case JOB_START:
		*job = xmalloc(sizeof(job_rec_t));
		(*job)->header.jobnum = strtol(f[F_JOB], NULL, 10);
		(*job)->header.partition = xstrdup(f[F_PARTITION]);
		strncpy((*job)->header.submitted, 
			f[F_SUBMITTED], TIMESTAMP_LENGTH);
		(*job)->header.starttime = strtol(f[F_STARTTIME], NULL, 10);
		(*job)->header.uid = atoi(f[F_UIDGID]);
		if ((p=strstr(f[F_UIDGID], ".")))
			(*job)->header.gid=atoi(++p);
		(*job)->jobname = xstrdup(f[F_JOBNAME]);
		(*job)->batch = atoi(f[F_BATCH]);
		(*job)->priority = atoi(f[F_PRIORITY]);
		(*job)->ncpus = strtol(f[F_NCPUS], NULL, 10);
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
		(*step)->header.jobnum = strtol(f[F_JOB], NULL, 10);
		strncpy((*step)->header.submitted, 
			f[F_SUBMITTED], TIMESTAMP_LENGTH);
		if (!strcmp(f[F_JOBSTEP], NOT_JOBSTEP)) {
			(*step)->stepnum = -2;
			return;
		}
		(*step)->header.partition = xstrdup(f[F_PARTITION]);
		(*step)->header.starttime = strtol(f[F_STARTTIME], NULL, 10);
		(*step)->header.uid = atoi(f[F_UIDGID]);
		if ((p=strstr(f[F_UIDGID], ".")))
			(*step)->header.gid=atoi(++p);
		
		(*step)->stepnum = strtol(f[F_JOBSTEP], NULL, 10);
		strncpy((*step)->finished, f[F_FINISHED], TIMESTAMP_LENGTH);
		(*step)->status = atoi(f[F_STATUS]);
		(*step)->error = strtol(f[F_ERROR], NULL, 10);
		(*step)->ntasks = strtol(f[F_NTASKS], NULL, 10);
		(*step)->ncpus = strtol(f[F_NCPUS], NULL, 10);
		(*step)->elapsed = strtol(f[F_ELAPSED], NULL, 10);
		
		(*step)->tot_cpu_sec = strtol(f[F_CPU_SEC], NULL, 10);
		(*step)->tot_cpu_usec = strtol(f[F_CPU_USEC], NULL, 10);
		(*step)->rusage.ru_utime.tv_sec = 
			strtol(f[F_USER_SEC], NULL, 10);
		(*step)->rusage.ru_utime.tv_usec = 
			strtol(f[F_USER_USEC], NULL, 10);
		(*step)->rusage.ru_stime.tv_sec = strtol(f[F_SYS_SEC], NULL, 10);
		(*step)->rusage.ru_stime.tv_usec = 
			strtol(f[F_SYS_USEC], NULL, 10);
		(*step)->rusage.ru_maxrss = strtol(f[F_RSS], NULL,10);
		(*step)->rusage.ru_ixrss = strtol(f[F_IXRSS], NULL,10);
		(*step)->rusage.ru_idrss = strtol(f[F_IDRSS], NULL,10);
		(*step)->rusage.ru_isrss = strtol(f[F_ISRSS], NULL,10);
		(*step)->rusage.ru_minflt = strtol(f[F_MINFLT], NULL,10);
		(*step)->rusage.ru_majflt = strtol(f[F_MAJFLT], NULL,10);
		(*step)->rusage.ru_nswap = strtol(f[F_NSWAP], NULL,10);
		(*step)->rusage.ru_inblock = strtol(f[F_INBLOCKS], NULL,10);
		(*step)->rusage.ru_oublock = strtol(f[F_OUBLOCKS], NULL,10);
		(*step)->rusage.ru_msgsnd = strtol(f[F_MSGSND], NULL,10);
		(*step)->rusage.ru_msgrcv = strtol(f[F_MSGRCV], NULL,10);
		(*step)->rusage.ru_nsignals = strtol(f[F_NSIGNALS], NULL,10);
		(*step)->rusage.ru_nvcsw = strtol(f[F_NVCSW], NULL,10);
		(*step)->rusage.ru_nivcsw = strtol(f[F_NIVCSW], NULL,10);
		(*step)->vsize = strtol(f[F_VSIZE], NULL,10);
		(*step)->psize = strtol(f[F_PSIZE], NULL,10);
		(*step)->stepname = xstrdup(f[F_STEPNAME]);
	break;
	case JOB_TERMINATED:
		*job = xmalloc(sizeof(job_rec_t));
		(*job)->header.jobnum = strtol(f[F_JOB], NULL, 10);
		(*job)->header.partition = xstrdup(f[F_PARTITION]);
		strncpy((*job)->header.submitted, 
			f[F_SUBMITTED], TIMESTAMP_LENGTH);
		(*job)->header.starttime = strtol(f[F_STARTTIME], NULL, 10);
		(*job)->header.uid = atoi(f[F_UIDGID]);
		if ((p=strstr(f[F_UIDGID], ".")))
			(*job)->header.gid=atoi(++p);
		(*job)->elapsed = strtol(f[F_TOT_ELAPSED], NULL, 10);
		strncpy((*job)->finished, f[F_FINISHED], TIMESTAMP_LENGTH);
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
		if (job->job_start_seen) {
			if (_same_start(job, temp)) {
				/* OK if really a duplicate */
				if (params.opt_verbose > 1 )
					fprintf(stderr,
						"Duplicate JOB_START for job"
						" %ld at line %ld -- ignoring"
						" it\n",
						job, lc);
			} else {
				fprintf(stderr,
					"Conflicting JOB_START for job %ld at"
					" line %ld -- ignoring it\n",
					job, lc);
				inputError++;
			}
			destroy_job(job);
			return;
		} /* Data out of order; we'll go ahead and populate it now */
	} else
		job = _init_job_rec(temp->header, lc);

	list_append(jobs, job);

	job->job_start_seen = 1;
	job->header.uid = temp->header.uid;
	job->header.gid = temp->header.gid;
	xfree(job->jobname);
	job->jobname = xstrdup(temp->jobname);
	job->batch = temp->batch;
	job->priority = temp->priority;
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
			if (_same_step(step, temp)) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Duplicate JOB_STEP record "
						"for jobstep %ld.%ld at line "
						"%ld -- ignoring it\n",
						step->header.jobnum, 
						step->stepnum, lc);
			} else {
				fprintf(stderr,
					"Conflicting JOB_STEP record for"
					" jobstep %ld.%ld at line %ld"
					" -- ignoring it\n",
					step->header.jobnum, 
					step->stepnum, lc);
				inputError++;
			}
			destroy_step(temp);
			return;
		}
		strncpy(step->finished, temp->finished, TIMESTAMP_LENGTH);
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
	
got_step:
	destroy_step(temp);
	
	if (job->job_terminated_seen == 0) {	/* If the job is still running,
						   this is the most recent
						   status */
		strncpy(job->finished, step->finished, TIMESTAMP_LENGTH);
		job->status = JOB_RUNNING;
		if ( job->error == 0 )
			job->error = step->error;
		job->elapsed = time(NULL) - job->header.starttime;
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
	if(job->rusage.ru_maxrss < step->rusage.ru_maxrss)
		job->rusage.ru_maxrss = step->rusage.ru_maxrss;
	if(job->rusage.ru_ixrss < step->rusage.ru_ixrss)
		job->rusage.ru_ixrss = step->rusage.ru_ixrss;
	if(job->rusage.ru_idrss < step->rusage.ru_idrss)
		job->rusage.ru_idrss = step->rusage.ru_idrss;
	if(job->rusage.ru_isrss < step->rusage.ru_isrss)
		job->rusage.ru_isrss = step->rusage.ru_isrss;
	if(job->rusage.ru_minflt < step->rusage.ru_minflt)
		job->rusage.ru_minflt = step->rusage.ru_minflt;
	if(job->rusage.ru_majflt < step->rusage.ru_majflt)
		job->rusage.ru_majflt = step->rusage.ru_majflt;
	if(job->rusage.ru_nswap < step->rusage.ru_nswap)
		job->rusage.ru_nswap = step->rusage.ru_nswap;
	if (job->psize < step->psize)
		job->psize = step->psize;
	if (job->vsize < step->vsize)
		job->vsize = step->vsize;

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
			return;
		}
		if (_same_terminated(job, temp)) {
			if (params.opt_verbose > 1 )
				fprintf(stderr,
					"Duplicate JOB_TERMINATED record (%s) "
					"for job %ld at  line %ld -- "
					"ignoring it\n",
					decode_status_int(temp->status),
					job, lc);
		} else {
			fprintf(stderr,
				"Conflicting JOB_TERMINATED record (%s) for "
				"job %ld at line %ld -- ignoring it\n",
				decode_status_int(temp->status), job, lc);
			inputError++;
		}
		return;
	}
	job->job_terminated_seen = 1;
	job->elapsed = temp->elapsed;
	strncpy(job->finished, temp->finished, TIMESTAMP_LENGTH);
	job->status = temp->status;
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
