/*****************************************************************************\
 *  accounting_storage_filetxt.c - account interface to filetxt.
 *
 *  $Id: accounting_storage_filetxt.c 13061 2008-01-22 21:23:56Z da $
 *****************************************************************************
 *  Copyright (C) 2004-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include <strings.h>
#include "src/common/slurm_accounting_storage.h"
#include "filetxt_jobacct_process.h"

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
const char plugin_name[] = "Accounting storage FileTxt plugin";
const char plugin_type[] = "accounting_storage/filetxt";
const uint32_t plugin_version = 100;
static FILE *		LOGFILE;
static int		LOGFILE_FD;
static pthread_mutex_t  logfile_lock = PTHREAD_MUTEX_INITIALIZER;
static int              storage_init;
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
		    "%u %s %d %d %u %u %s - %s\n",
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

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
	static int first = 1;
	char *log_file = NULL;	
	int 		rc = SLURM_SUCCESS;
	mode_t		prot = 0600;
	struct stat	statbuf;
	
	if(slurmdbd_conf) {
		fatal("The filetxt plugin should not "
		      "be run from the slurmdbd.  "
		      "Please use a database plugin");
	}

	/* This check for the slurm user id is a quick and dirty patch
	 * to see if the controller is calling this, since we open the
	 * file in append mode sacct could fail on it if the file
	 * isn't world writable.
	 */
	if(first && (getuid() == slurm_get_slurm_user_id())) {
		debug2("jobacct_init() called");
		log_file = slurm_get_accounting_storage_loc();
		if(!log_file)
			log_file = xstrdup(DEFAULT_STORAGE_LOC);
		slurm_mutex_lock( &logfile_lock );
		if (LOGFILE)
			fclose(LOGFILE);
		
		if (*log_file != '/')
			fatal("JobAcctLogfile must specify an "
			      "absolute pathname");
		if (stat(log_file, &statbuf)==0)/* preserve current file mode */
			prot = statbuf.st_mode;
		LOGFILE = fopen(log_file, "a");
		if (LOGFILE == NULL) {
			error("open %s: %m", log_file);
			storage_init = 0;
			xfree(log_file);
			slurm_mutex_unlock( &logfile_lock );
			return SLURM_ERROR;
		} else
			chmod(log_file, prot); 
		
		xfree(log_file);
		
		if (setvbuf(LOGFILE, NULL, _IOLBF, 0))
			error("setvbuf() failed");
		LOGFILE_FD = fileno(LOGFILE);
		slurm_mutex_unlock( &logfile_lock );
		storage_init = 1;
		/* since this can be loaded from many different places
		   only tell us once. */
		verbose("%s loaded", plugin_name);
		first = 0;
	} else {
		debug4("%s loaded", plugin_name);
	}
	return rc;
}


extern int fini ( void )
{
	if (LOGFILE)
		fclose(LOGFILE);
	return SLURM_SUCCESS;
}

extern void * acct_storage_p_get_connection(bool make_agent, int conn_num,
					    bool rollback)
{
	return NULL;
}

extern int acct_storage_p_close_connection(void **db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_commit(void *db_conn, bool commit)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_users(void *db_conn, uint32_t uid,
				    List user_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_coord(void *db_conn, uint32_t uid,
				    List acct_list, acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_accts(void *db_conn, uint32_t uid,
				    List acct_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_clusters(void *db_conn, uint32_t uid,
				       List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_associations(void *db_conn, uint32_t uid,
					   List association_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid, 
				  List qos_list)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_users(void *db_conn, uint32_t uid,
				       acct_user_cond_t *user_q,
				       acct_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_accounts(void *db_conn, uint32_t uid,
					   acct_account_cond_t *acct_q,
					   acct_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_clusters(void *db_conn, uint32_t uid,
					  acct_cluster_cond_t *cluster_q,
					  acct_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_associations(void *db_conn, uint32_t uid,
					      acct_association_cond_t *assoc_q,
					      acct_association_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
				      acct_qos_cond_t *qos_cond,
				      acct_qos_rec_t *qos)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_users(void *db_conn, uint32_t uid,
				       acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_coord(void *db_conn, uint32_t uid,
					List acct_list, 
					acct_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_accts(void *db_conn, uint32_t uid,
				       acct_account_cond_t *acct_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_clusters(void *db_conn, uint32_t uid,
					  acct_account_cond_t *cluster_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_associations(void *db_conn, uint32_t uid,
					      acct_association_cond_t *assoc_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_qos(void *db_conn, uint32_t uid, 
				      acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     acct_user_cond_t *user_q)
{
	return NULL;
}

extern List acct_storage_p_get_accts(void *db_conn, uid_t uid,
				     acct_account_cond_t *acct_q)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					acct_account_cond_t *cluster_q)
{
	return NULL;
}

extern List acct_storage_p_get_associations(void *db_conn, uid_t uid,
					    acct_association_cond_t *assoc_q)
{
	return NULL;
}

extern List acct_storage_p_get_qos(void *db_conn, uid_t uid,
				   acct_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_txn(void *db_conn, uid_t uid,
				   acct_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    acct_association_rec_t *acct_assoc,
				    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn, 
				     time_t sent_start)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   char *cluster,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason)
{
	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_node_up(void *db_conn,
					 char *cluster,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn,
					       char *cluster,
					       uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_procs(void *db_conn,
					       char *cluster,
					       uint32_t procs,
					       time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_get_usage(
	void *db_conn, uid_t uid, 
	acct_cluster_rec_t *cluster_rec, time_t start, time_t end)
{

	return SLURM_SUCCESS;
}

/* 
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(void *db_conn, char *cluster_name,
				       struct job_record *job_ptr)
{
	int	i,
		rc=SLURM_SUCCESS,
		tmp;
	char	buf[BUFFER_SIZE], *jname, *account, *nodes;
	long	priority;
	int track_steps = 0;

	if(!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	debug2("jobacct_job_start() called");

	if (job_ptr->start_time == 0) {
		/* This function is called when a job becomes elligible to run
		 * in order to record reserved time (a measure of system
		 * over-subscription). We only use this with database
		 * plugins. */
		return rc;
	}

	priority = (job_ptr->priority == NO_VAL) ?
		-1L : (long) job_ptr->priority;

	if (job_ptr->name && (tmp = strlen(job_ptr->name))) {
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
		       track_steps, priority, job_ptr->total_procs,
		       nodes, account);

	rc = _print_record(job_ptr, job_ptr->start_time, buf);
	
	xfree(jname);
	return rc;
}

/* 
 * load into the storage the end of a job
 */
extern int jobacct_storage_p_job_complete(void *db_conn,
					  struct job_record *job_ptr)
{
	char buf[BUFFER_SIZE];
	if(!storage_init) {
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
	snprintf(buf, BUFFER_SIZE, "%d %d %d %u %u",
		 JOB_TERMINATED,
		 (int) (job_ptr->end_time - job_ptr->start_time),
		 job_ptr->job_state & (~JOB_COMPLETING),
		 job_ptr->requid, job_ptr->exit_code);
	
	return  _print_record(job_ptr, job_ptr->end_time, buf);
}

/* 
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn,
					struct step_record *step_ptr)
{
	char buf[BUFFER_SIZE];
	int cpus = 0;
	char node_list[BUFFER_SIZE];
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	float float_tmp = 0;
	char *account;
	
	if(!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

#ifdef HAVE_BG
	cpus = step_ptr->job_ptr->num_procs;
	select_g_get_jobinfo(step_ptr->job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	if(ionodes) {
		snprintf(node_list, BUFFER_SIZE, 
			 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->job_ptr->nodes);
	
#else
	if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = step_ptr->job_ptr->total_procs;
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->job_ptr->nodes);
	} else {
		cpus = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step_ptr->step_layout->node_list);
	}
#endif
	if (step_ptr->job_ptr->account && step_ptr->job_ptr->account[0])
		account = step_ptr->job_ptr->account;
	else
		account = "(null)";

	step_ptr->job_ptr->requid = -1; /* force to -1 for sacct to know this
				     * hasn't been set yet  */

	snprintf(buf, BUFFER_SIZE, _jobstep_format,
		 JOB_STEP,
		 step_ptr->step_id,	/* stepid */
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
		 step_ptr->name,    /* step exe name */
		 node_list,     /* name of nodes step running on */
		 0,	/* max vsize node */
		 0,	/* max rss node */
		 0,	/* max pages node */
		 0,	/* min cpu node */
		 account,
		 step_ptr->job_ptr->requid); /* requester user id */
		 
	return _print_record(step_ptr->job_ptr, step_ptr->start_time, buf);
}

/* 
 * load into the storage the end of a job step
 */
extern int jobacct_storage_p_step_complete(void *db_conn,
					   struct step_record *step_ptr)
{
	char buf[BUFFER_SIZE];
	time_t now;
	int elapsed;
	int comp_status;
	int cpus = 0;
	char node_list[BUFFER_SIZE];
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	struct jobacctinfo dummy_jobacct;
#ifdef HAVE_BG
	char *ionodes = NULL;
#endif
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0, ave_cpu2 = 0;
	char *account;
	uint32_t exit_code;

	if(!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}
	
	now = time(NULL);

	if (jobacct == NULL) {
		/* JobAcctGather=jobacct_gather/none, no data to process */
		bzero(&dummy_jobacct, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
	}
	
	if ((elapsed=now-step_ptr->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */

	exit_code = step_ptr->exit_code;
	if (exit_code == NO_VAL) {
		comp_status = JOB_CANCELLED;
		exit_code = 0;
	} else if (exit_code)
		comp_status = JOB_FAILED;
	else
		comp_status = JOB_COMPLETE;

#ifdef HAVE_BG
	cpus = step_ptr->job_ptr->num_procs;
	select_g_get_jobinfo(step_ptr->job_ptr->select_jobinfo, 
			     SELECT_DATA_IONODES, 
			     &ionodes);
	if(ionodes) {
		snprintf(node_list, BUFFER_SIZE, 
			 "%s[%s]", step_ptr->job_ptr->nodes, ionodes);
		xfree(ionodes);
	} else
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step_ptr->job_ptr->nodes);
	
#else
	if(!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = step_ptr->job_ptr->total_procs;
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->job_ptr->nodes);
	
	} else {
		cpus = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s", 
			 step_ptr->step_layout->node_list);
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

	if (step_ptr->job_ptr->account && step_ptr->job_ptr->account[0])
		account = step_ptr->job_ptr->account;
	else
		account = "(null)";

	snprintf(buf, BUFFER_SIZE, _jobstep_format,
		 JOB_STEP,
		 step_ptr->step_id,	/* stepid */
		 comp_status,		/* completion status */
		 exit_code,	/* completion code */
		 cpus,          	/* number of tasks */
		 cpus,                  /* number of cpus */
		 elapsed,	        /* elapsed seconds */
		 /* total cputime seconds */
		 jobacct->user_cpu_sec	
		 + jobacct->sys_cpu_sec,
		 /* total cputime seconds */
		 jobacct->user_cpu_usec	
		 + jobacct->sys_cpu_usec,
		 jobacct->user_cpu_sec,	/* user seconds */
		 jobacct->user_cpu_usec,/* user microseconds */
		 jobacct->sys_cpu_sec,	/* system seconds */
		 jobacct->sys_cpu_usec,/* system microsecs */
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
		 step_ptr->name,      	/* step exe name */
		 node_list, /* name of nodes step running on */
		 jobacct->max_vsize_id.nodeid,	/* max vsize task */
		 jobacct->max_rss_id.nodeid,	/* max rss task */
		 jobacct->max_pages_id.nodeid,	/* max pages task */
		 jobacct->min_cpu_id.nodeid,	/* min cpu task */
		 account,
		 step_ptr->job_ptr->requid); /* requester user id */
		 
	return _print_record(step_ptr->job_ptr, now, buf);	
}

/* 
 * load into the storage a suspention of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn,
				     struct job_record *job_ptr)
{
	char buf[BUFFER_SIZE];
	static time_t	now = 0;
	static time_t	temp = 0;
	int elapsed;
	if(!storage_init) {
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
	snprintf(buf, BUFFER_SIZE, "%d %d %d",
		 JOB_SUSPEND,
		 elapsed,
		 job_ptr->job_state & (~JOB_COMPLETING));/* job status */
		
	return _print_record(job_ptr, now, buf);
}

/* 
 * get info from the storage 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs(void *db_conn, uid_t uid,
				       List selected_steps,
				       List selected_parts,
				       sacct_parameters_t *params)
{
	List job_list = NULL;
	acct_job_cond_t job_cond;
	memset(&job_cond, 0, sizeof(acct_job_cond_t));

	job_cond.acct_list = selected_steps;
	job_cond.step_list = selected_steps;
	job_cond.partition_list = selected_parts;
	job_cond.cluster_list = params->opt_cluster_list;

	if (params->opt_uid >=0) {
		char *temp = xstrdup_printf("%u", params->opt_uid);
		job_cond.userid_list = list_create(NULL);
		list_append(job_cond.userid_list, temp);
	}	

	if (params->opt_gid >=0) {
		char *temp = xstrdup_printf("%u", params->opt_gid);
		job_cond.groupid_list = list_create(NULL);
		list_append(job_cond.groupid_list, temp);
	}	

	job_list = filetxt_jobacct_process_get_jobs(&job_cond);

	if(job_cond.userid_list)
		list_destroy(job_cond.userid_list);
	if(job_cond.groupid_list)
		list_destroy(job_cond.groupid_list);
		
	return job_list;
}

/* 
 * get info from the storage 
 * returns List of jobacct_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					    acct_job_cond_t *job_cond)
{
	return filetxt_jobacct_process_get_jobs(job_cond);
}

/* 
 * expire old info from the storage 
 */
extern void jobacct_storage_p_archive(void *db_conn,
				      List selected_parts,
				      void *params)
{
	filetxt_jobacct_process_archive(selected_parts, params);
	return;
}

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	void *db_conn, char *cluster, time_t event_time)
{
	/* put end times for a clean start */
	return SLURM_SUCCESS;
}
