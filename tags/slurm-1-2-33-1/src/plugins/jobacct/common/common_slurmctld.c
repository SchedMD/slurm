/*****************************************************************************\
 *  jobacct_common.c - common functions for almost all jobacct plugins.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/
#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "jobacct_common.h"

static FILE *		LOGFILE;
static int		LOGFILE_FD;
static pthread_mutex_t  logfile_lock = PTHREAD_MUTEX_INITIALIZER;
static char *		log_file = NULL;
static int              init;
/* Format of the JOB_STEP record */
const char *_jobstep_format = 
"%d "
"%u "	/* stepid */
"%d "	/* completion status */
"%u "	/* completion code */
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
"%u "	/* max vsize task */
"%.2f "	/* ave vsize */
"%u "	/* max rss */
"%u "	/* max rss task */
"%.2f "	/* ave rss */
"%u "	/* max pages */
"%u "	/* max pages task */
"%.2f "	/* ave pages */
"%.2f "	/* min cpu */
"%u "	/* min cpu task */
"%.2f "	/* ave cpu */
"%s "	/* step process name */
"%s "	/* step node names */
"%u "	/* max vsize node */
"%u "	/* max rss node */
"%u "	/* max pages node */
"%u "	/* min cpu node */
"%s "   /* account */
"%u";   /* requester user id */

/*
 * Print the record to the log file.
 */

static int _print_record(struct job_record *job_ptr, 
			 time_t time, char *data)
{ 
	static int   rc=SLURM_SUCCESS;
	char *block_id = NULL;
	if(!job_ptr->details) {
		error("job_acct: job=%u doesn't exist", job_ptr->job_id);
		return SLURM_ERROR;
	}
	debug2("_print_record, job=%u, \"%s\"",
	       job_ptr->job_id, data);
#ifdef HAVE_BG
	select_g_get_jobinfo(job_ptr->select_jobinfo, 
			     SELECT_DATA_BLOCK_ID, 
			     &block_id);
		
#endif
	if(!block_id)
		block_id = xstrdup("-");

	slurm_mutex_lock( &logfile_lock );

	if (fprintf(LOGFILE,
		    "%u %s %u %u %d %d %s - %s\n",
		    job_ptr->job_id, job_ptr->partition,
		    (int)job_ptr->details->submit_time, (int)time, 
		    job_ptr->user_id, job_ptr->group_id, block_id, data)
	    < 0)
		rc=SLURM_ERROR;
#ifdef HAVE_FDATASYNC
	fdatasync(LOGFILE_FD);
#endif
	slurm_mutex_unlock( &logfile_lock );
	xfree(block_id);

	return rc;
}

extern int common_init_slurmctld(char *job_acct_log)
{
	int 		rc = SLURM_SUCCESS;
	mode_t		prot = 0600;
	struct stat	statbuf;

	debug2("jobacct_init() called");
	slurm_mutex_lock( &logfile_lock );
	if (LOGFILE)
		fclose(LOGFILE);
	log_file=job_acct_log;
	if (*log_file != '/')
		fatal("JobAcctLogfile must specify an absolute pathname");
	if (stat(log_file, &statbuf)==0)       /* preserve current file mode */
		prot = statbuf.st_mode;
	LOGFILE = fopen(log_file, "a");
	if (LOGFILE == NULL) {
		error("open %s: %m", log_file);
		init = 0;
		slurm_mutex_unlock( &logfile_lock );
		return SLURM_ERROR;
	} else
		chmod(log_file, prot); 
	if (setvbuf(LOGFILE, NULL, _IOLBF, 0))
		error("setvbuf() failed");
	LOGFILE_FD = fileno(LOGFILE);
	slurm_mutex_unlock( &logfile_lock );
	init = 1;
	return rc;
}

extern int common_fini_slurmctld()
{
	if (LOGFILE)
		fclose(LOGFILE);
	return SLURM_SUCCESS;
}

extern int common_job_start_slurmctld(struct job_record *job_ptr)
{
	int	i,
		ncpus=0,
		rc=SLURM_SUCCESS,
		tmp;
	char	buf[BUFFER_SIZE], *jname, *account, *nodes;
	long	priority;
	int track_steps = 0;

	if(!init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	debug2("jobacct_job_start() called");

	if (job_ptr->start_time == 0) {
		/* This function is called when a job becomes elligible to run
		 * in order to record reserved time (a measure of system 
		 * over-subscription). We only use this in the Gold plugin. */
		return SLURM_SUCCESS;
	}

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
		track_steps = 1;
	}

	if (job_ptr->account && job_ptr->account[0])
		account = job_ptr->account;
	else
		account = "(null)";
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

	if(job_ptr->batch_flag)
		track_steps = 1;

	job_ptr->requid = -1; /* force to -1 for sacct to know this
			       * hasn't been set yet */

	tmp = snprintf(buf, BUFFER_SIZE,
		       "%d %s %d %ld %u %s %s",
		       JOB_START, jname,
		       track_steps, priority, job_ptr->num_procs,
		       nodes, account);

	rc = _print_record(job_ptr, job_ptr->start_time, buf);
	
	xfree(jname);
	return rc;
}

extern int common_job_complete_slurmctld(struct job_record *job_ptr)
{
	char buf[BUFFER_SIZE];
	if(!init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}
	
	debug2("jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug("jobacct: job %u never started", job_ptr->job_id);
		return SLURM_ERROR;
	}
	/* leave the requid as a %d since we want to see if it is -1
	   in sacct */
	snprintf(buf, BUFFER_SIZE, "%d %u %d %u",
		 JOB_TERMINATED,
		 (int) (job_ptr->end_time - job_ptr->start_time),
		 job_ptr->job_state & (~JOB_COMPLETING),
		 job_ptr->requid);
	
	return  _print_record(job_ptr, job_ptr->end_time, buf);
}

extern int common_step_start_slurmctld(struct step_record *step)
{
	char buf[BUFFER_SIZE];
	int cpus = 0;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	float float_tmp = 0;
	char *account;
	
	if(!init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

#ifdef HAVE_BG
	cpus = step->job_ptr->num_procs;
	select_g_get_jobinfo(step->job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	if(ionodes) {
		snprintf(node_list, BUFFER_SIZE, 
			 "%s[%s]", step->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step->job_ptr->nodes);
	
#else
	if(!step->step_layout || !step->step_layout->task_cnt) {
		cpus = step->job_ptr->num_procs;
		snprintf(node_list, BUFFER_SIZE, "%s", step->job_ptr->nodes);
	} else {
		cpus = step->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step->step_layout->node_list);
	}
#endif
	if (step->job_ptr->account && step->job_ptr->account[0])
		account = step->job_ptr->account;
	else
		account = "(null)";

	step->job_ptr->requid = -1; /* force to -1 for sacct to know this
				     * hasn't been set yet  */

	snprintf(buf, BUFFER_SIZE, _jobstep_format,
		 JOB_STEP,
		 step->step_id,	/* stepid */
		 JOB_RUNNING,		/* completion status */
		 0,     		/* completion code */
		 cpus,          	/* number of tasks */
		 cpus,                  /* number of cpus */
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
		 0,	/* max vsize */
		 0,	/* max vsize task */
		 float_tmp,	/* ave vsize */
		 0,	/* max rss */
		 0,	/* max rss task */
		 float_tmp,	/* ave rss */
		 0,	/* max pages */
		 0,	/* max pages task */
		 float_tmp,	/* ave pages */
		 float_tmp,	/* min cpu */
		 0,	/* min cpu task */
		 float_tmp,	/* ave cpu */
		 step->name,    /* step exe name */
		 node_list,     /* name of nodes step running on */
		 0,	/* max vsize node */
		 0,	/* max rss node */
		 0,	/* max pages node */
		 0,	/* min cpu node */
		 account,
		 step->job_ptr->requid); /* requester user id */
		 
	return _print_record(step->job_ptr, step->start_time, buf);
}

extern int common_step_complete_slurmctld(struct step_record *step)
{
		char buf[BUFFER_SIZE];
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	char node_list[BUFFER_SIZE];
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step->jobacct;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *account;

	if(!init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}
	
	now = time(NULL);
	
	if ((elapsed=now-step->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */
	if (step->exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;

#ifdef HAVE_BG
	cpus = step->job_ptr->num_procs;
	select_g_get_jobinfo(step->job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	if(ionodes) {
		snprintf(node_list, BUFFER_SIZE, 
			 "%s[%s]", step->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step->job_ptr->nodes);
	
#else
	if(!step->step_layout || !step->step_layout->task_cnt) {
		cpus = step->job_ptr->num_procs;
		snprintf(node_list, BUFFER_SIZE, "%s", step->job_ptr->nodes);
	
	} else {
		cpus = step->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step->step_layout->node_list);
	}
#endif
	/* figure out the ave of the totals sent */
	if(cpus > 0) {
		ave_vsize = jobacct->tot_vsize;
		ave_vsize /= cpus;
		ave_rss = jobacct->tot_rss;
		ave_rss /= cpus;
		ave_pages = jobacct->tot_pages;
		ave_pages /= cpus;
		ave_cpu = jobacct->tot_cpu;
		ave_cpu /= cpus;	
		ave_cpu /= 100;
	}
 
	if(jobacct->min_cpu != (uint32_t)NO_VAL) {
		ave_cpu2 = jobacct->min_cpu;
		ave_cpu2 /= 100;
	}

	if (step->job_ptr->account && step->job_ptr->account[0])
		account = step->job_ptr->account;
	else
		account = "(null)";

	snprintf(buf, BUFFER_SIZE, _jobstep_format,
		 JOB_STEP,
		 step->step_id,	/* stepid */
		 comp_status,		/* completion status */
		 step->exit_code,	/* completion code */
		 cpus,          	/* number of tasks */
		 cpus,                  /* number of cpus */
		 elapsed,	        /* elapsed seconds */
		 /* total cputime seconds */
		 jobacct->rusage.ru_utime.tv_sec	
		 + jobacct->rusage.ru_stime.tv_sec,
		 /* total cputime seconds */
		 jobacct->rusage.ru_utime.tv_usec	
		 + jobacct->rusage.ru_stime.tv_usec,
		 jobacct->rusage.ru_utime.tv_sec,	/* user seconds */
		 jobacct->rusage.ru_utime.tv_usec,/* user microseconds */
		 jobacct->rusage.ru_stime.tv_sec,	/* system seconds */
		 jobacct->rusage.ru_stime.tv_usec,/* system microsecs */
		 jobacct->rusage.ru_maxrss,	/* max rss */
		 jobacct->rusage.ru_ixrss,	/* max ixrss */
		 jobacct->rusage.ru_idrss,	/* max idrss */
		 jobacct->rusage.ru_isrss,	/* max isrss */
		 jobacct->rusage.ru_minflt,	/* max minflt */
		 jobacct->rusage.ru_majflt,	/* max majflt */
		 jobacct->rusage.ru_nswap,	/* max nswap */
		 jobacct->rusage.ru_inblock,	/* total inblock */
		 jobacct->rusage.ru_oublock,	/* total outblock */
		 jobacct->rusage.ru_msgsnd,	/* total msgsnd */
		 jobacct->rusage.ru_msgrcv,	/* total msgrcv */
		 jobacct->rusage.ru_nsignals,	/* total nsignals */
		 jobacct->rusage.ru_nvcsw,	/* total nvcsw */
		 jobacct->rusage.ru_nivcsw,	/* total nivcsw */
		 jobacct->max_vsize,	/* max vsize */
		 jobacct->max_vsize_id.taskid,	/* max vsize node */
		 ave_vsize,	/* ave vsize */
		 jobacct->max_rss,	/* max vsize */
		 jobacct->max_rss_id.taskid,	/* max rss node */
		 ave_rss,	/* ave rss */
		 jobacct->max_pages,	/* max pages */
		 jobacct->max_pages_id.taskid,	/* max pages node */
		 ave_pages,	/* ave pages */
		 ave_cpu2,	/* min cpu */
		 jobacct->min_cpu_id.taskid,	/* min cpu node */
		 ave_cpu,	/* ave cpu */
		 step->name,      	/* step exe name */
		 node_list, /* name of nodes step running on */
		 jobacct->max_vsize_id.nodeid,	/* max vsize task */
		 jobacct->max_rss_id.nodeid,	/* max rss task */
		 jobacct->max_pages_id.nodeid,	/* max pages task */
		 jobacct->min_cpu_id.nodeid,	/* min cpu task */
		 account,
		 step->job_ptr->requid); /* requester user id */
		 
	return _print_record(step->job_ptr, now, buf);	
}

extern int common_suspend_slurmctld(struct job_record *job_ptr)
{
	char buf[BUFFER_SIZE];
	static time_t	now = 0;
	static time_t	temp = 0;
	int elapsed;
	if(!init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}
	
	/* tell what time has passed */
	if(!now)
		now = job_ptr->start_time;
	temp = now;
	now = time(NULL);
	
	if ((elapsed=now-temp) < 0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */
	
	/* here we are really just going for a marker in time to tell when
	 * the process was suspended or resumed (check job state), we don't 
	 * really need to keep track of anything else */
	snprintf(buf, BUFFER_SIZE, "%d %u %d",
		 JOB_SUSPEND,
		 elapsed,
		 job_ptr->job_state & (~JOB_COMPLETING));/* job status */
		
	return _print_record(job_ptr, now, buf);

}
