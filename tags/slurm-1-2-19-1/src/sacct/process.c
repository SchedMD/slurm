/*****************************************************************************\
 *  process.c - process functions for sacct
 *
 *  $Id: process.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  UCRL-CODE-226842.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "sacct.h"

job_rec_t *_find_job_record(acct_header_t header, int type);
int _remove_job_record(uint32_t jobnum);
step_rec_t *_find_step_record(job_rec_t *job, long jobstep);
job_rec_t *_init_job_rec(acct_header_t header);
step_rec_t *_init_step_rec(acct_header_t header);
int _parse_line(char *f[], void **data, int len);

job_rec_t *_find_job_record(acct_header_t header, int type)
{
	job_rec_t *job = NULL;
	ListIterator itr = list_iterator_create(jobs);

	while((job = (job_rec_t *)list_next(itr)) != NULL) {
		if (job->header.jobnum == header.jobnum) {
			if(job->header.job_submit == 0 && type == JOB_START) {
				list_remove(itr);
				destroy_job(job);
				job = NULL;
				break;
			}
		
			if(job->header.job_submit == BATCH_JOB_TIMESTAMP) {
				job->header.job_submit = header.job_submit;
				break;
			}
			
			if(job->header.job_submit == header.job_submit)
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

int _remove_job_record(uint32_t jobnum)
{
	job_rec_t *job = NULL;
	int rc = SLURM_ERROR;
	ListIterator itr = list_iterator_create(jobs);

	while((job = (job_rec_t *)list_next(itr)) != NULL) {
		if (job->header.jobnum == jobnum) {
			list_remove(itr);
			destroy_job(job);
			rc = SLURM_SUCCESS;
		}
	}
	list_iterator_destroy(itr);
	return rc;
}

step_rec_t *_find_step_record(job_rec_t *job, long stepnum)
{
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

job_rec_t *_init_job_rec(acct_header_t header)
{
	job_rec_t *job = xmalloc(sizeof(job_rec_t));
	memcpy(&job->header, &header, sizeof(acct_header_t));
	memset(&job->rusage, 0, sizeof(struct rusage));
	memset(&job->sacct, 0, sizeof(sacct_t));
	job->sacct.min_cpu = (float)NO_VAL;
	job->job_start_seen = 0;
	job->job_step_seen = 0;
	job->job_terminated_seen = 0;
	job->jobnum_superseded = 0;
	job->jobname = xstrdup("(unknown)");
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
	job->steps = list_create(destroy_step);
	job->nodes = NULL;
	job->track_steps = 0;
	job->account = NULL;
	job->requid = -1;

      	return job;
}

step_rec_t *_init_step_rec(acct_header_t header)
{
	step_rec_t *step = xmalloc(sizeof(job_rec_t));
	memcpy(&step->header, &header, sizeof(acct_header_t));
	memset(&step->rusage, 0, sizeof(struct rusage));
	memset(&step->sacct, 0, sizeof(sacct_t));
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

int _parse_header(char *f[], acct_header_t *header)
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

int _parse_line(char *f[], void **data, int len)
{
	int i = atoi(f[F_RECTYPE]);
	job_rec_t **job = (job_rec_t **)data;
	step_rec_t **step = (step_rec_t **)data;
	acct_header_t header;
	_parse_header(f, &header);
		
	switch(i) {
	case JOB_START:
		*job = _init_job_rec(header);
		(*job)->jobname = xstrdup(f[F_JOBNAME]);
		(*job)->track_steps = atoi(f[F_TRACK_STEPS]);
		(*job)->priority = atoi(f[F_PRIORITY]);
		(*job)->ncpus = atoi(f[F_NCPUS]);
		(*job)->nodes = xstrdup(f[F_NODES]);
		for (i=0; (*job)->nodes[i]; i++) { /* discard trailing <CR> */
			if (isspace((*job)->nodes[i]))
				(*job)->nodes[i] = '\0';
		}
		if (!strcmp((*job)->nodes, "(null)")) {
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
		*step = _init_step_rec(header);
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
		(*step)->sacct.max_vsize = atoi(f[F_MAX_VSIZE]) * 1024;
		if(len > F_STEPNODES) {
			(*step)->sacct.max_vsize_id.taskid = 
				atoi(f[F_MAX_VSIZE_TASK]);
			(*step)->sacct.ave_vsize = atof(f[F_AVE_VSIZE]) * 1024;
			(*step)->sacct.max_rss = atoi(f[F_MAX_RSS]) * 1024;
			(*step)->sacct.max_rss_id.taskid = 
				atoi(f[F_MAX_RSS_TASK]);
			(*step)->sacct.ave_rss = atof(f[F_AVE_RSS]) * 1024;
			(*step)->sacct.max_pages = atoi(f[F_MAX_PAGES]);
			(*step)->sacct.max_pages_id.taskid = 
				atoi(f[F_MAX_PAGES_TASK]);
			(*step)->sacct.ave_pages = atof(f[F_AVE_PAGES]);
			(*step)->sacct.min_cpu = atof(f[F_MIN_CPU]);
			(*step)->sacct.min_cpu_id.taskid = 
				atoi(f[F_MIN_CPU_TASK]);
			(*step)->sacct.ave_cpu = atof(f[F_AVE_CPU]);
			(*step)->stepname = xstrdup(f[F_STEPNAME]);
			(*step)->nodes = xstrdup(f[F_STEPNODES]);
		} else {
			(*step)->sacct.max_vsize_id.taskid = (uint16_t)NO_VAL;
			(*step)->sacct.ave_vsize = (float)NO_VAL;
			(*step)->sacct.max_rss = (uint32_t)NO_VAL;
			(*step)->sacct.max_rss_id.taskid = (uint16_t)NO_VAL;
			(*step)->sacct.ave_rss = (float)NO_VAL;
			(*step)->sacct.max_pages = (uint32_t)NO_VAL;
			(*step)->sacct.max_pages_id.taskid = (uint16_t)NO_VAL;
			(*step)->sacct.ave_pages = (float)NO_VAL;
			(*step)->sacct.min_cpu = (uint32_t)NO_VAL;
			(*step)->sacct.min_cpu_id.taskid = (uint16_t)NO_VAL;
			(*step)->sacct.ave_cpu =  (float)NO_VAL;
			(*step)->stepname = NULL;
			(*step)->nodes = NULL;
		}
		if(len > F_MIN_CPU_NODE) {
			(*step)->sacct.max_vsize_id.nodeid = 
				atoi(f[F_MAX_VSIZE_NODE]);
			(*step)->sacct.max_rss_id.nodeid = 
				atoi(f[F_MAX_RSS_NODE]);
			(*step)->sacct.max_pages_id.nodeid = 
				atoi(f[F_MAX_PAGES_NODE]);
			(*step)->sacct.min_cpu_id.nodeid = 
				atoi(f[F_MIN_CPU_NODE]);
		} else {
			(*step)->sacct.max_vsize_id.nodeid = 
				(uint32_t)NO_VAL;
			(*step)->sacct.max_rss_id.nodeid = 
				(uint32_t)NO_VAL;
			(*step)->sacct.max_pages_id.nodeid = 
				(uint32_t)NO_VAL;
			(*step)->sacct.min_cpu_id.nodeid = 
				(uint32_t)NO_VAL;
		}
		if(len > F_STEP_ACCOUNT)
			(*step)->account = xstrdup(f[F_STEP_ACCOUNT]);
		if(len > F_STEP_REQUID)
			(*step)->requid = atoi(f[F_STEP_REQUID]);
		break;
	case JOB_SUSPEND:
	case JOB_TERMINATED:
		*job = _init_job_rec(header);
		(*job)->elapsed = atoi(f[F_TOT_ELAPSED]);
		(*job)->status = atoi(f[F_STATUS]);		
		if(len > F_JOB_REQUID) 
			(*job)->requid = atoi(f[F_JOB_REQUID]);
		break;
	default:
		printf("UNKOWN TYPE %d",i);
		break;
	}
	return SLURM_SUCCESS;
}

void process_start(char *f[], int lc, int show_full, int len)
{
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);
	job = _find_job_record(temp->header, JOB_START);
	if (job) {	/* Hmmm... that's odd */
		printf("job->header.job_submit = %d", (int)job->header.job_submit);
		if(job->header.job_submit == 0)
			_remove_job_record(job->header.jobnum);
		else {
			fprintf(stderr,
				"Conflicting JOB_START for job %u at"
				" line %d -- ignoring it\n",
				job->header.jobnum, lc);
			input_error++;
			destroy_job(temp);
			return;
		}
	}
	
	job = temp;
	job->show_full = show_full;
	list_append(jobs, job);
	job->job_start_seen = 1;
	
}

void process_step(char *f[], int lc, int show_full, int len)
{
	job_rec_t *job = NULL;
	
	step_rec_t *step = NULL;
	step_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);

	job = _find_job_record(temp->header, JOB_STEP);
	
	if (temp->stepnum == -2) {
		destroy_step(temp);
		return;
	}
	if (!job) {	/* fake it for now */
		job = _init_job_rec(temp->header);
		if (params.opt_verbose > 1) 
			fprintf(stderr, 
				"Note: JOB_STEP record %u.%u preceded "
				"JOB_START record at line %d\n",
				temp->header.jobnum, temp->stepnum, lc);
	}
	job->show_full = show_full;
	
	if ((step = _find_step_record(job, temp->stepnum))) {
		
		if (temp->status == JOB_RUNNING) {
			destroy_step(temp);
			return;/* if "R" record preceded by F or CD; unusual */
		}
		if (step->status != JOB_RUNNING) { /* if not JOB_RUNNING */
			fprintf(stderr,
				"Conflicting JOB_STEP record for "
				"jobstep %u.%u at line %d "
				"-- ignoring it\n",
				step->header.jobnum, 
				step->stepnum, lc);
			input_error++;
			
			destroy_step(temp);
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
		memcpy(&step->sacct, &temp->sacct, sizeof(sacct_t));
		xfree(step->stepname);
		step->stepname = xstrdup(temp->stepname);
		step->end = temp->header.timestamp;
		destroy_step(temp);
		goto got_step;
	}
	step = temp;
	temp = NULL;
	list_append(job->steps, step);
	if(job->header.timestamp == 0)
		job->header.timestamp = step->header.timestamp;
	job->job_step_seen = 1;
	job->ntasks += step->ntasks;
	if(!job->nodes || !strcmp(job->nodes, "(unknown)")) {
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
	/* now aggregate the aggregatable */
	job->ncpus = MAX(job->ncpus, step->ncpus);
	if(step->status < JOB_COMPLETE)
		return;
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

	/* get the max for all the sacct_t struct */
	aggregate_sacct(&job->sacct, &step->sacct);
}

void process_suspend(char *f[], int lc, int show_full, int len)
{
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);
	job = _find_job_record(temp->header, JOB_SUSPEND);
	if (!job)    
		job = _init_job_rec(temp->header);
	
	job->show_full = show_full;
	if (job->status == JOB_SUSPENDED) 
		job->elapsed -= temp->elapsed;

	//job->header.timestamp = temp->header.timestamp;
	job->status = temp->status;
	destroy_job(temp);
}
	
void process_terminated(char *f[], int lc, int show_full, int len)
{
	job_rec_t *job = NULL;
	job_rec_t *temp = NULL;

	_parse_line(f, (void **)&temp, len);
	job = _find_job_record(temp->header, JOB_TERMINATED);
	if (!job) {	/* fake it for now */
		job = _init_job_rec(temp->header);
		if (params.opt_verbose > 1) 
			fprintf(stderr, "Note: JOB_TERMINATED record for job "
				"%u preceded "
				"other job records at line %d\n",
				temp->header.jobnum, lc);
	} else if (job->job_terminated_seen) {
		if (temp->status == JOB_NODE_FAIL) {
			/* multiple node failures - extra TERMINATED records */
			if (params.opt_verbose > 1)
				fprintf(stderr, 
					"Note: Duplicate JOB_TERMINATED "
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
			decode_status_int(temp->status), 
			job->header.jobnum, lc);
		input_error++;
		goto finished;
	}
	job->job_terminated_seen = 1;
	job->elapsed = temp->elapsed;
	job->end = temp->header.timestamp;
	job->status = temp->status;
	job->requid = temp->requid;
	if(list_count(job->steps) > 1)
		job->track_steps = 1;
	job->show_full = show_full;
	
finished:
	destroy_job(temp);
}

void find_hostname(uint32_t pos, char *hosts, char *host)
{
	hostlist_t hostlist = NULL;
	char *temp = NULL;

	if(pos == (uint32_t)NO_VAL) {
		snprintf(host, 50, "'N/A'");
		return;
	}
	hostlist = hostlist_create(hosts);
	temp = hostlist_nth(hostlist, pos);
	if(temp) {
		snprintf(host, 50, "%s", temp);
		free(temp);
	} else {
		snprintf(host, 50, "'N/A'");
	}
	return;
}

void aggregate_sacct(sacct_t *dest, sacct_t *from)
{
	if(dest->max_vsize < from->max_vsize) {
		dest->max_vsize = from->max_vsize;
		dest->max_vsize_id = from->max_vsize_id;
	}
	dest->ave_vsize += from->ave_vsize;
	
	if(dest->max_rss < from->max_rss) {
		dest->max_rss = from->max_rss;
		dest->max_rss_id = from->max_rss_id;
	}
	dest->ave_rss += from->ave_rss;
	
	if(dest->max_pages < from->max_pages) {
		dest->max_pages = from->max_pages;
		dest->max_pages_id = from->max_pages_id;
	}
	dest->ave_pages += from->ave_pages;
	
	if((dest->min_cpu > from->min_cpu) 
	   || (dest->min_cpu == (float)NO_VAL)) {
		dest->min_cpu = from->min_cpu;
		dest->min_cpu_id = from->min_cpu_id;
	}
	dest->ave_cpu += from->ave_cpu;
}

void destroy_acct_header(void *object)
{
	acct_header_t *header = (acct_header_t *)object;
	if(header) {
		xfree(header->partition);
		xfree(header->blockid);
	}
}
void destroy_job(void *object)
{
	job_rec_t *job = (job_rec_t *)object;
	if (job) {
		if(job->steps)
			list_destroy(job->steps);
		destroy_acct_header(&job->header);
		xfree(job->jobname);
		xfree(job->nodes);
		xfree(job);
	}
}

void destroy_step(void *object)
{
	step_rec_t *step = (step_rec_t *)object;
	if (step) {
		destroy_acct_header(&step->header);
		xfree(step->stepname);
		xfree(step->nodes);
		xfree(step);
	}
}
