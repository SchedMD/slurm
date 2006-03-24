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

int  sameJobStart(long j, char *f[]);
int  sameJobStep(long js, char *f[]);
int  sameJobTerminated(long j, char *f[]);
void linkJobstep(long j, long js);
int _find_job_record(long job, char *submitted);
int _find_step_record(long j, long jobstep);
long _init_job_struct(long job, char *f[]);

/* Have we seen this data already?  */

int sameJobStart(long j, char *f[])
{
	if (jobs[j].uid != atoi(f[F_UID]))
		return 0;
	if (jobs[j].gid != atoi(f[F_GID]))
		return 0;
	if (strncmp(jobs[j].jobname, f[F_JOBNAME], MAX_JOBNAME_LENGTH))
		return 0;
	if (jobs[j].batch != atoi(f[F_BATCH]))
		return 0;
	if (jobs[j].priority != atoi(f[F_PRIORITY]))
		return 0;
	if (jobs[j].ncpus != strtol(f[F_NCPUS], NULL, 10))
		return 0;
	if (strncmp(jobs[j].nodes, f[F_NODES], strlen(jobs[j].nodes)))
		return 0;
	return 1;
}

int sameJobStep(long js, char *f[])
{ 
	if (strcmp(jobsteps[js].finished, f[F_FINISHED]))
		return 0;
	if (strncmp(jobsteps[js].cstatus, f[F_CSTATUS],
		    strlen(jobsteps[js].cstatus)))
		return 0;
	if (jobsteps[js].error != strtol(f[F_ERROR], NULL, 10))
		return 0;
	if (jobsteps[js].nprocs != strtol(f[F_NPROCS], NULL, 10))
		return 0;
	if (jobsteps[js].ncpus != strtol(f[F_NCPUS], NULL, 10))
		return 0;
	if (jobsteps[js].elapsed != strtol(f[F_ELAPSED], NULL, 10))
		return 0;
	if (jobsteps[js].tot_cpu_sec != strtol(f[F_CPU_SEC], NULL, 10))
		return 0;
	if (jobsteps[js].tot_cpu_usec != strtol(f[F_CPU_USEC], NULL, 10))
		return 0;
	if (jobsteps[js].tot_user_sec != strtol(f[F_USER_SEC], NULL, 10))
		return 0;
	if (jobsteps[js].tot_user_usec != strtol(f[F_USER_USEC], NULL, 10))
		return 0;
	if (jobsteps[js].tot_sys_sec != strtol(f[F_SYS_SEC], NULL, 10))
		return 0;
	if (jobsteps[js].tot_sys_usec != strtol(f[F_SYS_USEC], NULL, 10))
		return 0;
	if (jobsteps[js].rss != strtol(f[F_RSS], NULL,10))
		return 0;
	if (jobsteps[js].ixrss != strtol(f[F_IXRSS], NULL,10))
		return 0;
	if (jobsteps[js].idrss != strtol(f[F_IDRSS], NULL,10))
		return 0;
	if (jobsteps[js].isrss != strtol(f[F_ISRSS], NULL,10))
		return 0;
	if (jobsteps[js].minflt != strtol(f[F_MINFLT], NULL,10))
		return 0;
	if (jobsteps[js].majflt != strtol(f[F_MAJFLT], NULL,10))
		return 0;
	if (jobsteps[js].nswap != strtol(f[F_NSWAP], NULL,10))
		return 0;
	if (jobsteps[js].inblocks != strtol(f[F_INBLOCKS], NULL,10))
		return 0;
	if (jobsteps[js].oublocks != strtol(f[F_OUBLOCKS], NULL,10))
		return 0;
	if (jobsteps[js].msgsnd != strtol(f[F_MSGSND], NULL,10))
		return 0;
	if (jobsteps[js].msgrcv != strtol(f[F_MSGRCV], NULL,10))
		return 0;
	if (jobsteps[js].nsignals != strtol(f[F_NSIGNALS], NULL,10))
		return 0;
	if (jobsteps[js].nvcsw != strtol(f[F_NVCSW], NULL,10))
		return 0;
	if (jobsteps[js].nivcsw != strtol(f[F_NIVCSW], NULL,10))
		return 0;
	if (jobsteps[js].vsize != strtol(f[F_VSIZE], NULL,10))
		return 0;
	if (jobsteps[js].psize != strtol(f[F_PSIZE], NULL,10))
		return 0;
	return 1;		/* they are the same */ 
}

int  sameJobTerminated(long j, char *f[])
{
	if (jobs[j].job_terminated_seen != 1)
		return 0;
	if (jobs[j].elapsed != strtol(f[F_TOT_ELAPSED], NULL, 10))
		return 0;
	if (strcmp(jobs[j].finished, f[F_FINISHED]))
		return 0;
	if (strncmp(jobs[j].cstatus, f[F_CSTATUS], strlen(jobs[j].cstatus)))
		return 0;
	return 1;
}

void linkJobstep(long j, long js)
{
	long	*current;
	current = &jobs[j].first_jobstep;
	while ( (*current>=0) &&
		(jobsteps[*current].jobstep < jobsteps[js].jobstep)) {
		current = &jobsteps[*current].next;
	}
	jobsteps[js].next = *current;
	*current = js; 
}

int _find_job_record(long job, char *submitted)
{
	int	cmp,
		i;

	for (i=0; i<Njobs; i++) {
		if (jobs[i].job == job) {
			cmp = strncmp(jobs[i].submitted, submitted,
				      TIMESTAMP_LENGTH);
			if ( cmp == 0)
				return i;
			else if (cmp < 0)
				/* If we're looking for a later
				 * record with this job number, we
				 * know that this one is an older,
				 * duplicate record.
				 *   We assume that the newer record
				 * will be created if it doesn't
				 * already exist. */
				jobs[i].jobnum_superseded = 1;
		}
	}
	return -1;
}

int _find_step_record(long j, long jobstep)
{
	int	i;
	if ((i=jobs[j].first_jobstep)<0)
		return -1;
	for (; i>=0; i=jobsteps[i].next) {
		if (jobsteps[i].jobstep == jobstep)
			return i;
	}
	return -1;
}

long _init_job_struct(long job, char *f[])
{
	long	j;
	char	*p;

	if ((j=Njobs++)>= MAX_JOBS) {
		fprintf(stderr, "Too many jobs listed in the log file;\n"
			"stopped after %ld jobs at input line %ld.\n"
			"Please use \"--expire\" to reduce the "
			"size of the log.\n",
			Njobs, -1);
		exit(2);
	} 
	jobs[j].job = job;
	jobs[j].job_start_seen = 0;
	jobs[j].job_step_seen = 0;
	jobs[j].job_terminated_seen = 0;
	jobs[j].jobnum_superseded = 0;
	jobs[j].first_jobstep = -1;
	jobs[j].partition = xmalloc(strlen(f[F_PARTITION])+1);
	strcpy(jobs[j].partition, f[F_PARTITION]);
	strncpy(jobs[j].submitted, f[F_SUBMITTED], 16);
	strncpy(jobs[j].jobname, "(unknown)", MAX_JOBNAME_LENGTH);
	strcpy(jobs[j].cstatus,"r");
	strcpy(jobs[j].finished,"?");
	jobs[j].starttime = strtol(f[F_STARTTIME], NULL, 10);
	/* Early versions of jobacct treated F_UIDGID as a reserved
	 * field, so we might find "-" here. Take advantage of the
	 * fact that atoi() will return 0 if it finds something that's
	 * not a number for uid and gid.
	 */
	jobs[j].uid = atoi(f[F_UIDGID]);
	if ((p=strstr(f[F_UIDGID],".")))
		jobs[j].gid=atoi(++p);
	jobs[j].tot_cpu_sec = 0;
	jobs[j].tot_cpu_usec = 0;
	jobs[j].tot_user_sec = 0;
	jobs[j].tot_user_usec = 0;
	jobs[j].tot_sys_sec = 0;
	jobs[j].tot_sys_usec = 0;
	jobs[j].rss = 0;
	jobs[j].ixrss = 0;
	jobs[j].idrss = 0;
	jobs[j].isrss = 0;
	jobs[j].minflt = 0;
	jobs[j].majflt = 0;
	jobs[j].nswap = 0;
	jobs[j].inblocks = 0;
	jobs[j].oublocks = 0;
	jobs[j].msgsnd = 0;
	jobs[j].msgrcv = 0;
	jobs[j].nsignals = 0;
	jobs[j].nvcsw = 0;
	jobs[j].nivcsw = 0;
	jobs[j].vsize = 0;
	jobs[j].psize = 0;
	jobs[j].error = 0;
	return j;
}

void processJobStart(char *f[])
{
	int	i;
	long	j;	/* index in jobs[] */
	long	job;

	job = strtol(f[F_JOB], NULL, 10);
	j = _find_job_record(job, f[F_SUBMITTED]);
	if (j >= 0 ) {	/* Hmmm... that's odd */
		if (jobs[j].job_start_seen) {
			if (sameJobStart(j, f)) {/* OK if really a duplicate */
				if (params.opt_verbose > 1 )
					fprintf(stderr,
						"Duplicate JOB_START for job"
						" %ld at line %ld -- ignoring"
						" it\n",
						job, -1);
			} else {
				fprintf(stderr,
					"Conflicting JOB_START for job %ld at"
					" line %ld -- ignoring it\n",
					job, -1);
				inputError++;
			}
			return;
		} /* Data out of order; we'll go ahead and populate it now */
	} else
		j = _init_job_struct(job,f);
	jobs[j].job_start_seen = 1;
	jobs[j].uid = atoi(f[F_UID]);
	jobs[j].gid = atoi(f[F_GID]);
	strncpy(jobs[j].jobname, f[F_JOBNAME], MAX_JOBNAME_LENGTH);
	jobs[j].batch = atoi(f[F_BATCH]);
	jobs[j].priority = atoi(f[F_PRIORITY]);
	jobs[j].ncpus = strtol(f[F_NCPUS], NULL, 10);
	jobs[j].nodes = xmalloc(strlen(f[F_NODES])+1);
	strcpy(jobs[j].nodes, f[F_NODES]); 
	for (i=0; jobs[j].nodes[i]; i++)	/* discard trailing <CR> */
		if (isspace(jobs[j].nodes[i]))
			jobs[j].nodes[i] = 0;
	if (strcmp(jobs[j].nodes, "(null)")==0) {
		free(jobs[j].nodes);
		jobs[j].nodes = "unknown";
	}
}

void processJobStep(char *f[])
{
	long	j,	/* index into jobs */
		js,	/* index into jobsteps */
		job,
		jobstep;

	job = strtol(f[F_JOB], NULL, 10);
	if (strcmp(f[F_JOBSTEP],NOT_JOBSTEP)==0)
		jobstep = -2;
	else
		jobstep = strtol(f[F_JOBSTEP], NULL, 10);
	j = _find_job_record(job, f[F_SUBMITTED]);
	if (j<0) {	/* fake it for now */
		j = _init_job_struct(job,f);
		if ((params.opt_verbose > 1) 
		    && (params.opt_jobstep_list==NULL)) 
			fprintf(stderr, 
				"Note: JOB_STEP record %ld.%ld preceded "
				"JOB_START record at line %ld\n",
				job, jobstep, -1);
	}
	if ((js=_find_step_record(j, jobstep))>=0) {
		if (strcasecmp(f[F_CSTATUS], "R")==0)
			return;/* if "R" record preceded by F or CD; unusual */
		if (strcasecmp(jobsteps[js].cstatus, "R")) { /* if not "R" */
			if (sameJobStep(js, f)) {
				if (params.opt_verbose > 1)
					fprintf(stderr,
						"Duplicate JOB_STEP record "
						"for jobstep %ld.%ld at line "
						"%ld -- ignoring it\n",
						job, jobstep, -1);
			} else {
				fprintf(stderr,
					"Conflicting JOB_STEP record for"
					" jobstep %ld.%ld at line %ld"
					" -- ignoring it\n",
					job, jobstep, -1);
				inputError++;
			}
			return;
		}
		goto replace_js;
	}
	if ((js = Njobsteps++)>=MAX_JOBSTEPS) {
		fprintf(stderr, "Too many jobsteps listed in the log file;\n"
			"stopped after %ld jobs and %ld job steps\n"
			"at input line %ld. Please use \"--expire\"\n"
			"to reduce the size of the log.\n",
			Njobs, js, -1);
		exit(2);
	}
	jobsteps[js].j = j;
	jobsteps[js].jobstep = jobstep;
	if (jobstep >= 0) {
		linkJobstep(j, js);
		jobs[j].job_step_seen = 1;
	}
replace_js:
	strcpy(jobsteps[js].finished, f[F_FINISHED]);
	strcpy(jobsteps[js].cstatus, f[F_CSTATUS]);
	jobsteps[js].error = strtol(f[F_ERROR], NULL, 10);
	jobsteps[js].nprocs = strtol(f[F_NPROCS], NULL, 10);
	jobsteps[js].ncpus = strtol(f[F_NCPUS], NULL, 10);
	jobsteps[js].elapsed = strtol(f[F_ELAPSED], NULL, 10);
	jobsteps[js].tot_cpu_sec = strtol(f[F_CPU_SEC], NULL, 10);
	jobsteps[js].tot_cpu_usec = strtol(f[F_CPU_USEC], NULL, 10);
	jobsteps[js].tot_user_sec = strtol(f[F_USER_SEC], NULL, 10);
	jobsteps[js].tot_user_usec = strtol(f[F_USER_USEC], NULL, 10);
	jobsteps[js].tot_sys_sec = strtol(f[F_SYS_SEC], NULL, 10);
	jobsteps[js].tot_sys_usec = strtol(f[F_SYS_USEC], NULL, 10);
	jobsteps[js].rss = strtol(f[F_RSS], NULL,10);
	jobsteps[js].ixrss = strtol(f[F_IXRSS], NULL,10);
	jobsteps[js].idrss = strtol(f[F_IDRSS], NULL,10);
	jobsteps[js].isrss = strtol(f[F_ISRSS], NULL,10);
	jobsteps[js].minflt = strtol(f[F_MINFLT], NULL,10);
	jobsteps[js].majflt = strtol(f[F_MAJFLT], NULL,10);
	jobsteps[js].nswap = strtol(f[F_NSWAP], NULL,10);
	jobsteps[js].inblocks = strtol(f[F_INBLOCKS], NULL,10);
	jobsteps[js].oublocks = strtol(f[F_OUBLOCKS], NULL,10);
	jobsteps[js].msgsnd = strtol(f[F_MSGSND], NULL,10);
	jobsteps[js].msgrcv = strtol(f[F_MSGRCV], NULL,10);
	jobsteps[js].nsignals = strtol(f[F_NSIGNALS], NULL,10);
	jobsteps[js].nvcsw = strtol(f[F_NVCSW], NULL,10);
	jobsteps[js].nivcsw = strtol(f[F_NIVCSW], NULL,10);
	jobsteps[js].vsize = strtol(f[F_VSIZE], NULL,10);
	jobsteps[js].psize = strtol(f[F_PSIZE], NULL,10);

	if (jobs[j].job_terminated_seen == 0) {	/* If the job is still running,
						   this is the most recent
						   status */
		strcpy(jobs[j].finished, f[F_FINISHED]);
		jobs[j].cstatus[0] = 'r'; jobs[j].cstatus[1] = 0;
		if ( jobs[j].error == 0 )
			jobs[j].error = jobsteps[js].error;
		jobs[j].elapsed = time(NULL) - jobs[j].starttime;
	}
	/* now aggregate the aggregatable */
	jobs[j].tot_cpu_sec += jobsteps[js].tot_cpu_sec;
	jobs[j].tot_cpu_usec += jobsteps[js].tot_cpu_usec;
	jobs[j].tot_user_sec += jobsteps[js].tot_user_sec;
	jobs[j].tot_user_usec += jobsteps[js].tot_user_usec;
	jobs[j].tot_sys_sec += jobsteps[js].tot_sys_sec;
	jobs[j].tot_sys_usec += jobsteps[js].tot_sys_usec;
	jobs[j].inblocks += jobsteps[js].inblocks;
	jobs[j].oublocks += jobsteps[js].oublocks;
	jobs[j].msgsnd += jobsteps[js].msgsnd;
	jobs[j].msgrcv += jobsteps[js].msgrcv;
	jobs[j].nsignals += jobsteps[js].nsignals;
	jobs[j].nvcsw += jobsteps[js].nvcsw;
	jobs[j].nivcsw += jobsteps[js].nivcsw;
	/* and finally the maximums for any process */
	if (jobs[j].rss < jobsteps[js].rss)
		jobs[j].rss = jobsteps[js].rss;
	if (jobs[j].ixrss < jobsteps[js].ixrss)
		jobs[j].ixrss = jobsteps[js].ixrss;
	if (jobs[j].idrss < jobsteps[js].idrss)
		jobs[j].idrss = jobsteps[js].idrss;
	if (jobs[j].isrss < jobsteps[js].isrss)
		jobs[j].isrss = jobsteps[js].isrss;
	if (jobs[j].majflt < jobsteps[js].majflt)
		jobs[j].majflt = jobsteps[js].majflt;
	if (jobs[j].minflt < jobsteps[js].minflt)
		jobs[j].minflt = jobsteps[js].minflt;
	if (jobs[j].nswap < jobsteps[js].nswap)
		jobs[j].nswap = jobsteps[js].nswap;
	if (jobs[j].psize < jobsteps[js].psize)
		jobs[j].psize = jobsteps[js].psize;
	if (jobs[j].vsize < jobsteps[js].vsize)
		jobs[j].vsize = jobsteps[js].vsize;
}

void processJobTerminated(char *f[])
{
	long	i,
		j,
		job;

	job = strtol(f[F_JOB], NULL, 10);
	j = _find_job_record(job, f[F_SUBMITTED]);
	if (j<0) {	/* fake it for now */
		j = _init_job_struct(job,f);
		if (params.opt_verbose > 1) 
			fprintf(stderr, "Note: JOB_TERMINATED record for job "
				"%ld preceded "
				"other job records at line %ld\n",
				job, -1);
	} else if (jobs[j].job_terminated_seen) {
		if (strcasecmp(f[F_CSTATUS],"nf")==0) {
			/* multiple node failures - extra TERMINATED records */
			if (params.opt_verbose > 1)
				fprintf(stderr, 
					"Note: Duplicate JOB_TERMINATED "
					"record (nf) for job %ld at "
					"line %ld\n", 
					job, -1);
			/* JOB_TERMINATED/NF records may be preceded
			 * by a JOB_TERMINATED/CA record; NF is much
			 * more interesting.
			 */
			strncpy(jobs[j].cstatus, "nf", 3);
			return;
		}
		if (sameJobTerminated(j, f)) {
			if (params.opt_verbose > 1 )
				fprintf(stderr,
					"Duplicate JOB_TERMINATED record (%s) "
					"for job %ld at  line %ld -- "
					"ignoring it\n",
					f[F_CSTATUS], job, -1);
		} else {
			fprintf(stderr,
				"Conflicting JOB_TERMINATED record (%s) for "
				"job %ld at line %ld -- ignoring it\n",
				f[F_CSTATUS], job, -1);
			inputError++;
		}
		return;
	}
	jobs[j].job_terminated_seen = 1;
	jobs[j].elapsed = strtol(f[F_TOT_ELAPSED], NULL, 10);
	strcpy(jobs[j].finished, f[F_FINISHED]);
	strncpy(jobs[j].cstatus, f[F_CSTATUS], 3);
	for (i=0; jobs[j].cstatus[i]; i++)
		if (isspace(jobs[j].cstatus[i]))
			jobs[j].cstatus[i] = 0;
}
