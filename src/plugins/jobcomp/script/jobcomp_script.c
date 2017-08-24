/*****************************************************************************\
 *  jobcomp_script.c - Script running slurm job completion logging plugin.
 *****************************************************************************
 *  Produced at Center for High Performance Computing, North Dakota State
 *  University
 *  Written by Nathan Huff <nhuff@acm.org>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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
 *****************************************************************************
 *  Here is a list of the environment variables set
 *
 *  ACCOUNT		Account name
 *  BATCH		"yes" if submitted via sbatch, "no" otherwise
 *  END			Time of job termination, UTS
 *  GID			Group ID of job owner
 *  JOBID		SLURM Job ID
 *  JOBNAME		Job name
 *  JOBSTATE		Termination state of job (FIXME
 *  NODECNT		Count of allocated nodes
 *  NODES		List of allocated nodes
 *  PARTITION		Partition name used to run job
 *  PROCS		Count of allocated CPUs
 *  START		Time of job start, UTS
 *  STDERR		Job's stderr file name (if any)
 *  STDIN		Job's stdin file name (if any)
 *  STDOUT		Job's stdout file name (if any)
 *  SUBMIT		Time of job submission, UTS
 *  UID			User ID of job owner
 *  WORK_DIR		Job's working directory
 *
 *  BlueGene specific environment variables:
 *  BLOCKID		Name of Block ID
 *  CONNECT_TYPE	Connection type: small, torus or mesh
 *  GEOMETRY		Requested geometry of the job, "#x#x#" where "#"
 *			represents the X, Y and Z dimension sizes
\*****************************************************************************/

#include "config.h"

#if HAVE_PATHS_H
#  include <paths.h>
#endif

#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

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
 * the plugin (e.g., "jobcomp" for SLURM job completion logging) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load job completion logging plugins if the plugin_type string has a
 * prefix of "jobcomp/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[]       	= "Job completion logging script plugin";
const char plugin_type[]       	= "jobcomp/script";
const uint32_t plugin_version	= SLURM_VERSION_NUMBER;

static char * script = NULL;
static List comp_list = NULL;

static pthread_t script_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comp_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t comp_list_cond = PTHREAD_COND_INITIALIZER;
static int agent_exit = 0;

/*
 *  Local plugin errno
 */
static int plugin_errno = SLURM_SUCCESS;

static struct jobcomp_errno {
	int n;
	const char *descr;
} errno_table [] = {
	{ 0,      "No Error"              },
	{ EACCES, "Script access denied"  },
	{ EEXIST, "Script does not exist" },
	{ EINVAL, "JocCompLoc invalid"    },
	{ -1,     "Unknown Error"         }
};

/*
 *  Return string representation of plugin errno
 */
static const char * _jobcomp_script_strerror (int errnum)
{
	struct jobcomp_errno *ep = errno_table;

	while ((ep->n != errnum) && (ep->n != -1))
		ep++;

	return (ep->descr);
}

/*
 *  Structure for holding job completion information for later
 *   use by script;
 */
struct jobcomp_info {
	uint32_t jobid;
	uint32_t array_job_id;
	uint32_t array_task_id;
	uint32_t uid;
	uint32_t gid;
	uint32_t limit;
	uint32_t nprocs;
	uint32_t nnodes;
	uint16_t batch_flag;
	time_t submit;
	time_t start;
	time_t end;
	char *nodes;
	char *name;
	char *partition;
	char *jobstate;
	char *account;
	char *work_dir;
	char *std_in;
	char *std_out;
	char *std_err;
#ifdef HAVE_BG
	char *connect_type;
	char *geometry;
	char *blockid;
#endif
};

static struct jobcomp_info * _jobcomp_info_create (struct job_record *job)
{
	enum job_states state;
	struct jobcomp_info * j = xmalloc (sizeof (*j));

	j->jobid = job->job_id;
	j->uid = job->user_id;
	j->gid = job->group_id;
	j->name = xstrdup (job->name);
	j->array_job_id = job->array_job_id;
	j->array_task_id = job->array_task_id;

	if (IS_JOB_RESIZING(job)) {
		state = JOB_RESIZING;
		j->jobstate = xstrdup (job_state_string (state));
		if (job->resize_time)
			j->start = job->resize_time;
		else
			j->start = job->start_time;
		j->end = time(NULL);
	} else {
		/* Job state will typically have JOB_COMPLETING or JOB_RESIZING
		 * flag set when called. We remove the flags to get the eventual
		 * completion state: JOB_FAILED, JOB_TIMEOUT, etc. */
		state = job->job_state & JOB_STATE_BASE;
		j->jobstate = xstrdup (job_state_string (state));
		if (job->resize_time)
			j->start = job->resize_time;
		else if (job->start_time > job->end_time) {
			/* Job cancelled while pending and
			 * expected start time is in the future. */
			j->start = 0;
		} else
			j->start = job->start_time;
		j->end = job->end_time;
	}

	j->partition = xstrdup (job->partition);
	if ((job->time_limit == NO_VAL) && job->part_ptr)
		j->limit = job->part_ptr->max_time;
	else
		j->limit = job->time_limit;
	j->submit = job->details ? job->details->submit_time:job->start_time;
	j->batch_flag = job->batch_flag;
	j->nodes = xstrdup (job->nodes);
	j->nprocs = job->total_cpus;
	j->nnodes = job->node_cnt;
	j->account = job->account ? xstrdup (job->account) : NULL;
	if (job->details && job->details->work_dir)
		j->work_dir = xstrdup(job->details->work_dir);
	else
		j->work_dir = xstrdup("unknown");
	if (job->details) {
		if (job->details->std_in)
			j->std_in = xstrdup(job->details->std_in);
		if (job->details->std_out)
			j->std_out = xstrdup(job->details->std_out);
		if (job->details->std_err)
			j->std_err = xstrdup(job->details->std_err);
	}

#ifdef HAVE_BG
	j->connect_type = select_g_select_jobinfo_xstrdup(job->select_jobinfo,
						   SELECT_PRINT_CONNECTION);
	j->geometry = select_g_select_jobinfo_xstrdup(job->select_jobinfo,
					       SELECT_PRINT_GEOMETRY);
	j->blockid = select_g_select_jobinfo_xstrdup(job->select_jobinfo,
					      SELECT_PRINT_BG_ID);
#endif
	return (j);
}

static void _jobcomp_info_destroy (struct jobcomp_info *j)
{
	if (j == NULL)
		return;
	xfree (j->name);
	xfree (j->partition);
	xfree (j->nodes);
	xfree (j->jobstate);
	xfree (j->account);
	xfree (j->work_dir);
	xfree (j->std_in);
	xfree (j->std_out);
	xfree (j->std_err);
#ifdef HAVE_BG
	xfree (j->connect_type);
	xfree (j->geometry);
	xfree (j->blockid);
#endif
	xfree (j);
}

/*
 * Check if the script exists and if we can execute it.
 */
static int
_check_script_permissions(char * path)
{
	struct stat st;

	if (stat(path, &st) < 0) {
		plugin_errno = errno;
		return error("jobcomp/script: failed to stat %s: %m", path);
	}

	if (!(st.st_mode & S_IFREG)) {
		plugin_errno = EACCES;
		return error("jobcomp/script: %s isn't a regular file", path);
	}

	if (access(path, X_OK) < 0) {
		plugin_errno = EACCES;
		return error("jobcomp/script: %s is not executable", path);
	}

	return SLURM_SUCCESS;
}

static char ** _extend_env (char ***envp)
{
	char **ep;
	size_t newcnt = (xsize (*envp) / sizeof (char *)) + 1;

	*envp = xrealloc (*envp, newcnt * sizeof (char *));

	(*envp)[newcnt - 1] = NULL;
	ep = &((*envp)[newcnt - 2]);

	/*
	 *  Find last non-NULL entry
	 */
	while (*ep == NULL)
		--ep;

	return (++ep);
}

static int _env_append (char ***envp, const char *name, const char *val)
{
	char *entry = NULL;
	char **ep;

	if (val == NULL)
		val = "";

	xstrfmtcat (entry, "%s=%s", name, val);

	if (entry == NULL)
		return (-1);

	ep = _extend_env (envp);
	*ep = entry;

	return (0);
}

static int _env_append_fmt (char ***envp, const char *name,
		const char *fmt, ...)
  __attribute__ ((format (printf, 3, 4)));
static int _env_append_fmt (char ***envp, const char *name,
		const char *fmt, ...)
{
	char val[1024];
	va_list ap;

	va_start (ap, fmt);
	vsnprintf (val, sizeof (val) - 1, fmt, ap);
	va_end (ap);

	return (_env_append (envp, name, val));
}

static char ** _create_environment (struct jobcomp_info *job)
{
	char **env;
	char *tz;

	env = xmalloc (1 * sizeof (*env));
	env[0] = NULL;

	_env_append_fmt (&env, "JOBID", "%u",  job->jobid);
	_env_append_fmt (&env, "ARRAYJOBID", "%u", job->array_job_id);
	_env_append_fmt (&env, "ARRAYTASKID", "%u", job->array_task_id);
	_env_append_fmt (&env, "UID",   "%u",  job->uid);
	_env_append_fmt (&env, "GID",   "%u",  job->gid);
	_env_append_fmt (&env, "START", "%ld", (long)job->start);
	_env_append_fmt (&env, "END",   "%ld", (long)job->end);
	_env_append_fmt (&env, "SUBMIT","%ld", (long)job->submit);
	_env_append_fmt (&env, "PROCS", "%u",  job->nprocs);
	_env_append_fmt (&env, "NODECNT", "%u", job->nnodes);

	_env_append (&env, "BATCH", (job->batch_flag ? "yes" : "no"));
	_env_append (&env, "NODES",     job->nodes);
	_env_append (&env, "ACCOUNT",   job->account);
	_env_append (&env, "JOBNAME",   job->name);
	_env_append (&env, "JOBSTATE",  job->jobstate);
	_env_append (&env, "PARTITION", job->partition);
	_env_append (&env, "WORK_DIR",  job->work_dir);
	if (job->std_in)
		_env_append (&env, "STDIN",     job->std_in);
	if (job->std_out)
		_env_append (&env, "STDOUT",     job->std_out);
	if (job->std_err)
		_env_append (&env, "STDERR",     job->std_err);

#ifdef HAVE_BG
	_env_append (&env, "BLOCKID",      job->blockid);
	_env_append (&env, "CONNECT_TYPE", job->connect_type);
	_env_append (&env, "GEOMETRY",     job->geometry);
#endif

	if (job->limit == INFINITE)
		_env_append (&env, "LIMIT", "UNLIMITED");
	else
		_env_append_fmt (&env, "LIMIT", "%lu",
		                 (unsigned long) job->limit);

	if ((tz = getenv ("TZ")))
		_env_append_fmt (&env, "TZ", "%s", tz);
#ifdef _PATH_STDPATH
	_env_append (&env, "PATH", _PATH_STDPATH);
#else
	_env_append (&env, "PATH", "/bin:/usr/bin");
#endif

	return (env);
}

static int _redirect_stdio (void)
{
	int devnull;
	if ((devnull = open ("/dev/null", O_RDWR)) < 0)
		return error ("jobcomp/script: Failed to open /dev/null: %m");
	if (dup2 (devnull, STDIN_FILENO) < 0)
		return error ("jobcomp/script: Failed to redirect stdin: %m");
	if (dup2 (devnull, STDOUT_FILENO) < 0)
		return error ("jobcomp/script: Failed to redirect stdout: %m");
	if (dup2 (devnull, STDERR_FILENO) < 0)
		return error ("jobcomp/script: Failed to redirect stderr: %m");
	close (devnull);
	return (0);
}

static void _jobcomp_child (char * script, struct jobcomp_info *job)
{
	char * args[] = {script, NULL};
	const char *tmpdir;
	char **env;

#ifdef _PATH_TMP
	tmpdir = _PATH_TMP;
#else
	tmpdir = "/tmp";
#endif
	/*
	 * Reinitialize log so we can log any errors for
	 *  diagnosis
	 */
	log_reinit ();

	if (_redirect_stdio () < 0)
		exit (1);

	if (chdir (tmpdir) != 0) {
		error ("jobcomp/script: chdir (%s): %m", tmpdir);
		exit(1);
	}

	if (!(env = _create_environment (job))) {
		error ("jobcomp/script: Failed to create env!");
		exit (1);
	}

	execve(script, args, env);

	/*
	 * Failure of execve implies error
	 */
	error ("jobcomp/script: execve(%s): %m", script);
	exit (1);
}

static int _jobcomp_exec_child (char *script, struct jobcomp_info *job)
{
	pid_t pid;
	int status = 0;

	if (script == NULL || job == NULL)
		return (-1);

	if ((pid = fork()) < 0) {
		error ("jobcomp/script: fork: %m");
		return (-1);
	}

	if (pid == 0)
		_jobcomp_child (script, job);

	/*
	 *  Parent continues
	 */

	if (waitpid(pid, &status, 0) < 0)
		error ("jobcomp/script: waitpid: %m");

	if (WEXITSTATUS(status))
		error ("jobcomp/script: script %s exited with status %d",
		       script, WEXITSTATUS(status));

	return (0);
}


/*
 * Thread function that executes a script
 */
static void * _script_agent (void *args)
{
	while (1) {
		struct jobcomp_info *job;

		slurm_mutex_lock(&comp_list_mutex);

		if (list_is_empty(comp_list) && !agent_exit)
			slurm_cond_wait(&comp_list_cond, &comp_list_mutex);

		/*
		 * It is safe to unlock list mutex here. List has its
		 *  own internal mutex that protects the comp_list itself
		 */
		slurm_mutex_unlock(&comp_list_mutex);

		if ((job = list_pop(comp_list))) {
			_jobcomp_exec_child (script, job);
			_jobcomp_info_destroy (job);
		}

		/*
		 *  Exit if flag is set and we have no more entries to log
		 */
		if (agent_exit && list_is_empty (comp_list))
			break;
	}

	return NULL;
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init (void)
{
	pthread_attr_t attr;

	verbose("jobcomp/script plugin loaded init");

	slurm_mutex_lock(&thread_flag_mutex);

	if (comp_list)
		error("Creating duplicate comp_list, possible memory leak");
	if (!(comp_list = list_create((ListDelF) _jobcomp_info_destroy))) {
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	if (script_thread) {
		debug2( "Script thread already running, not starting another");
		slurm_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}

	slurm_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create(&script_thread, &attr, _script_agent, NULL);

	slurm_mutex_unlock(&thread_flag_mutex);
	slurm_attr_destroy(&attr);

	return SLURM_SUCCESS;
}

/* Set the location of the script to run*/
extern int slurm_jobcomp_set_location (char * location)
{
	if (location == NULL) {
		plugin_errno = EACCES;
		return error("jobcomp/script JobCompLoc needs to be set");
	}

	if (_check_script_permissions(location) != SLURM_SUCCESS)
		return SLURM_ERROR;

	xfree(script);
	script = xstrdup(location);

	return SLURM_SUCCESS;
}

int slurm_jobcomp_log_record (struct job_record *record)
{
	struct jobcomp_info * job;

	debug3("Entering slurm_jobcomp_log_record");

	if (!(job = _jobcomp_info_create (record)))
		return error ("jobcomp/script: Failed to create job info!");

	slurm_mutex_lock(&comp_list_mutex);
	list_append(comp_list, job);
	slurm_cond_broadcast(&comp_list_cond);
	slurm_mutex_unlock(&comp_list_mutex);

	return SLURM_SUCCESS;
}

/* Return the error code of the plugin*/
extern int slurm_jobcomp_get_errno(void)
{
	return plugin_errno;
}

/* Return a string representation of the error */
extern const char * slurm_jobcomp_strerror(int errnum)
{
	return _jobcomp_script_strerror (errnum);
}

static int _wait_for_thread (pthread_t thread_id)
{
	int i;

	for (i=0; i<20; i++) {
		slurm_cond_broadcast(&comp_list_cond);
		usleep(1000 * i);
		if (pthread_kill(thread_id, 0))
			return SLURM_SUCCESS;
	}

	error("Could not kill jobcomp script pthread");
	return SLURM_ERROR;
}

/* Called when script unloads */
extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&thread_flag_mutex);
	if (script_thread) {
		verbose("Script Job Completion plugin shutting down");
		agent_exit = 1;
		rc = _wait_for_thread(script_thread);
		script_thread = 0;
	}
	slurm_mutex_unlock(&thread_flag_mutex);

	xfree(script);
	if (rc == SLURM_SUCCESS) {
		slurm_mutex_lock(&comp_list_mutex);
		FREE_NULL_LIST(comp_list);
		slurm_mutex_unlock(&comp_list_mutex);
	}

	return rc;
}

/*
 * get info from the storage
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern List slurm_jobcomp_get_jobs(slurmdb_job_cond_t *job_cond)
{

	info("This function is not implemented.");
	return NULL;
}

/*
 * expire old info from the storage
 */
extern int slurm_jobcomp_archive(slurmdb_archive_cond_t *archive_cond)
{
	info("This function is not implemented.");
	return SLURM_SUCCESS;
}
