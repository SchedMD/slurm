/*****************************************************************************\
 *  jobacct.c - process and record information about process accountablity
 *
 *  $Id: jobacct.c 7620 2006-03-29 17:42:21Z da $
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "jobacct.h"

#define BUFFER_SIZE 4096
#define TIMESTAMP_LENGTH 15

static FILE *		LOGFILE;
static int		LOGFILE_FD;
static pthread_mutex_t  logfile_lock = PTHREAD_MUTEX_INITIALIZER;
static char *		log_file = NULL;
/* Format of the JOB_STEP record */
const char *_jobstep_format = 
"%d "
"%u "	/* stepid */
"%d "	/* completion status */
"%d "	/* completion code */
"%u "	/* nprocs */
"%u "	/* number of cpus */
"%u "	/* elapsed seconds */
"%u "	/* total cputime seconds */
"%u "	/* total cputime microseconds */
"%u "	/* user seconds */
"%u "	/* user microseconds */
"%u "	/* system seconds */
"%u "	/* system microseconds */
"%u "	/* max rss */
"%u "	/* max ixrss */
"%u "	/* max idrss */
"%u "	/* max isrss */
"%u "	/* max minflt */
"%u "	/* max majflt */
"%u "	/* max nswap */
"%u "	/* total inblock */
"%u "	/* total outblock */
"%u "	/* total msgsnd */
"%u "	/* total msgrcv */
"%u "	/* total nsignals */
"%u "	/* total nvcsw */
"%u "	/* total nivcsw */
"%u "	/* max vsize */
"%u "	/* max psize */
"%s";	/* step process name */


/*
 * Print the record to the log file.
 */

static int _print_record(struct job_record *job_ptr, 
			 time_t time, char *data)
{ 
	struct tm   *ts; /* timestamp decoder */
	static int   rc=SLURM_SUCCESS;

	ts = xmalloc(sizeof(struct tm));
	gmtime_r(&time, ts);
	debug("_print_record, job=%u, \"%20s\"",
	      job_ptr->job_id, data);
	slurm_mutex_lock( &logfile_lock );
	if (fprintf(LOGFILE,
		    "%u %s %u %u %d %d - - %s\n",
		    job_ptr->job_id, job_ptr->partition,
		    job_ptr->start_time, (int)time, 
		    job_ptr->user_id, job_ptr->group_id, data)
	    < 0)
		rc=SLURM_ERROR;
	fdatasync(LOGFILE_FD);
	slurm_mutex_unlock( &logfile_lock );
	xfree(ts);
	return rc;
}

int jobacct_init(char *job_acct_log)
{
	int 		rc = SLURM_SUCCESS;
	mode_t		prot = 0600;
	struct stat	statbuf;


	debug("jobacct_init() called");
	slurm_mutex_lock( &logfile_lock );
	if (LOGFILE)
		fclose(LOGFILE);
	log_file=job_acct_log;
	if (*log_file != '/')
		fatal("JobAcctLoc must specify an absolute pathname");
	if (stat(log_file, &statbuf)==0)       /* preserve current file mode */
		prot = statbuf.st_mode;
	LOGFILE = fopen(log_file, "a");
	if (LOGFILE == NULL) {
		fatal("open %s: %m", log_file);
		rc = SLURM_ERROR;
	} else
		chmod(log_file, prot); 
	if (setvbuf(LOGFILE, NULL, _IOLBF, 0))
		fatal("setvbuf() failed");
	LOGFILE_FD = fileno(LOGFILE);
	slurm_mutex_unlock( &logfile_lock );
	//_get_slurmctld_syms();
	return rc;
}

int jobacct_job_start(struct job_record *job_ptr)
{
	int	i,
		ncpus=0,
		rc=SLURM_SUCCESS,
		tmp;
	char	buf[BUFFER_SIZE], *jname;
	long		priority;
	
	debug("jobacct_job_start() called");
	for (i=0; i < job_ptr->num_cpu_groups; i++)
		ncpus += (job_ptr->cpus_per_node[i])
			* (job_ptr->cpu_count_reps[i]);
	priority = (job_ptr->priority == NO_VAL) ?
		-1L : (long) job_ptr->priority;

	if ((tmp = strlen(job_ptr->name))) {
		jname = xmalloc(++tmp);
		for (i=0; i<tmp; i++) {
			if (isspace(job_ptr->name[i]))
				jname[i]='_';
			else
				jname[i]=job_ptr->name[i];
		}
	} else {
		jname = xstrdup("allocation");
	}
	tmp = snprintf(buf, BUFFER_SIZE,
		       "%d %s %u %ld %u %s",
		       JOB_START, jname,
		       job_ptr->batch_flag, priority, ncpus,
		       job_ptr->nodes);
	
	rc = _print_record(job_ptr, job_ptr->start_time, buf);
	
	xfree(jname);
	return rc;
}

int jobacct_step_start(struct step_record *step)
{
	char buf[BUFFER_SIZE];
	int	rc;

	snprintf(buf, BUFFER_SIZE, _jobstep_format,
		 JOB_STEP,
		 step->step_id,	/* stepid */
		 JOB_RUNNING,		/* completion status */
		 0,     		/* completion code */
		 step->num_tasks,	/* number of tasks */
		 step->job_ptr->num_procs,/* number of cpus */
		 0,	        	/* elapsed seconds */
		 0,                    /* total cputime seconds */
		 0,    		/* total cputime seconds */
		 0,	/* user seconds */
		 0,/* user microseconds */
		 0,	/* system seconds */
		 0,/* system microsecs */
		 0,	/* max rss */
		 0,	/* max ixrss */
		 0,	/* max idrss */
		 0,	/* max isrss */
		 0,	/* max minflt */
		 0,	/* max majflt */
		 0,	/* max nswap */
		 0,	/* total inblock */
		 0,	/* total outblock */
		 0,	/* total msgsnd */
		 0,	/* total msgrcv */
		 0,	/* total nsignals */
		 0,	/* total nvcsw */
		 0,	/* total nivcsw */
		 0,		/* max vsize */
		 0,		/* max psize */
		 step->name);      	/* step exe name */
	rc = _print_record(step->job_ptr, step->start_time, buf);	
	return rc;
}

int jobacct_step_complete(struct step_record *step)
{
	char buf[BUFFER_SIZE];
	time_t now;
	int rc;
	int elapsed;
	int comp_status;
	
	now = time(NULL);
	
	if ((elapsed=now-step->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */
	if (step->exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;
	
	snprintf(buf, BUFFER_SIZE, _jobstep_format,
			  JOB_STEP,
			  step->step_id,	/* stepid */
			  comp_status,		/* completion status */
			  step->exit_code,	/* completion code */
			  step->num_tasks,	/* number of tasks */
			  step->job_ptr->job_state,/* number of cpus */
			  elapsed,	        	/* elapsed seconds */
			  /* total cputime seconds */
			  step->rusage.ru_utime.tv_sec	
			  + step->rusage.ru_stime.tv_sec,
			  /* total cputime seconds */
			  step->rusage.ru_utime.tv_usec	
			  + step->rusage.ru_stime.tv_usec,
			  step->rusage.ru_utime.tv_sec,	/* user seconds */
			  step->rusage.ru_utime.tv_usec,/* user microseconds */
			  step->rusage.ru_stime.tv_sec,	/* system seconds */
			  step->rusage.ru_stime.tv_usec,/* system microsecs */
			  step->rusage.ru_maxrss,	/* max rss */
			  step->rusage.ru_ixrss,	/* max ixrss */
			  step->rusage.ru_idrss,	/* max idrss */
			  step->rusage.ru_isrss,	/* max isrss */
			  step->rusage.ru_minflt,	/* max minflt */
			  step->rusage.ru_majflt,	/* max majflt */
			  step->rusage.ru_nswap,	/* max nswap */
			  step->rusage.ru_inblock,	/* total inblock */
			  step->rusage.ru_oublock,	/* total outblock */
			  step->rusage.ru_msgsnd,	/* total msgsnd */
			  step->rusage.ru_msgrcv,	/* total msgrcv */
			  step->rusage.ru_nsignals,	/* total nsignals */
			  step->rusage.ru_nvcsw,	/* total nvcsw */
			  step->rusage.ru_nivcsw,	/* total nivcsw */
			  step->max_vsize,		/* max vsize */
			  step->max_psize,		/* max psize */
			  step->name);      	/* step exe name */
	rc = _print_record(step->job_ptr, now, buf);	
	return rc;
}

int jobacct_job_complete(struct job_record *job_ptr) 
{
	int		rc = SLURM_SUCCESS,
		tmp;
	char		buf[BUFFER_SIZE];
	
	debug("jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("jobacct: job %u never started", job_ptr->job_id);
		return rc;
	}
	
	snprintf(buf, BUFFER_SIZE, "%d %u %d",
		 JOB_TERMINATED,
		 (int) (job_ptr->end_time - job_ptr->start_time),
		 job_ptr->job_state & (~JOB_COMPLETING));
	
	rc = _print_record(job_ptr, job_ptr->end_time, buf);
	return rc;
}

int jobacct_suspend(struct job_record *job_ptr)
{
	int		i;
	char buf[BUFFER_SIZE];
	time_t		now;
	struct tm 	ts; /* timestamp decoder */
	int	nchars, rc;
	int elapsed;
	int     comp_status;
	struct step_record  *step = NULL;
	now = time(NULL);
	/*****************************
	 * THIS DOESN"T WORK YET!!!!!
	 *****************************/
	return SLURM_ERROR;
	if ((elapsed=now-step->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */
	
	nchars = snprintf(buf, BUFFER_SIZE, _jobstep_format,
			  JOB_STEP,
			  step->step_id,	/* stepid */
			  job_ptr->job_state,/* completion status */
			  0,     		/* completion code */
			  step->num_tasks,	/* number of tasks */
			  step->job_ptr->num_procs,/* number of cpus */
			  elapsed,	           /* elapsed seconds */
			  0,                    /* total cputime seconds */
			  0,    		/* total cputime seconds */
			  0,	/* user seconds */
			  0,/* user microseconds */
			  0,	/* system seconds */
			  0,/* system microsecs */
			  0,	/* max rss */
			  0,	/* max ixrss */
			  0,	/* max idrss */
			  0,	/* max isrss */
			  0,	/* max minflt */
			  0,	/* max majflt */
			  0,	/* max nswap */
			  0,	/* total inblock */
			  0,	/* total outblock */
			  0,	/* total msgsnd */
			  0,	/* total msgrcv */
			  0,	/* total nsignals */
			  0,	/* total nvcsw */
			  0,	/* total nivcsw */
			  0,		/* max vsize */
			  0,		/* max psize */
			  step->name);      	/* step exe name */
	rc = _print_record(step->job_ptr, step->start_time, buf);	
}
