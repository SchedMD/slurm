/*****************************************************************************\
 *  jobacct_linux.c - slurm job accounting plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM, and Danny Auble, <da@llnl.gov>
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
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "src/plugins/jobacct/common/jobacct_common.h"


#define BUFFER_SIZE 4096

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
const char plugin_name[] =
"Job accounting LINUX plugin";
const char plugin_type[] = "jobacct/linux";
const uint32_t plugin_version = 100;

/* Other useful declarations */
#if 0

typedef struct prec {	/* process record */
	pid_t	pid;
	pid_t	ppid;
	int	psize;	/* maxrss */
	int	vsize;	/* max virtual size */
} prec_t;

static int freq = 0;
static List prec_list = NULL;
/* Finally, pre-define all the routines. */

static void _get_offspring_data(prec_t *ancestor, pid_t pid);
static void _get_process_data();
static int _get_process_data_line(FILE *in, prec_t *prec);
static void *_watch_tasks(void *arg);
static void _destroy_prec(void *object);
#endif
/*
 * The following routine is called by the slurmd mainline
 */

int jobacct_p_init_struct(struct jobacctinfo *jobacct, uint16_t tid)
{
	return common_init_struct(jobacct, tid);
}

struct jobacctinfo *jobacct_p_alloc()
{
	return common_alloc_jobacct();
}

void jobacct_p_free(struct jobacctinfo *jobacct)
{
	common_free_jobacct(jobacct);
}

int jobacct_p_setinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return common_setinfo(jobacct, type, data);
	
}

int jobacct_p_getinfo(struct jobacctinfo *jobacct, 
		      enum jobacct_data_type type, void *data)
{
	return common_getinfo(jobacct, type, data);
}

void jobacct_p_aggregate(struct jobacctinfo *dest, struct jobacctinfo *from)
{
	common_aggregate(dest, from);
}

void jobacct_p_pack(struct jobacctinfo *jobacct, Buf buffer)
{
	common_pack(jobacct, buffer);
}

int jobacct_p_unpack(struct jobacctinfo **jobacct, Buf buffer)
{
	return common_unpack(jobacct, buffer);
}


int jobacct_p_init_slurmctld(char *job_acct_log)
{
	return common_init_slurmctld(job_acct_log);
}

int jobacct_p_fini_slurmctld()
{
	return common_fini_slurmctld();
}

int jobacct_p_job_start_slurmctld(struct job_record *job_ptr)
{
	return common_job_start_slurmctld(job_ptr);
}

int jobacct_p_job_complete_slurmctld(struct job_record *job_ptr) 
{
	return  common_job_complete_slurmctld(job_ptr);
}

int jobacct_p_step_start_slurmctld(struct step_record *step)
{
	return common_step_start_slurmctld(step);	
}

int jobacct_p_step_complete_slurmctld(struct step_record *step)
{
	return common_step_complete_slurmctld(step);	
}

int jobacct_p_suspend_slurmctld(struct job_record *job_ptr)
{
	return common_suspend_slurmctld(job_ptr);
}

/*
 * jobacct_startpoll() is called when the plugin is loaded by
 * slurmd, before any other functions are called.  Put global
 * initialization here.
 */

int jobacct_p_startpoll(int frequency)
{
	int rc = SLURM_SUCCESS;
	
#if 0
	pthread_attr_t attr;
	pthread_t _watch_tasks_thread_id;
#endif	
	debug("jobacct BLUEGENE plugin loaded");
	return rc;
#if 0
	/* FIXME!!!!!!!!!!!!!!!!!!!!
	   This was written for linux systems doesn't to anything on bluegene
	*/

	/* Parse the JobAcctParameters */

	
	debug("jobacct: frequency = %d", frequency);
		
	fini = false;
	
	if (frequency == 0) {	/* don't want dynamic monitoring? */
		debug2("jobacct LINUX dynamic logging disabled");
		return rc;
	}

	freq = frequency;
	
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
		debug3("jobacct LINUX dynamic logging enabled");
	pthread_attr_destroy(&attr);
#endif
	return rc;
}

int jobacct_p_endpoll()
{
	return common_endpoll();
}

int jobacct_p_add_task(pid_t pid, uint16_t tid)
{
	return common_add_task(pid, tid);
}

struct jobacctinfo *jobacct_p_stat_task(pid_t pid)
{
	return common_stat_task(pid);
}

int jobacct_p_remove_task(pid_t pid)
{
	return common_remove_task(pid);
}

void jobacct_p_suspendpoll()
{
	common_suspendpoll();
}

#if 0

/* 
 * _get_offspring_data() -- collect memory usage data for the offspring
 *
 * For each process that lists <pid> as its parent, add its memory
 * usage data to the ancestor's <prec> record. Recurse to gather data
 * for *all* subsequent generations.
 *
 * IN:	ancestor	The entry in precTable[] to which the data
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
static void _get_offspring_data(prec_t *ancestor, pid_t pid) 
{
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
static void _get_process_data(pid_t pid) 
{

	return;
}

/* _get_process_data_line() - get line of data from /proc/<pid>/stat
 *
 * IN:	in - input file channel
 * OUT:	prec - the destination for the data
 *
 * RETVAL:	==0 - no valid data
 * 		!=0 - data are valid
 *
 * Note: It seems a bit wasteful to do all those atoi() and
 *       atol() conversions that are implicit in the scanf(),
 *       but they help to ensure that we really are looking at the
 *       expected type of record.
 */
static int _get_process_data_line(FILE *in, prec_t *prec) 
{
	return 1;
}

/* _watch_tasks() -- monitor slurm jobs and track their memory usage
 *
 * IN, OUT:	Irrelevant; this is invoked by pthread_create()
 */

static void *_watch_tasks(void *arg) {

	while(!fini) {	/* Do this until slurm_jobacct_task_exit() stops us */
		if(!suspended) {
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
