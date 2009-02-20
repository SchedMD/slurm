/*****************************************************************************\
 *  jobacct_gather_aix.c - slurm job accounting gather plugin for AIX.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM, and Danny Auble, <da@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <signal.h>
#include "src/common/jobacct_common.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/slurmd/common/proctrack.h"

#ifdef HAVE_AIX
#include <procinfo.h>
#include <sys/types.h>
#define NPROCS 5000
#endif


/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a 
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job accounting API 
 * matures.
 */
const char plugin_name[] = "Job accounting gather AIX plugin";
const char plugin_type[] = "jobacct_gather/aix";
const uint32_t plugin_version = 100;
	
/* Other useful declarations */
#ifdef HAVE_AIX
typedef struct prec {	/* process record */
	pid_t   pid;
	pid_t   ppid;
	int     usec;   /* user cpu time */
	int     ssec;   /* system cpu time */
	int     pages;  /* pages */
	float	rss;	/* maxrss */
	float	vsize;	/* max virtual size */
} prec_t;

static int freq = 0;
static int pagesize = 0;
/* Finally, pre-define all the routines. */

static void _acct_kill_job(void);
static void _get_offspring_data(List prec_list, prec_t *ancestor, pid_t pid);
static void _get_process_data();
static void *_watch_tasks(void *arg);
static void _destroy_prec(void *object);

/* system call to get process table */
extern int getprocs(struct procsinfo *procinfo, int, struct fdsinfo *, 
		    int, pid_t *, int);
    /* procinfo:   pointer to array of procinfo struct */
    /* nproc:      number of user procinfo struct */
    /* sizproc:    size of expected procinfo structure */


/* 
 * _get_offspring_data() -- collect memory usage data for the offspring
 *
 * For each process that lists <pid> as its parent, add its memory
 * usage data to the ancestor's <prec> record. Recurse to gather data
 * for *all* subsequent generations.
 *
 * IN:	prec_list       list of prec's
 * 	ancestor	The entry in prec_list to which the data
 * 			should be added. Even as we recurse, this will
 * 			always be the prec for the base of the family
 * 			tree.
 * 	pid		The process for which we are currently looking 
 * 			for offspring.
 *
 * OUT:	none.
 *
 * RETVAL:	none.
 *
 * THREADSAFE! Only one thread ever gets here.
 */
static void _get_offspring_data(List prec_list, prec_t *ancestor, pid_t pid)
{
	ListIterator itr;
	prec_t *prec = NULL;
	
	itr = list_iterator_create(prec_list);
	while((prec = list_next(itr))) {
		if (prec->ppid == pid) {
			_get_offspring_data(prec_list, ancestor, prec->pid);
			debug2("adding %d to %d rss = %f vsize = %f", 
			      prec->pid, ancestor->pid, 
			      prec->rss, prec->vsize);
			ancestor->usec += prec->usec;
			ancestor->ssec += prec->ssec;
			ancestor->pages += prec->pages;
			ancestor->rss += prec->rss;
			ancestor->vsize += prec->vsize;
		}
	}
	list_iterator_destroy(itr);
	
	return;
}

/*
 * _get_process_data() - Build a table of all current processes
 *
 * IN:	pid.
 *
 * OUT:	none
 *
  * THREADSAFE! Only one thread ever gets here.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
static void _get_process_data() 
{
	struct procsinfo proc;
	pid_t *pids = NULL;
	int npids = 0;
	int i;
	uint32_t total_job_mem = 0;
	int pid = 0;
	static int processing = 0;
	prec_t *prec = NULL;
	struct jobacctinfo *jobacct = NULL;
	List prec_list = NULL;
	ListIterator itr;
	ListIterator itr2;
	
	if(!pgid_plugin && cont_id == (uint32_t)NO_VAL) {
		debug("cont_id hasn't been set yet not running poll");
		return;	
	}

	if(processing) {
		debug("already running, returning");
		return;
	}
	
	processing = 1;
	prec_list = list_create(_destroy_prec);

	if(!pgid_plugin) {
		/* get only the processes in the proctrack container */
		slurm_container_get_pids(cont_id, &pids, &npids);
		if(!npids) {
			debug4("no pids in this container %d", cont_id);
			goto finished;
		}
		for (i = 0; i < npids; i++) {
			pid = pids[i];
			if(!getprocs(&proc, sizeof(proc), 0, 0, &pid, 1)) 
				continue; /* Assume the process went away */
			prec = xmalloc(sizeof(prec_t));
			list_append(prec_list, prec);
			prec->pid = proc.pi_pid;
			prec->ppid = proc.pi_ppid;		
			prec->usec = proc.pi_ru.ru_utime.tv_sec +
				proc.pi_ru.ru_utime.tv_usec * 1e-6;
			prec->ssec = proc.pi_ru.ru_stime.tv_sec +
				proc.pi_ru.ru_stime.tv_usec * 1e-6;
			prec->pages = proc.pi_majflt;
			prec->rss = (proc.pi_trss + proc.pi_drss) * pagesize;
			//prec->rss *= 1024;
			prec->vsize = (proc.pi_tsize / 1024);
			prec->vsize += (proc.pi_dvm * pagesize);
			//prec->vsize *= 1024;
			/*  debug("vsize = %f = (%d/1024)+(%d*%d)",   */
/*    		      prec->vsize, proc.pi_tsize, proc.pi_dvm, pagesize);  */
		}
	} else {
		while(getprocs(&proc, sizeof(proc), 0, 0, &pid, 1) == 1) {
			prec = xmalloc(sizeof(prec_t));
			list_append(prec_list, prec);
			prec->pid = proc.pi_pid;
			prec->ppid = proc.pi_ppid;		
			prec->usec = proc.pi_ru.ru_utime.tv_sec +
				proc.pi_ru.ru_utime.tv_usec * 1e-6;
			prec->ssec = proc.pi_ru.ru_stime.tv_sec +
				proc.pi_ru.ru_stime.tv_usec * 1e-6;
			prec->pages = proc.pi_majflt;
			prec->rss = (proc.pi_trss + proc.pi_drss) * pagesize;
			//prec->rss *= 1024;
			prec->vsize = (proc.pi_tsize / 1024);
			prec->vsize += (proc.pi_dvm * pagesize);
			//prec->vsize *= 1024;
			/*  debug("vsize = %f = (%d/1024)+(%d*%d)",   */
/*    		      prec->vsize, proc.pi_tsize, proc.pi_dvm, pagesize);  */
		}
	}
	if(!list_count(prec_list))
		goto finished;
	
	slurm_mutex_lock(&jobacct_lock);
	if(!task_list || !list_count(task_list)) {
		slurm_mutex_unlock(&jobacct_lock);
		goto finished;
	}
	itr = list_iterator_create(task_list);
	while((jobacct = list_next(itr))) {
		itr2 = list_iterator_create(prec_list);
		while((prec = list_next(itr2))) {
			//debug2("pid %d ? %d", prec->ppid, jobacct->pid);
			if (prec->pid == jobacct->pid) {
				/* find all my descendents */
				_get_offspring_data(prec_list, prec, 
						    prec->pid);
						
				/* tally their usage */
				jobacct->max_rss = jobacct->tot_rss = 
					MAX(jobacct->max_rss, (int)prec->rss);
				total_job_mem += jobacct->max_rss;
				jobacct->max_vsize = jobacct->tot_vsize = 
					MAX(jobacct->max_vsize, 
					    (int)prec->vsize);
				jobacct->max_pages = jobacct->tot_pages =
					MAX(jobacct->max_pages, prec->pages);
				jobacct->min_cpu = jobacct->tot_cpu = 
					MAX(jobacct->min_cpu,
					    (prec->usec + prec->ssec));
				debug2("%d size now %d %d time %d",
				      jobacct->pid, jobacct->max_rss, 
				      jobacct->max_vsize, jobacct->tot_cpu);
				
				break;
			}
		}
		list_iterator_destroy(itr2);
	}
	list_iterator_destroy(itr);	
	slurm_mutex_unlock(&jobacct_lock);

	if (job_mem_limit) {
		debug("Job %u memory used:%u limit:%u KB", 
		      acct_job_id, total_job_mem, job_mem_limit);
	}
	if (acct_job_id && job_mem_limit &&
	    (total_job_mem > job_mem_limit)) {
		error("Job %u exceeded %u KB memory limit, being killed",
		       acct_job_id, job_mem_limit);
		_acct_kill_job();
	}

finished:
	list_destroy(prec_list);
	processing = 0;	

	return;
}

/* _acct_kill_job() issue RPC to kill a slurm job */
static void _acct_kill_job(void)
{
	slurm_msg_t msg;
	job_step_kill_msg_t req;

	slurm_msg_t_init(&msg);
	/* 
	 * Request message:
	 */
	req.job_id      = acct_job_id;
	req.job_step_id = NO_VAL;
	req.signal      = SIGKILL;
	req.batch_flag  = 0;
	msg.msg_type    = REQUEST_CANCEL_JOB_STEP;
	msg.data        = &req;

	slurm_send_only_controller_msg(&msg);
}

/* _watch_tasks() -- monitor slurm jobs and track their memory usage
 *
 * IN, OUT:	Irrelevant; this is invoked by pthread_create()
 */

static void *_watch_tasks(void *arg) 
{

	while(!jobacct_shutdown) {	/* Do this until shutdown is requested */
		if(!jobacct_suspended) {
			_get_process_data();	/* Update the data */ 
		}
		sleep(freq);
	} 
	return NULL;
}


static void _destroy_prec(void *object)
{
	prec_t *prec = (prec_t *)object;
	xfree(prec);
	return;
}

#endif

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	char *temp = slurm_get_proctrack_type();
	if(!strcasecmp(temp, "proctrack/pgid")) {
		info("WARNING: We will use a much slower algorithm with "
		     "proctrack/pgid, use Proctracktype=proctrack/aix "
		     "with %s", plugin_name);
		pgid_plugin = true;
	}
	xfree(temp);
	temp = slurm_get_accounting_storage_type();
	if(!strcasecmp(temp, ACCOUNTING_STORAGE_TYPE_NONE)) {
		error("WARNING: Even though we are collecting accounting "
		      "information you have asked for it not to be stored "
		      "(%s) if this is not what you have in mind you will "
		      "need to change it.", ACCOUNTING_STORAGE_TYPE_NONE);
	}
	xfree(temp);

	verbose("%s loaded", plugin_name);
	return SLURM_SUCCESS;
}

extern int fini ( void )
{
	return SLURM_SUCCESS;
}

extern struct jobacctinfo *jobacct_gather_p_create(jobacct_id_t *jobacct_id)
{
	return jobacct_common_alloc_jobacct(jobacct_id);
}

extern void jobacct_gather_p_destroy(struct jobacctinfo *jobacct)
{
	jobacct_common_free_jobacct(jobacct);
}

extern int jobacct_gather_p_setinfo(struct jobacctinfo *jobacct, 
				    enum jobacct_data_type type, void *data)
{
	return jobacct_common_setinfo(jobacct, type, data);
	
}

extern int jobacct_gather_p_getinfo(struct jobacctinfo *jobacct, 
				    enum jobacct_data_type type, void *data)
{
	return jobacct_common_getinfo(jobacct, type, data);
}

extern void jobacct_gather_p_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	jobacct_common_pack(jobacct, buffer);
}

extern int jobacct_gather_p_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	return jobacct_common_unpack(jobacct, buffer);
}

extern void jobacct_gather_p_aggregate(struct jobacctinfo *dest,
				       struct jobacctinfo *from)
{
	jobacct_common_aggregate(dest, from);
}

/*
 * jobacct_startpoll() is called when the plugin is loaded by
 * slurmd, before any other functions are called.  Put global
 * initialization here.
 */

extern int jobacct_gather_p_startpoll(uint16_t frequency)
{
	int rc = SLURM_SUCCESS;
	
#ifdef HAVE_AIX
	pthread_attr_t attr;
	pthread_t _watch_tasks_thread_id;

	debug("%s loaded", plugin_name);
	
	debug("jobacct: frequency = %d", frequency);
		
	jobacct_shutdown = false;
	
	if (frequency == 0) {	/* don't want dynamic monitoring? */
		debug2("jobacct AIX dynamic logging disabled");
		return rc;
	}

	freq = frequency;
	pagesize = getpagesize()/1024;
	task_list = list_create(jobacct_common_free_jobacct);
	
	/* create polling thread */
	slurm_attr_init(&attr);
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
		error("pthread_attr_setdetachstate error %m");
	
	if  (pthread_create(&_watch_tasks_thread_id, &attr,
			    &_watch_tasks, NULL)) {
		debug("jobacct failed to create _watch_tasks "
		      "thread: %m");
		frequency = 0;
	}
	else 
		debug3("jobacct AIX dynamic logging enabled");
	slurm_attr_destroy(&attr);
#else
	error("jobacct AIX not loaded, not an aix system, check slurm.conf");
#endif
	return rc;
}

extern int jobacct_gather_p_endpoll()
{
	jobacct_shutdown = true;

	return SLURM_SUCCESS;
}

extern void jobacct_gather_p_change_poll(uint16_t frequency)
{
#ifdef HAVE_AIX
	freq = frequency;
	if (freq == 0)
		jobacct_shutdown = true;
#endif
	return;
}

extern void jobacct_gather_p_suspend_poll()
{
	jobacct_common_suspend_poll();
}

extern void jobacct_gather_p_resume_poll()
{
	jobacct_common_resume_poll();
}

extern int jobacct_gather_p_set_proctrack_container_id(uint32_t id)
{
	return jobacct_common_set_proctrack_container_id(id);
}

extern int jobacct_gather_p_add_task(pid_t pid, jobacct_id_t *jobacct_id)
{
	return jobacct_common_add_task(pid, jobacct_id);
}

extern struct jobacctinfo *jobacct_gather_p_stat_task(pid_t pid)
{
#ifdef HAVE_AIX
	_get_process_data();
#endif
	if(pid)
		return jobacct_common_stat_task(pid);
	else
		return NULL;
}

extern struct jobacctinfo *jobacct_gather_p_remove_task(pid_t pid)
{
	return jobacct_common_remove_task(pid);
}

extern void jobacct_gather_p_2_sacct(sacct_t *sacct, 
				     struct jobacctinfo *jobacct)
{
	jobacct_common_2_sacct(sacct, jobacct);
}
