/*****************************************************************************\
 *  accounting_storage_filetxt.c - account interface to filetxt.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include <string.h>
#include <unistd.h>

#include "src/common/slurm_xlator.h"
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
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "jobacct" for Slurm job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  Slurm will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobacct/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Accounting storage FileTxt plugin";
const char plugin_type[] = "accounting_storage/filetxt";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

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
"%u "	/* min cpu */
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
	if (!job_ptr->details) {
		error("job_acct: job=%u doesn't exist", job_ptr->job_id);
		return SLURM_ERROR;
	}
	debug2("_print_record, job=%u, \"%s\"",
	       job_ptr->job_id, data);

	slurm_mutex_lock( &logfile_lock );

	if (fprintf(LOGFILE,
		    "%u %s %d %d %u %u - - %s\n",
		    job_ptr->job_id, job_ptr->partition,
		    (int)job_ptr->details->submit_time, (int)time,
		    job_ptr->user_id, job_ptr->group_id, data)
	    < 0)
		rc=SLURM_ERROR;
#ifdef HAVE_FDATASYNC
	fdatasync(LOGFILE_FD);
#endif
	slurm_mutex_unlock( &logfile_lock );

	return rc;
}

/* Make a copy of in_string replacing spaces with underscores.
 * Use xfree to release returned memory */
static char *_safe_dup(char *in_string)
{
	int i;
	char *out_string;

	if (in_string && in_string[0]) {
		out_string = xstrdup(in_string);
		for (i = 0; out_string[i]; i++) {
			if (isspace(out_string[i]))
				out_string[i]='_';
		}
	} else {
		out_string = xstrdup("(null)");
	}

	return out_string;
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

	if (slurmdbd_conf) {
		fatal("The filetxt plugin should not "
		      "be run from the slurmdbd.  "
		      "Please use a database plugin");
	}

	/*
	 * This check for the slurm user id is a quick and dirty patch
	 * to see if the controller is calling this, since we open the
	 * file in append mode stats could fail on it if the file
	 * isn't world writable.
	 */
	if (first && (getuid() == slurm_get_slurm_user_id())) {
		debug2("slurmdb_init() called");
		log_file = slurm_get_accounting_storage_loc();
		if (!log_file)
			log_file = xstrdup(DEFAULT_STORAGE_LOC);
		slurm_mutex_lock( &logfile_lock );
		if (LOGFILE)
			fclose(LOGFILE);

		if (*log_file != '/')
			fatal("AccountingStorageLoc must specify an "
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
		} else {
			if (chmod(log_file, prot))
				error("%s: chmod(%s):%m", __func__, log_file);
		}

		xfree(log_file);

		if (setvbuf(LOGFILE, NULL, _IOLBF, 0))
			error("setvbuf() failed");
		LOGFILE_FD = fileno(LOGFILE);
		slurm_mutex_unlock( &logfile_lock );
		storage_init = 1;
		/*
		 * since this can be loaded from many different places
		 * only tell us once.
		 */
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

extern void * acct_storage_p_get_connection(
	const slurm_trigger_callbacks_t *cb,
	int conn_num, uint16_t *persist_conn_flags,
	bool rollback, char *cluster_name)
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
				    List acct_list, slurmdb_user_cond_t *user_q)
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

extern int acct_storage_p_add_federations(void *db_conn, uint32_t uid,
					  List federation_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_tres(void *db_conn,
				     uint32_t uid, List tres_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_assocs(void *db_conn, uint32_t uid,
				     List assoc_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_qos(void *db_conn, uint32_t uid,
				  List qos_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_res(void *db_conn, uint32_t uid,
				  List res_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_wckeys(void *db_conn, uint32_t uid,
				  List wckey_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_add_reservation(void *db_conn,
					  slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_users(void *db_conn, uint32_t uid,
				       slurmdb_user_cond_t *user_q,
				       slurmdb_user_rec_t *user)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_accts(void *db_conn, uint32_t uid,
					   slurmdb_account_cond_t *acct_q,
					   slurmdb_account_rec_t *acct)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_clusters(void *db_conn, uint32_t uid,
					  slurmdb_cluster_cond_t *cluster_q,
					  slurmdb_cluster_rec_t *cluster)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_assocs(void *db_conn, uint32_t uid,
					      slurmdb_assoc_cond_t *assoc_q,
					      slurmdb_assoc_rec_t *assoc)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_federations(
				void *db_conn, uint32_t uid,
				slurmdb_federation_cond_t *fed_cond,
				slurmdb_federation_rec_t *fed)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_job(void *db_conn, uint32_t uid,
				      slurmdb_job_modify_cond_t *job_cond,
				      slurmdb_job_rec_t *job)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond,
				      slurmdb_qos_rec_t *qos)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_res(void *db_conn, uint32_t uid,
				      slurmdb_res_cond_t *ser_res_cond,
				      slurmdb_res_rec_t *ser_res)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_modify_wckeys(void *db_conn, uint32_t uid,
				      slurmdb_wckey_cond_t *wckey_cond,
				      slurmdb_wckey_rec_t *wckey)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_modify_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_users(void *db_conn, uint32_t uid,
				       slurmdb_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_coord(void *db_conn, uint32_t uid,
					List acct_list,
					slurmdb_user_cond_t *user_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_accts(void *db_conn, uint32_t uid,
				       slurmdb_account_cond_t *acct_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_clusters(void *db_conn, uint32_t uid,
					  slurmdb_account_cond_t *cluster_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_assocs(void *db_conn, uint32_t uid,
					      slurmdb_assoc_cond_t *assoc_q)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_federations(
					void *db_conn, uint32_t uid,
					slurmdb_federation_cond_t *fed_cond)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_remove_qos(void *db_conn, uint32_t uid,
				      slurmdb_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_remove_res(void *db_conn, uint32_t uid,
				      slurmdb_res_cond_t *res_cond)
{
	return NULL;
}

extern List acct_storage_p_remove_wckeys(void *db_conn, uint32_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern int acct_storage_p_remove_reservation(void *db_conn,
					     slurmdb_reservation_rec_t *resv)
{
	return SLURM_SUCCESS;
}

extern List acct_storage_p_get_users(void *db_conn, uid_t uid,
				     slurmdb_user_cond_t *user_q)
{
	return NULL;
}

extern List acct_storage_p_get_accts(void *db_conn, uid_t uid,
				     slurmdb_account_cond_t *acct_q)
{
	return NULL;
}

extern List acct_storage_p_get_clusters(void *db_conn, uid_t uid,
					slurmdb_cluster_cond_t *cluster_cond)
{
	return NULL;
}

extern List acct_storage_p_get_federations(void *db_conn, uid_t uid,
					   slurmdb_federation_cond_t *fed_cond)
{
	return NULL;
}

extern List acct_storage_p_get_config(void *db_conn, char *config_name)
{
	return NULL;
}

extern List acct_storage_p_get_tres(void *db_conn, uid_t uid,
				      slurmdb_tres_cond_t *tres_cond)
{
	slurmdb_tres_rec_t *tres_rec;
	List ret_list = list_create(slurmdb_destroy_tres_rec);

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_CPU;
	tres_rec->type = xstrdup("cpu");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_MEM;
	tres_rec->type = xstrdup("mem");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_ENERGY;
	tres_rec->type = xstrdup("energy");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_NODE;
	tres_rec->type = xstrdup("node");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_BILLING;
	tres_rec->type = xstrdup("billing");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_FS_DISK;
	tres_rec->type = xstrdup("fs");
	tres_rec->name = xstrdup("disk");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_VMEM;
	tres_rec->type = xstrdup("vmem");

	tres_rec = xmalloc(sizeof(slurmdb_tres_rec_t));
	list_append(ret_list, tres_rec);
	tres_rec->id = TRES_PAGES;
	tres_rec->type = xstrdup("pages");

	return ret_list;
}

extern List acct_storage_p_get_assocs(void *db_conn, uid_t uid,
				      slurmdb_assoc_cond_t *assoc_q)
{
	return NULL;
}

extern List acct_storage_p_get_events(void *db_conn, uint32_t uid,
				      slurmdb_event_cond_t *event_cond)
{
	return NULL;
}

extern List acct_storage_p_get_problems(void *db_conn, uid_t uid,
					slurmdb_assoc_cond_t *assoc_q)
{
	return NULL;
}

extern List acct_storage_p_get_qos(void *db_conn, uid_t uid,
				   slurmdb_qos_cond_t *qos_cond)
{
	return NULL;
}

extern List acct_storage_p_get_res(void *db_conn, uid_t uid,
				   slurmdb_res_cond_t *res_cond)
{
	return NULL;
}

extern List acct_storage_p_get_wckeys(void *db_conn, uid_t uid,
				      slurmdb_wckey_cond_t *wckey_cond)
{
	return NULL;
}

extern List acct_storage_p_get_reservations(void *db_conn, uid_t uid,
					    slurmdb_reservation_cond_t *resv_cond)
{
	return NULL;
}

extern List acct_storage_p_get_txn(void *db_conn, uid_t uid,
				   slurmdb_txn_cond_t *txn_cond)
{
	return NULL;
}

extern int acct_storage_p_get_usage(void *db_conn, uid_t uid,
				    void *in, int type,
				    time_t start, time_t end)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_roll_usage(void *db_conn,
				     time_t sent_start, time_t sent_end,
				     uint16_t archive_data,
				     rollup_stats_t *rollup_stats)
{
	int rc = SLURM_SUCCESS;

	return rc;
}

extern int acct_storage_p_fix_runaway_jobs(void *db_conn, uint32_t uid,
					   List jobs)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_node_down(void *db_conn,
					   struct node_record *node_ptr,
					   time_t event_time, char *reason,
					   uint32_t reason_uid)
{
	return SLURM_SUCCESS;
}
extern int clusteracct_storage_p_node_up(void *db_conn,
					 struct node_record *node_ptr,
					 time_t event_time)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_ctld(void *db_conn, uint16_t port)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_register_disconn_ctld(
	void *db_conn, char *control_host)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_fini_ctld(void *db_conn,
					   char *ip, uint16_t port,
					   char *cluster_nodes)
{
	return SLURM_SUCCESS;
}

extern int clusteracct_storage_p_cluster_tres(void *db_conn,
					      char *cluster_nodes,
					      char *tres_str_in,
					      time_t event_time,
					      uint16_t rpc_version)
{
	return SLURM_SUCCESS;
}

/*
 * load into the storage the start of a job
 */
extern int jobacct_storage_p_job_start(void *db_conn,
				       struct job_record *job_ptr)
{
	int	rc = SLURM_SUCCESS;
	char	buf[BUFFER_SIZE], *account, *nodes;
	char    *jname = NULL;
	long	priority;
	int track_steps = 0;

	if (!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	debug2("slurmdb_job_start() called");

	if (job_ptr->start_time == 0) {
		/* This function is called when a job becomes elligible to run
		 * in order to record reserved time (a measure of system
		 * over-subscription). We only use this with database
		 * plugins. */
		return rc;
	}

	priority = (job_ptr->priority == NO_VAL) ?
		   -1L : (long) job_ptr->priority;

	if (job_ptr->name && job_ptr->name[0]) {
		jname = _safe_dup(job_ptr->name);
	} else {
		jname = xstrdup("allocation");
		track_steps = 1;
	}

	account= _safe_dup(job_ptr->account);
	if (job_ptr->nodes && job_ptr->nodes[0])
		nodes = job_ptr->nodes;
	else
		nodes = "(null)";

	if (job_ptr->batch_flag)
		track_steps = 1;

	job_ptr->requid = -1; /* force to -1 for stats to know this
			       * hasn't been set yet */

	snprintf(buf, BUFFER_SIZE,
		 "%d %s %d %ld %u %s %s",
		 JOB_START, jname,
		 track_steps, priority, job_ptr->total_cpus,
		 nodes, account);

	rc = _print_record(job_ptr, job_ptr->start_time, buf);
	xfree(account);
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
	uint32_t job_state;
	int duration;
	uint32_t exit_code;

	if (!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	debug2("slurmdb_job_complete() called");
	if (IS_JOB_RESIZING(job_ptr)) {
		job_state = JOB_RESIZING;
		if (job_ptr->resize_time)
			duration = time(NULL) - job_ptr->resize_time;
		else
			duration = time(NULL) - job_ptr->start_time;
	} else {
		if (job_ptr->end_time == 0) {
			debug("jobacct: job %u never started", job_ptr->job_id);
			return SLURM_ERROR;
		}
		job_state = job_ptr->job_state & JOB_STATE_BASE;
		if (job_ptr->resize_time)
			duration = job_ptr->end_time - job_ptr->resize_time;
		else
			duration = job_ptr->end_time - job_ptr->start_time;
	}

	exit_code = job_ptr->exit_code;
	if (exit_code == 1) {
		/* This wasn't signaled, it was set by Slurm so don't
		 * treat it like a signal.
		 */
		exit_code = 256;
	}

	/* leave the requid as a %d since we want to see if it is -1
	   in stats */
	snprintf(buf, BUFFER_SIZE, "%d %d %u %u %u",
		 JOB_TERMINATED, duration,
		 job_state, job_ptr->requid, exit_code);

	return  _print_record(job_ptr, job_ptr->end_time, buf);
}

/*
 * load into the storage the start of a job step
 */
extern int jobacct_storage_p_step_start(void *db_conn,
					struct step_record *step_ptr)
{
	char buf[BUFFER_SIZE];
	int cpus = 0, rc;
	char node_list[BUFFER_SIZE];
	float float_tmp = 0;
	char *account, *step_name;

	if (!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = step_ptr->job_ptr->total_cpus;
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->job_ptr->nodes);
	} else {
		cpus = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->step_layout->node_list);
	}

	account   = _safe_dup(step_ptr->job_ptr->account);
	step_name = _safe_dup(step_ptr->name);

	step_ptr->job_ptr->requid = -1; /* force to -1 for stats to know this
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
		 0,	/* user microseconds */
		 0,	/* system seconds */
		 0,	/* system microsecs */
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
		 0,	/* min cpu */
		 0,	/* min cpu task */
		 float_tmp,	/* ave cpu */
		 step_name,	/* step exe name */
		 node_list,     /* name of nodes step running on */
		 0,	/* max vsize node */
		 0,	/* max rss node */
		 0,	/* max pages node */
		 0,	/* min cpu node */
		 account,
		 step_ptr->job_ptr->requid); /* requester user id */

	rc = _print_record(step_ptr->job_ptr, step_ptr->start_time, buf);
	xfree(account);
	xfree(step_name);
	return rc;
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
	int cpus = 0, rc;
	char node_list[BUFFER_SIZE];
	struct jobacctinfo *jobacct = (struct jobacctinfo *)step_ptr->jobacct;
	struct jobacctinfo dummy_jobacct;
	float ave_vsize = 0, ave_rss = 0, ave_pages = 0;
	float ave_cpu = 0;
	uint32_t ave_cpu2 = 0;
	char *account, *step_name;
	uint32_t exit_code;
	bool null_jobacct = false;

	if (!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	now = time(NULL);

	if (jobacct == NULL) {
		/* JobAcctGather=slurmdb_gather/none, no data to process */
		memset(&dummy_jobacct, 0, sizeof(dummy_jobacct));
		jobacct = &dummy_jobacct;
		null_jobacct = true;
	}

	if ((elapsed=now-step_ptr->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */

	exit_code = step_ptr->exit_code;
	comp_status = step_ptr->state;
	if (comp_status < JOB_COMPLETE) {
		if (exit_code == NO_VAL) {
			comp_status = JOB_CANCELLED;
			exit_code = 0;
		} else if (exit_code)
			comp_status = JOB_FAILED;
		else
			comp_status = JOB_COMPLETE;
	}

	if (!step_ptr->step_layout || !step_ptr->step_layout->task_cnt) {
		cpus = step_ptr->job_ptr->total_cpus;
		snprintf(node_list, BUFFER_SIZE, "%s", step_ptr->job_ptr->nodes);

	} else {
		cpus = step_ptr->step_layout->task_cnt;
		snprintf(node_list, BUFFER_SIZE, "%s",
			 step_ptr->step_layout->node_list);
	}

	if (!null_jobacct) {
		/* figure out the ave of the totals sent */
		if (cpus > 0) {
			ave_vsize = jobacct->tres_usage_in_tot[TRES_ARRAY_VMEM];
			ave_vsize /= cpus;
			ave_rss = jobacct->tres_usage_in_tot[TRES_ARRAY_MEM];
			ave_rss /= cpus;
			ave_pages = jobacct->tres_usage_in_tot[
				TRES_ARRAY_PAGES];
			ave_pages /= cpus;
			ave_cpu = jobacct->tres_usage_in_tot[TRES_ARRAY_CPU];
			ave_cpu /= cpus;
		}

		if (jobacct->tres_usage_in_max[TRES_ARRAY_CPU] != INFINITE64)
			ave_cpu2 = jobacct->tres_usage_in_max[TRES_ARRAY_CPU];
	}

	account   = _safe_dup(step_ptr->job_ptr->account);
	step_name = _safe_dup(step_ptr->name);

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
		 null_jobacct ? 0 : jobacct->tres_usage_in_max[TRES_ARRAY_VMEM],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_taskid[
			 TRES_ARRAY_VMEM],
		 ave_vsize,	/* ave vsize */
		 null_jobacct ? 0 : jobacct->tres_usage_in_max[TRES_ARRAY_MEM],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_taskid[
			 TRES_ARRAY_MEM],
		 ave_rss,	/* ave rss */
		 null_jobacct ? 0 : jobacct->tres_usage_in_max[
			 TRES_ARRAY_PAGES],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_taskid[
			 TRES_ARRAY_PAGES],
		 ave_pages,	/* ave pages */
		 ave_cpu2,	/* min cpu */
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_taskid[
			 TRES_ARRAY_CPU],
		 ave_cpu,	/* ave cpu */
		 step_name,	/* step exe name */
		 node_list, /* name of nodes step running on */
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_nodeid[
			 TRES_ARRAY_VMEM],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_nodeid[
			 TRES_ARRAY_MEM],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_nodeid[
			 TRES_ARRAY_PAGES],
		 null_jobacct ? 0 : jobacct->tres_usage_in_max_nodeid[
			 TRES_ARRAY_CPU],
		 account,
		 step_ptr->job_ptr->requid); /* requester user id */

	rc = _print_record(step_ptr->job_ptr, now, buf);
	xfree(account);
	xfree(step_name);
	return rc;
}

/*
 * load into the storage a suspension of a job
 */
extern int jobacct_storage_p_suspend(void *db_conn,
				     struct job_record *job_ptr)
{
	char buf[BUFFER_SIZE];
	static time_t	now = 0;
	static time_t	temp = 0;
	int elapsed;
	if (!storage_init) {
		debug("jobacct init was not called or it failed");
		return SLURM_ERROR;
	}

	/* tell what time has passed */
	if (!now)
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
		 job_ptr->job_state & JOB_STATE_BASE);/* job status */

	return _print_record(job_ptr, now, buf);
}


/*
 * get info from the storage
 * returns List of slurmdb_job_rec_t *
 * note List needs to be freed when called
 */
extern List jobacct_storage_p_get_jobs_cond(void *db_conn, uid_t uid,
					    slurmdb_job_cond_t *job_cond)
{
	return filetxt_jobacct_process_get_jobs(job_cond);
}

/*
 * expire old info from the storage
 */
extern int jobacct_storage_p_archive(void *db_conn,
				      slurmdb_archive_cond_t *arch_cond)
{
	return filetxt_jobacct_process_archive(arch_cond);

}

/*
 * load old info into the storage
 */
extern int jobacct_storage_p_archive_load(void *db_conn,
					  slurmdb_archive_rec_t *arch_rec)
{
	return SLURM_ERROR;
}

extern int acct_storage_p_update_shares_used(void *db_conn,
					     List shares_used)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_flush_jobs_on_cluster(
	void *db_conn, time_t event_time)
{
	/* put end times for a clean start */
	return SLURM_SUCCESS;
}

extern int acct_storage_p_reconfig(void *db_conn)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_reset_lft_rgt(void *db_conn, uid_t uid,
					List cluster_list)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_stats(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_clear_stats(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_get_data(void *db_conn, acct_storage_info_t dinfo,
				   void *data)
{
	return SLURM_SUCCESS;
}

extern int acct_storage_p_shutdown(void *db_conn, bool dbd)
{
	return SLURM_SUCCESS;
}
