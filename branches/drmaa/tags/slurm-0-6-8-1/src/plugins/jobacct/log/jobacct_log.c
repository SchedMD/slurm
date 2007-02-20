/*****************************************************************************\
 *  jobacct_log.c - slurm job accounting plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *  Written by Andy Riebs, <andy.riebs@hp.com>, who borrowed heavily
 *  from other parts of SLURM.
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
 *  This file is patterned after jobcomp_log.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <slurm/slurm_errno.h>

#include "src/common/hostlist.h"
#include "src/common/slurm_cred.h"
#include "src/common/slurm_jobacct.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

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
    "Job accounting LOG plugin for slurmctld and slurmd";
const char plugin_type[] = "jobacct/log";
const uint32_t plugin_version = 100;

/*
 * Data and routines common to the slurmctld and slurmd plugins
 */

#define DEFAULT_SEND_RETRIES 3
#define DEFAULT_SEND_RETRY_DELAY 5
#define DEFAULT_STAGGER_SLOT_SIZE 1
#ifndef HOST_NAME_MAX
#ifdef MAXHOSTNAMELEN
#define HOST_NAME_MAX MAXHOSTNAMELEN
#else
#define HOST_NAME_MAX 256
#endif
#endif
#define MAX_BUFFER_SIZE 1024
#define MAX_MSG_SIZE 1024
#define NOT_FOUND "NOT_FOUND"

typedef enum _stats_msg_type {
		TO_CONTROLLER=100,
		TO_MYNODE=101,
		TO_NODE0=102
} _stats_msg_type_t;

typedef struct _stats_msg {
	_stats_msg_type_t	msg_type;
	uint32_t	 	jobid;   /* in network order! */
	uint32_t		stepid;  /* ditto */
	uint16_t		datalen; /* ditto */
	char			data[MAX_MSG_SIZE];
} _stats_msg_t;

/* Job records */

static List		jobsteps_active,
			jobsteps_retiring;
static pthread_mutex_t	jobsteps_active_lock = PTHREAD_MUTEX_INITIALIZER,
			jobsteps_retiring_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct	_jrec {
	uint32_t	jobid;		/* record is for this SLURM job id */
	uint32_t	stepid;		/* record is for this step id */
	uint32_t	nprocs;		/* number of processes */
	uint32_t	ntasks;		/* number of tasks on this node */
	uint32_t	ncpus;		/* number of processors */
	uint32_t	nnodes;		/* number of nodes */
	uint32_t	nodeid;		/* relative node position */
	time_t		start_time;	/* when the jobstep started */
	struct rusage	rusage;		/* capture everything from wait3() */
	int	        status;		/* First non-zero completion code */
	uint32_t        max_vsize;	/* max virtual mem size of any proc  */
	uint32_t        max_psize;	/* max phys. memory size of any proc */ 
	int		not_reported;	/* Used by nodes 0,1 to track how
					   many nodes still have to report in */
	char		node0[HOST_NAME_MAX]; /* It's proven to be hard to */ 
	char		node1[HOST_NAME_MAX]; /* keep track of these!      */
} _jrec_t;

/* Other useful declarations */

static	char		*rev_stg = "$Revision$";

/*
 * The following data are used by slurmctld
 */


/* File used for logging */
static char *		log_file = NULL;
static pthread_mutex_t  logfile_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *		LOGFILE;
static int		LOGFILE_FD;


/* For passing data between slurmd processes */

int	slurmd_port;

typedef enum _mynode_msg_type {
	LAUNCH=1,		/* make them explicit for easy debugging */
	TASKDATA=2
} _mynode_msg_type_t;

typedef struct _mynode_msg {
	_mynode_msg_type_t	msgtype;
	_jrec_t			jrec;
} _mynode_msg_t;


/* Data used in the slurmd session manager calls slurmd_jobacct_smgr() and
 * slurmd_jobacct_task_exit() */

static long	max_psize = 0,
		max_vsize = 0;

typedef struct _prec {	/* process record */
	pid_t	pid;
	pid_t	ppid;
	long	psize;	/* maxrss */
	long	vsize;	/* max virtual size */
} _prec_t;

static _prec_t		*precTable = NULL; /* Keep it thread safe! */
static pthread_mutex_t  precTable_lock = PTHREAD_MUTEX_INITIALIZER;
			/* precTable_lock prevents collisions between
			 * slurmd_jobacct_task_exit() and _watch_tasks() */
static long		nPrecs = 0;	   /* Number of precTable entries */
static long		prec_frequency = 0; /* seconds between precTable 
					       updates, 0 = don't do it */
static pthread_t _watch_tasks_thread_id;

static long		max_send_retries = DEFAULT_SEND_RETRIES,
			max_send_retry_delay = DEFAULT_SEND_RETRY_DELAY,
			stagger_slot_size = DEFAULT_STAGGER_SLOT_SIZE;

/* Finally, pre-define all the routines. */

static _jrec_t	*_alloc_jrec(slurmd_job_t *job);
static _jrec_t	*_get_jrec_by_jobstep(List jrecs,
			uint32_t jobid, uint32_t stepid);
static void	 _aggregate_job_data(_jrec_t *jrec, _jrec_t *inrec);
static void	 _get_node_01_names(char *node0, char *node1, char **env);
static void	 _get_offspring_data(_prec_t *ancestor, pid_t pid);
static _prec_t	*_get_process_data(void);
static int	 _get_process_data_line(FILE *in, _prec_t *prec);
static void	 _get_slurmctld_syms(void);
static int	 _pack_jobrec(_jrec_t *outrec, _jrec_t *inrec);
static int	 _print_record(struct job_record *job_ptr, char *data);
static void	 _process_mynode_msg(_mynode_msg_t *msg);
static void	 _process_mynode_msg_launch(_jrec_t *jrec);
static void	 _process_mynode_msg_taskdata(_jrec_t *jrec);
static void	 _process_node0_data(_jrec_t *inrec);
static void	 _process_node0_msg(_jrec_t *inrec);
static void	 _remove_jrec_from_list(List jrecs, 
			uint32_t jobid, uint32_t stepid);
static int	 _send_data_to_mynode(_mynode_msg_type_t msgtype, _jrec_t *jrec);
static int	 _send_data_to_node_0(_jrec_t *jrec);
static int	 _send_data_to_slurmctld(_jrec_t *jrec, int done); 
static int	 _send_msg_to_slurmctld(_stats_msg_t *stats);
static void	 _stagger_time(long nodeid, long n_contenders);
static int	 _unpack_jobrec(_jrec_t *outrec, _jrec_t *inrec);
static void	*_watch_tasks(void *arg);

static struct job_record *(*find_job_record_in_slurmctld)(uint32_t job_id);


/*
 * The following routines are used from both slurmctld and slurmd
 *
 */

int slurm_jobacct_process_message(struct slurm_msg *msg)
{
	struct job_record	*job_ptr;
	uint32_t		jobid,
				stepid;
	int			rc = SLURM_SUCCESS;
	jobacct_msg_t		*jmsg;
	_stats_msg_type_t	msgtype;
	_stats_msg_t		*stats;

	jmsg    = msg->data;
	stats   = (_stats_msg_t *)jmsg->data;
	msgtype = ntohl(stats->msg_type);
	jobid   = ntohl(stats->jobid);
	stepid  = ntohl(stats->stepid); 
	debug2("jobacct(%d): in slurm_jobacct_process_message, "
			"job %u.%u, msgtype=%d", 
			getpid(), jobid, stepid, msgtype);

	switch (msgtype) {

/*
 * Messages sent from slurmd to slurmctld; all we need to do is write
 * the data, which arrive as formatted ASCII text, to the log file.
 *
 * We flush the file to ensure that the data are immediately available
 * to potential consumers.
 */
	  case TO_CONTROLLER: 
		debug2("jobacct(%d) slurmctld received record for "
				"job %d, \"%30s...\"",
				getpid(), jobid, stats->data);
		if ((job_ptr = (*find_job_record_in_slurmctld)(jobid))==NULL) {
			error("jobacct(%d): job %lu record not found, "
					"record starts %30s",
					getpid(), jobid, jmsg->data);
			return(SLURM_ERROR);
		}
		rc = _print_record(job_ptr, stats->data);
		break;

/*
 * The slurmd session manager (smgr.c) invokes g_slurmd_jobacct_task_exit(),
 * which forwards the data here, to the slurmd mainline, where we can aggregate
 * all of the data for the job.
 */

	  case TO_MYNODE:
		_process_mynode_msg( (_mynode_msg_t *) stats->data );
		break;

/*
 * Once the slurmd session manager is done, for whatever reason, the job
 * manager (mgr.c) takes any task data and sends it here for aggregation.
 */

	  case TO_NODE0:
		_process_node0_msg( (_jrec_t *)stats->data);
		break;

	  default:
		error("jobacct(%d): unknown message type: %d",
				getpid(), msgtype);
		rc = SLURM_ERROR;
		break;
	}

	return rc;
}

/*
 * Send a message to slurmctld.
 */

static int _send_msg_to_slurmctld(_stats_msg_t *stats) {

	slurm_msg_t		*msg,
				*retmsg=NULL;
	jobacct_msg_t		*jmsg;
	int			rc = SLURM_SUCCESS,
				retry;

	debug2("jobacct(%d): _send_msg_to_slurmctld, msgtype=%d",
			getpid(), ntohl(stats->msg_type)); 
	jmsg = xmalloc(sizeof(jobacct_msg_t));
	jmsg->len = sizeof(_stats_msg_t);
	jmsg->data = (char *)stats;
	msg = xmalloc(sizeof(slurm_msg_t));
	retmsg = xmalloc(sizeof(slurm_msg_t));
	msg->msg_type	= MESSAGE_JOBACCT_DATA;
	msg->data	= jmsg;
	msg->data_size  = sizeof(jobacct_msg_t);

	for (retry=0; retry<max_send_retries; retry++) {
		if ((rc=slurm_send_recv_controller_msg(msg, retmsg)) >= 0)
			break;
		if (retry==0)
			srand( (getpid()*times(NULL))%RAND_MAX );
		sleep(1+(rand()/(RAND_MAX/max_send_retry_delay)));
	}
	if (rc<0)
		error("jobacct(%d): _send_msg_to_slurmctld(msg, retmsg)"
				" says %d (%m) after %d tries",
				getpid(), rc, retry);
	else {
		debug3("jobacct(%d): slurm_send_recv_controller_msg says %d",
				rc);
		slurm_free_cred(retmsg->cred);
	}
	xfree(jmsg);
	xfree(msg);
	debug2("jobacct(%d): leaving _send_msg_to_slurmctld, rc=%d",
			getpid(), rc);

	return rc;
}

/*
 * JOBACCT/LOG plugin code for SLURMCTLD
 */ 


/*
 * slurmctld_jobacct_init() is called when the plugin is loaded by
 * slurmctld, before any other functions are called.  Put global
 * initialization here.
 */

int slurmctld_jobacct_init(char *job_acct_loc, char *job_acct_parameters)
{
	int 		rc = SLURM_SUCCESS;
	mode_t		prot = 0600;
	struct stat	statbuf;


	debug2("slurmctld_jobacct_init() called");
	info("jobacct LOG plugin loaded (%s)", rev_stg);
	slurm_mutex_lock( &logfile_lock );
	if (LOGFILE)
		fclose(LOGFILE);
	log_file=job_acct_loc;
	if (*log_file != '/')
		fatal("JobAcctLoc must specify an absolute pathname");
	if (stat(log_file, &statbuf)==0)	/* preserve current file mode */
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
	_get_slurmctld_syms();
	return rc;
}
 
int slurmctld_jobacct_job_complete(struct job_record *job_ptr) 
{
	int		rc = SLURM_SUCCESS,
			tmp;
	char		*buf;
	struct tm	ts; /* timestamp decoder */

	debug2("slurmctld_jobacct_job_complete() called");
	if (job_ptr->end_time == 0) {
		debug2("jobacct: job %u never started", job_ptr->job_id);
		return rc;
	}
	gmtime_r(&job_ptr->end_time, &ts);
	buf = xmalloc(MAX_BUFFER_SIZE);
	tmp = snprintf(buf, MAX_MSG_SIZE,
		"JOB_TERMINATED 1 12 %u %04d%02d%02d%02d%02d%02d %s",
		(int) (job_ptr->end_time - job_ptr->start_time),
		1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
		ts.tm_hour, ts.tm_min, ts.tm_sec,
		job_state_string_compact(
			(job_ptr->job_state) & ~JOB_COMPLETING));
	if (tmp >= MAX_MSG_SIZE) {
		error("slurmctld_jobacct_job_complete buffer overflow");
		rc = SLURM_ERROR;
	} else {
		rc = _print_record(job_ptr, buf);
	}
	xfree(buf);
	return rc;
}

int slurmctld_jobacct_job_start(struct job_record *job_ptr)
{
	int		i,
			ncpus=0,
			rc=SLURM_SUCCESS,
			tmp;
	char		*buf,
			*jname;
	long		priority;
	
	debug2("slurmctld_jobacct_job_start() called");
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
		jname = (char*) xmalloc(10);
		strncpy(jname, "(noname)", 10);
	}
	buf = xmalloc(MAX_BUFFER_SIZE);
	tmp = snprintf(buf, MAX_MSG_SIZE,
			"JOB_START 1 16 %d %d %s %u %ld %u %s",
			job_ptr->user_id, job_ptr->group_id, jname,
			job_ptr->batch_flag, priority, ncpus,
			job_ptr->nodes);
	if (tmp >= MAX_MSG_SIZE) {
		error("slurmctld_jobacct_job_start buffer overflow");
		rc = SLURM_ERROR;
	} else {
		rc = _print_record(job_ptr, buf);
	}
	xfree(buf);
	xfree(jname);
	return rc;
}
 
/*
 * Some symbols that we need when plugged in to slurmctld are unresolvable
 * when plugged in to slurmd; this makes the plugrack routines very unhappy,
 * so we'll just resolve them ourselves.
 */
static void _get_slurmctld_syms(void)
{
	void	*slurmctld_handle;

	if ((slurmctld_handle=dlopen("", RTLD_LAZY)) == NULL)
		error("dlopen failed in _get_slurmctld_syms");
	if ((find_job_record_in_slurmctld=
				dlsym(slurmctld_handle, "find_job_record"))==NULL)
		error("find_job_record not found in _get_slurmctld_syms");
	dlclose(slurmctld_handle);
}






/*
 * Print the record to the log file.
 */

static int _print_record(struct job_record *job_ptr, char *data)
{ 
	struct tm   *ts; /* timestamp decoder */
	static int   rc=SLURM_SUCCESS;

	ts = xmalloc(sizeof(struct tm));
	gmtime_r(&job_ptr->start_time, ts);
	debug2("jobacct:_print_record, job=%u, rec starts \"%20s",
			job_ptr->job_id, data);
	slurm_mutex_lock( &logfile_lock );
	if (fprintf(LOGFILE,
			"%u %s %04d%02d%02d%02d%02d%02d %u %d.%d - %s\n",
			job_ptr->job_id, job_ptr->partition,
			1900+(ts->tm_year), 1+(ts->tm_mon), ts->tm_mday,
			ts->tm_hour, ts->tm_min, ts->tm_sec, 
			(int) job_ptr->start_time, 
			job_ptr->user_id, job_ptr->group_id, data)
		< 0)
		rc=SLURM_ERROR;
	fdatasync(LOGFILE_FD);
	slurm_mutex_unlock( &logfile_lock );
	xfree(ts);
	return rc;
}



/*
 * JOBACCT/LOG plugin code for SLURMD
 */

/* Format of the JOB_STEP record */

#define RECORD_VERSION 1
#define NUM_FIELDS 38
const char	*_jobstep_format =
		"JOB_STEP "
		"%u "	/* RECORD_VERSION */
		"%u "	/* NUM_FIELDS */
		"%u "	/* stepid */
		"%s "	/* completion time */
		"%s "	/* completion status */
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
		"%u"	/* max psize */
		;

/*
 * The following routine is called by the slurmd mainline
 */

/*
 * slurmd_jobacct_init() is called when the plugin is loaded by
 * slurmd, before any other functions are called.  Put global
 * initialization here.
 */

int slurmd_jobacct_init(char *job_acct_parameters)
{
	int 	i,
		plen,
		rc = SLURM_SUCCESS;
	static struct {
		long	*val;
		char	*name;
	} parameters[] = {
		{ &prec_frequency, "Frequency=" },
		{ &max_send_retries, "MaxSendRetries=" },
		{ &max_send_retry_delay, "MaxSendRetryDelay=" },
		{ &stagger_slot_size, "StaggerSlotSize=" },
		{ 0, "" },
	};
	char	*parameter_buffer,
		*next_parameter,
		*this_parameter,
		*val_ptr;

	info("jobacct LOG plugin (%s)", rev_stg);

	/* Parse the JobAcctParameters */

	parameter_buffer = xstrdup(job_acct_parameters);
	next_parameter = parameter_buffer;
	while (strlen(next_parameter)) {
		this_parameter = next_parameter;
		if ((next_parameter = strstr(this_parameter, ","))==NULL)
			next_parameter = "";
		else
			*next_parameter++ = 0;
		if ((val_ptr=strstr(this_parameter, "="))==NULL) {
			error("jobacct: parameter \"%s\" missing \"=\", "
				"ignoring it", this_parameter);
			continue;
		}
		val_ptr++;
		for (i=0; (plen=strlen(parameters[i].name)); i++) {
			if (strncasecmp(parameters[i].name,
					this_parameter, plen)==0) {
				*parameters[i].val=strtol(val_ptr, NULL, 0);
				goto next_parm;
			}
		}
		error("jobacct: unknown parameter, \"%s\", ignoring it",
				this_parameter);
	next_parm: ;
	}
	xfree(parameter_buffer);

	if (max_send_retries < 1) {
		error("jobacct: \"MaxSendRetries=%ld\" is invalid; using %ld",
				max_send_retries, DEFAULT_SEND_RETRIES);
		max_send_retries = DEFAULT_SEND_RETRIES;
	}
	if (max_send_retry_delay < 0) {
		error("jobacct: \"MaxSendRetryDelay=%ld\" is invalid;"
				" using %ld",
				max_send_retry_delay, DEFAULT_SEND_RETRY_DELAY);
		max_send_retry_delay = DEFAULT_SEND_RETRY_DELAY;
	}
	if (stagger_slot_size < 0) {
		error("jobacct: \"StaggerSlotSize=%ld\" is invalid;"
				" using %ld",
				stagger_slot_size, DEFAULT_STAGGER_SLOT_SIZE);
		stagger_slot_size = DEFAULT_STAGGER_SLOT_SIZE;
	}

	debug2("jobacct: frequency=%ld, MaxSendRetries=%ld,"
			" MaxSendRetryDelay=%ld, StaggerSlotSize=%ld",
		prec_frequency, max_send_retries, max_send_retry_delay,
		stagger_slot_size);

	/* finish the plugin's initialization */

	slurmd_port=slurm_get_slurmd_port();
	jobsteps_active = list_create(NULL); 
	jobsteps_retiring = list_create(NULL); 

	return rc;
}


/*
 * The following routines are called by the slurmd/mgr process
 */

int slurmd_jobacct_jobstep_launched(slurmd_job_t *job)
{
	_jrec_t *jrec;
	int	rc=SLURM_SUCCESS;

	debug3("slurmd_jobacct_jobstep_launched() called");
	jrec = _alloc_jrec(job);
	jrec->nodeid = job->nodeid;
	debug2("jobacct(%d): in slurmd_jobacct_jobstep_launched with %d cpus,"
			" node0,1=%s,%s, this is node %d of %d",
			getpid(), job->cpus, jrec->node0, jrec->node1,
			job->nodeid, job->nnodes);
	rc = _send_data_to_mynode(LAUNCH, jrec);
	xfree(jrec);
	return rc;
}

int slurmd_jobacct_jobstep_terminated(slurmd_job_t *job)
{
	debug3("jobacct(%d): slurmd_jobacct_jobstep_terminated(%u.%u)",
			getpid(), job->jobid, job->stepid);
	return SLURM_SUCCESS;
}

/*
 * The following routines are called from the slurmd session manager process
 */

int slurmd_jobacct_smgr(void)
{
	if (prec_frequency == 0)	/* don't want dynamic monitoring? */
		return 0;
	if ( _get_process_data() ) {	/* If we can gather the data... */ 
		if  (pthread_create(&_watch_tasks_thread_id, NULL,
					&_watch_tasks, NULL)) {
			debug("jobacct failed to create _watch_tasks "
					"thread: %m");
			prec_frequency = 0;
		}
		else 
			debug3("jobacct LOG dynamic logging enabled");
	} else {
		prec_frequency = 0;
		debug2("jobacct LOG dynamic logging disabled"); 
	}
	return 0;
}

int slurmd_jobacct_task_exit(slurmd_job_t *job, pid_t pid, int status, struct rusage *rusage)
{
	_jrec_t *jrec;
	int	rc=SLURM_SUCCESS;

	debug2("slurmd_jobacct_task_exit for job %u.%u,"
			" node %d, status=%d",
			job->jobid, job->stepid, job->nodeid, status/256);
	jrec = _alloc_jrec(job);
	jrec->nodeid			= job->nodeid;
	memcpy(&jrec->rusage, rusage, sizeof(struct rusage));
	jrec->status		 	= status/256;
	if (prec_frequency) {	/* if dynamic monitoring */
		slurm_mutex_lock(&precTable_lock); /* let watcher finish loop */
		pthread_cancel(_watch_tasks_thread_id); 
		pthread_join(_watch_tasks_thread_id,NULL);
		slurm_mutex_unlock(&precTable_lock);
		jrec->max_psize			= max_psize;
		jrec->max_vsize			= max_vsize;
	}
	rc = _send_data_to_mynode(TASKDATA, jrec);
	xfree(jrec);
	return rc;
}

/*
 * Local utility routines
 */

/*
 * Aggregate the accounting data 
 *
 * Threads: jrec and inrec must be locked by caller, if appropriate.
 */

static void _aggregate_job_data(_jrec_t *jrec, _jrec_t *inrec)
{
	debug("jobacct(%d): entering _aggregate_job_data, inbound utime=%d.%06d",
			getpid(), inrec->rusage.ru_utime.tv_sec,
			inrec->rusage.ru_utime.tv_usec);
	jrec->rusage.ru_utime.tv_sec	+= inrec->rusage.ru_utime.tv_sec;
	jrec->rusage.ru_utime.tv_usec	+= inrec->rusage.ru_utime.tv_usec;
	while (jrec->rusage.ru_utime.tv_usec >= 1E6) {
		jrec->rusage.ru_utime.tv_sec++;
		jrec->rusage.ru_utime.tv_usec -= 1E6;
	}
	jrec->rusage.ru_stime.tv_sec	+= inrec->rusage.ru_stime.tv_sec;
	jrec->rusage.ru_stime.tv_usec	+= inrec->rusage.ru_stime.tv_usec;
	while (jrec->rusage.ru_stime.tv_usec >= 1E6) {
		jrec->rusage.ru_stime.tv_sec++;
		jrec->rusage.ru_stime.tv_usec -= 1E6;
	}
	jrec->rusage.ru_maxrss		+= inrec->rusage.ru_maxrss;
	jrec->rusage.ru_ixrss		+= inrec->rusage.ru_ixrss;
	jrec->rusage.ru_idrss		+= inrec->rusage.ru_idrss;
	jrec->rusage.ru_isrss		+= inrec->rusage.ru_isrss;
	jrec->rusage.ru_minflt		+= inrec->rusage.ru_minflt;
	jrec->rusage.ru_majflt		+= inrec->rusage.ru_majflt;
	jrec->rusage.ru_nswap		+= inrec->rusage.ru_nswap;
	jrec->rusage.ru_inblock		+= inrec->rusage.ru_inblock;
	jrec->rusage.ru_oublock		+= inrec->rusage.ru_oublock;
	jrec->rusage.ru_msgsnd		+= inrec->rusage.ru_msgsnd;
	jrec->rusage.ru_msgrcv		+= inrec->rusage.ru_msgrcv;
	jrec->rusage.ru_nsignals	+= inrec->rusage.ru_nsignals;
	jrec->rusage.ru_nvcsw		+= inrec->rusage.ru_nvcsw;
	jrec->rusage.ru_nivcsw		+= inrec->rusage.ru_nivcsw;
	if ( jrec->status == 0 )	/* Only take the first non-zero code */
		jrec->status		 = inrec->status;
	if ( jrec->max_psize < inrec->max_psize)
		jrec->max_psize		= inrec->max_psize;
	if ( jrec->max_vsize < inrec->max_vsize)
		jrec->max_vsize		= inrec->max_vsize;
	debug("jobacct(%d): leaving _aggregate_job_data, total utime=%d.%06d",
			getpid(), jrec->rusage.ru_utime.tv_sec,
			jrec->rusage.ru_utime.tv_usec);
	return;
}

/*
 * Allocate and initialize a jrec.
 */

static _jrec_t  *_alloc_jrec(slurmd_job_t *job)
{
	_jrec_t *jrec;

	jrec = xmalloc(sizeof(_jrec_t));
	jrec->jobid			= job->jobid;
	jrec->stepid			= job->stepid;
	jrec->nprocs			= job->nprocs;
	jrec->ntasks			= job->ntasks;
	jrec->nnodes			= job->nnodes;
	jrec->ncpus			= job->cpus;
	jrec->nodeid			= job->nodeid;
	jrec->start_time		= time(NULL);
	jrec->rusage.ru_utime.tv_sec	= 0;
	jrec->rusage.ru_utime.tv_usec	= 0;
	jrec->rusage.ru_stime.tv_sec	= 0;
	jrec->rusage.ru_stime.tv_usec	= 0;
	jrec->rusage.ru_maxrss		= 0;
	jrec->rusage.ru_ixrss		= 0;
	jrec->rusage.ru_idrss		= 0;
	jrec->rusage.ru_isrss		= 0;
	jrec->rusage.ru_minflt		= 0;
	jrec->rusage.ru_majflt		= 0;
	jrec->rusage.ru_nswap		= 0;
	jrec->rusage.ru_inblock		= 0;
	jrec->rusage.ru_oublock		= 0;
	jrec->rusage.ru_msgsnd		= 0;
	jrec->rusage.ru_msgrcv		= 0;
	jrec->rusage.ru_nsignals	= 0;
	jrec->rusage.ru_nvcsw		= 0;
	jrec->rusage.ru_nivcsw		= 0;
	jrec->status			= 0;
	jrec->max_vsize			= 0;
	jrec->max_psize			= 0; 
	jrec->not_reported		= job->nnodes;
	if (job->batch)	/* Account for the batch control pseudo-step */
		jrec->not_reported++;
	_get_node_01_names(jrec->node0, jrec->node1, job->env);
	return jrec;
}

/*
 * Select a jrec from the list by jobid and stepid.
 *
 * THREADS:	The caller must lock {jrecs}_lock, if necessary.
 */

static _jrec_t *_get_jrec_by_jobstep(List jrecs, uint32_t jobid,
		uint32_t stepid) {
	_jrec_t *jrec = NULL;
	ListIterator i;
	if (jrecs==NULL) {
		error("no accounting job list");
		return jrec;
	}
	i = list_iterator_create(jrecs);
	while ((jrec = list_next(i))) {
		if ( (jrec->jobid == jobid ) && (jrec->stepid == stepid)) {
			break;
		}
	}
	list_iterator_destroy(i);
	return jrec;
}

/*
 * Get the node name for the first node in the current allocation, then
 * get the name of the second node for redundancy.
 */

static void _get_node_01_names( char *node0, char *node1, char **env ) {
	hostlist_t	hl;
	char		*hname;
	int		i=0,
			slen;
	const char	*env_var = "SLURM_NODELIST=";
	slen = strlen(env_var);
	while (env && env[i+1] && *(hname=env[i++])) {
		if (strncmp(env_var, hname, slen)==0) { 
			hname += slen;
			hl = hostlist_create(hname);
			hname = hostlist_shift(hl);
			strncpy(node0, hname, HOST_NAME_MAX);
			hname = hostlist_shift(hl);
			if (hname)
				strncpy(node1, hname, HOST_NAME_MAX);
			hostlist_destroy(hl);
			goto done;
		}
		/* Either user cleared SLURM_NODELIST or 
		 * never set (as is case with POE on AIX) */
		strncpy(node0, NOT_FOUND, HOST_NAME_MAX);
	}
  done:
	debug2("jobacct(%d): node0 is \"%s\"\n", getpid(), node0);
	return;
}

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
static void
_get_offspring_data(_prec_t *ancestor, pid_t pid) {
	int	i;

	for (i=0; i<nPrecs; i++) {
		if (precTable[i].ppid == pid) {
			_get_offspring_data(ancestor, precTable[i].pid);
			ancestor->psize += precTable[i].psize;
			ancestor->vsize += precTable[i].vsize;
		}
	}
	return;
}

/*
 * _get_process_data() - Build a table of all current processes
 *
 * IN:	nothing.
 *
 * OUT:	none
 *
 * SIDE EFFECTS: nPrecs	= The number of entries in precTable[]
 *
 * RETVAL:	pointer to precTable[]
 *
 * THREADSAFE! Only one thread ever gets here.
 *
 * Assumption:
 *    Any file with a name of the form "/proc/[0-9]+/stat"
 *    is a Linux-style stat entry. We disregard the data if they look
 *    wrong.
 */
static _prec_t *_get_process_data(void) {
	static	int	precTableSize = 0;
	static	DIR	*SlashProc;		/* For /proc */ 
	static	int	SlashProcOpen = 0;

	struct		dirent *SlashProcEntry;
	FILE		*statFile;
	char		*iptr, *optr;
	char		statFileName[256];	/* Allow ~20x extra length */
	
	int		i, my_pid;
	long		psize, vsize;

	if (SlashProcOpen) {
		rewinddir(SlashProc);
	} else {
		SlashProc=opendir("/proc");
		if (SlashProc == NULL) {
			perror("opening /proc");
			return NULL;
		}
		SlashProcOpen=1;
	}
	strcpy(statFileName, "/proc/");
	nPrecs = 0;
	while ((SlashProcEntry=readdir(SlashProc))) {
		if (nPrecs >= precTableSize) 	/* then add another 100 */
			precTable = (_prec_t *)realloc(precTable,
					(precTableSize+=100)*sizeof(_prec_t));
		/* Save a few cyles by simulating
			strcat(statFileName, SlashProcEntry->d_name);
			strcat(statFileName, "/stat");
		   while checking for a numeric filename (which really
		   should be a pid).
		*/
		optr = statFileName+sizeof("/proc");
		iptr = SlashProcEntry->d_name;
		do {
			if (*iptr < '0')
				goto NextSlashProcEntry;
			if ( (*optr++ = *iptr++)>'9')
				goto NextSlashProcEntry;
		} while (*iptr);
		iptr = (char*)"/stat";
		do { *optr++ = *iptr++; } while (*iptr);
		*optr = 0;
		if ((statFile=fopen(statFileName,"r"))==NULL)
			continue;	/* Assume the process went away */

		if (_get_process_data_line(statFile, &precTable[nPrecs]))
			nPrecs++;	/* If valid data, preserve it */
		fclose(statFile);
NextSlashProcEntry: ;
	}
	if (nPrecs == 0)
		return NULL;	/* We have no business being here! */

	my_pid = getpid();	/* Tally the data for my children's children */
	psize = 0;
	vsize = 0;
	for (i=0; i<nPrecs; i++) {
		if (precTable[i].ppid == my_pid) {
				/* find all my descendents */
			_get_offspring_data(&precTable[i],
				precTable[i].pid);
				/* tally their memory usage */
			psize += precTable[i].psize;
			vsize += precTable[i].vsize;
			if (vsize==0)
				vsize=1; /* Flag to let us know we found it,
					    though it is already finished */
		}
	}
	if (max_psize < psize)
		max_psize = psize;
	if (max_vsize < vsize)
		max_vsize = vsize;

	return precTable;
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
static int _get_process_data_line(FILE *in, _prec_t *prec) {
	/* discardable data */
	int		d;
	char		c;
	char		*s;
	long int	ld;
	unsigned long	lu;
	int max_path_len = pathconf("/", _PC_NAME_MAX);

	/* useful datum */
	int		nvals;

	s = xmalloc(max_path_len + 1);
	nvals=fscanf(in,
		"%d %s %c %d %d "
		"%d %d %d %lu %lu "
		"%lu %lu %lu %lu %lu "
		"%ld %ld %ld %ld %ld "
		"%ld %lu %lu %ld %lu", 
		&prec->pid, s, &c, &prec->ppid, &d,
		&d, &d, &d, &lu, &lu,
		&lu, &lu, &lu, &lu, &lu,
		&ld, &ld, &ld, &ld, &ld,
		&ld, &lu, &prec->vsize, &prec->psize, &lu );
	/* The fields in the record are
	 *	pid, command, state, ppid, pgrp,
	 *	session, tty_nr, tpgid, flags, minflt,
	 *	cminflt, majflt, cmajflt, utime, stime,
	 *	cutime, cstime, priority, nice, lit_0,
	 *	itrealvalue, starttime, vsize, rss, rlim
	 */
	xfree(s);
	if (nvals != 25)	/* Is it what we expected? */
		return 0;	/* No! */
	prec->psize *= getpagesize();	/* convert pages to bytes */
	prec->psize /= 1024;		/* now convert psize to kibibytes */
	prec->vsize /= 1024;		/* and convert vsize to kibibytes */
	return 1;
}

/*
 * Simply duplicate a jobrecord's fields in network format
 *
 * Threads: Both inrec and outrec must be locked by the caller, if appropriate
 */ 
static int _pack_jobrec(_jrec_t *outrec, _jrec_t *inrec) {
	outrec->jobid = htonl(inrec->jobid);
	outrec->stepid = htonl(inrec->stepid);
	outrec->nodeid = htonl(inrec->nodeid);
	outrec->nnodes = htonl(inrec->nnodes);
	outrec->nprocs = htonl(inrec->nprocs);
	outrec->ncpus = htonl(inrec->ncpus);
	outrec->start_time = (time_t)htonl(inrec->start_time);
	outrec->rusage.ru_utime.tv_sec = htonl(inrec->rusage.ru_utime.tv_sec);
	outrec->rusage.ru_utime.tv_usec = htonl(inrec->rusage.ru_utime.tv_usec);
	outrec->rusage.ru_stime.tv_sec = htonl(inrec->rusage.ru_stime.tv_sec);
	outrec->rusage.ru_stime.tv_usec = htonl(inrec->rusage.ru_stime.tv_usec);
	outrec->rusage.ru_maxrss = htonl(inrec->rusage.ru_maxrss);
	outrec->rusage.ru_ixrss = htonl(inrec->rusage.ru_ixrss);
	outrec->rusage.ru_idrss = htonl(inrec->rusage.ru_idrss);
	outrec->rusage.ru_isrss = htonl(inrec->rusage.ru_isrss);
	outrec->rusage.ru_minflt = htonl(inrec->rusage.ru_minflt);
	outrec->rusage.ru_majflt = htonl(inrec->rusage.ru_majflt);
	outrec->rusage.ru_nswap = htonl(inrec->rusage.ru_nswap);
	outrec->rusage.ru_inblock = htonl(inrec->rusage.ru_inblock);
	outrec->rusage.ru_oublock = htonl(inrec->rusage.ru_oublock);
	outrec->rusage.ru_msgsnd = htonl(inrec->rusage.ru_msgsnd);
	outrec->rusage.ru_msgrcv = htonl(inrec->rusage.ru_msgrcv);
	outrec->rusage.ru_nsignals = htonl(inrec->rusage.ru_nsignals);
	outrec->rusage.ru_nvcsw = htonl(inrec->rusage.ru_nvcsw);
	outrec->rusage.ru_nivcsw = htonl(inrec->rusage.ru_nivcsw);
	outrec->status = htonl(inrec->status);
	outrec->max_vsize = htonl(inrec->max_vsize);
	outrec->max_psize = htonl(inrec->max_psize); 
	outrec->not_reported = htonl(inrec->not_reported);
	strcpy(outrec->node0, inrec->node0);
	strcpy(outrec->node1, inrec->node1);
	return SLURM_SUCCESS;
}

static void _process_mynode_msg(_mynode_msg_t *msg) {
	debug2("jobacct(%d): in process_mynode_msg(msg=%d) for job %u.%u",
			getpid(), msg->msgtype, msg->jrec.jobid,
			msg->jrec.stepid);
	switch (msg->msgtype) {
	  case LAUNCH:
		  _process_mynode_msg_launch( (_jrec_t *) &msg->jrec);
		  break;
	  case TASKDATA:
		  _process_mynode_msg_taskdata( (_jrec_t *) &msg->jrec);
		  break;
	  default:
		  error("jobacct(%d): invalid mynode msgtype: %d", 
				  getpid(), msg->msgtype);
		  break;
	}
	return;
}

/*
 * Job step launched, so set up a jrec for it.
 */

static void _process_mynode_msg_launch(_jrec_t *inrec) { 
	_jrec_t	*jrec;

	debug2("jobacct(%d): in _process_mynode_msg_launch", getpid());
	slurm_mutex_lock(&jobsteps_active_lock);
	/* Have we seen this one before? */
	if (_get_jrec_by_jobstep(jobsteps_active, inrec->jobid,
				inrec->stepid)) {
		slurm_mutex_unlock(&jobsteps_active_lock);
		error("jobacct(%d): dup launch record for %u.%u",
				getpid(), inrec->jobid, inrec->stepid);
		return;
	}
	jrec = xmalloc(sizeof(_jrec_t));
	memcpy(jrec, inrec, sizeof(_jrec_t));
	list_append(jobsteps_active, jrec);
	if (jrec->nodeid==0)	/* Notify the logger that a step has started */
		_send_data_to_slurmctld(jrec, 0);
	slurm_mutex_unlock(&jobsteps_active_lock);
}

/*
 * Capture and aggregate task data from slurmd/smgr.
 */

static void _process_mynode_msg_taskdata(_jrec_t *inrec){
	_jrec_t		*jrec;

	debug2("jobacct(%d): in _process_mynode_msg_taskdata for job %u.%u"
			" ntasks=%d",
			getpid(), inrec->jobid, inrec->stepid, inrec->ntasks); 
	slurm_mutex_lock(&jobsteps_active_lock);
	jrec = _get_jrec_by_jobstep(jobsteps_active, inrec->jobid,
			inrec->stepid);
	if ( jrec == NULL ) {
		slurm_mutex_unlock(&jobsteps_active_lock);
		error("jobacct(%d): task data but no record for %u.%u,"
				" discarding data",
				getpid(), inrec->jobid, inrec->stepid);
		return;
	}
	_aggregate_job_data(jrec, inrec);
	if (--jrec->ntasks == 0)	/* All tasks have reported */
		_send_data_to_node_0(jrec);
	slurm_mutex_unlock(&jobsteps_active_lock);
}

/* Aggregate the final data from each node
 *
 * Input: inrec - jrec in host order
 */
static void _process_node0_data(_jrec_t *inrec) {
	_jrec_t		*jrec;

	slurm_mutex_lock(&jobsteps_retiring_lock);
	jrec = _get_jrec_by_jobstep(jobsteps_retiring, inrec->jobid,
			inrec->stepid);
	if (jrec == NULL) {
		jrec = xmalloc(sizeof(_jrec_t));
		memcpy(jrec, inrec, sizeof(_jrec_t));
		list_append(jobsteps_retiring, jrec);
	} else {
		_aggregate_job_data(jrec, inrec);
		jrec->nnodes += inrec->nnodes;
		jrec->ncpus  += inrec->ncpus;
	}
	if (--jrec->not_reported < 0)
		error("jobacct(%d): invalid, not_reported=%d",
				getpid(), jrec->not_reported);
	debug2("jobacct(%d): not_reported=%d after node0 message, "
			"cum. utime=%d.%06d",
			getpid(), jrec->not_reported,
			jrec->rusage.ru_utime.tv_sec,
			jrec->rusage.ru_utime.tv_usec );
	if (jrec->not_reported <= 0) {
		_send_data_to_slurmctld(jrec, 1);
		_remove_jrec_from_list(jobsteps_retiring, jrec->jobid,
				jrec->stepid);
		slurm_mutex_unlock(&jobsteps_retiring_lock);
		xfree(jrec); 
	} else {
		slurm_mutex_unlock(&jobsteps_retiring_lock);
	}
}

/*
 * Process the data sent to node0 for aggregation.
 *
 * Input: nrec - jrec in network order
 */

static void _process_node0_msg(_jrec_t *nrec) {
	_jrec_t		*hrec;	/* host-ordered copy of nrec */

	hrec = xmalloc(sizeof(_jrec_t));
	_unpack_jobrec(hrec, nrec);
	debug2("jobacct(%d): Received %u.%u node0 message, "
			"nodeid=%d, utime=%d.%06d",
			getpid(), hrec->jobid, hrec->stepid,
			hrec->nodeid,
			hrec->rusage.ru_utime.tv_sec,
			hrec->rusage.ru_utime.tv_usec);
	_process_node0_data(hrec);
	xfree(hrec);
}

/*
 * Remove a jobstep record from the list.
 */
static void _remove_jrec_from_list(List jrecs, uint32_t jobid, uint32_t stepid)
{
	_jrec_t		*jrec;

	ListIterator i = list_iterator_create(jrecs);
	while ((jrec = list_next(i))) {
		if ( (jrec->jobid == jobid )
				&& (jrec->stepid == stepid)) {
			debug2("jobacct(%d): in _remove_jrec_from_list, "
				"found %u.%u record",
				getpid(), jobid, stepid); 
			break;
		}
	}
	list_remove(i);
	return;
}

/*
 * Send data from the slurmd/mgr or slurmd/smgr process to the
 * parent slurmd process. Since we're on the same node, we will
 * assume that the parent's byte order is the same as ours.
 *
 * Threads: jrec must be locked by the caller.
 */

static int _send_data_to_mynode(_mynode_msg_type_t msgtype, _jrec_t *jrec) {
	slurm_msg_t	*msg,
			*retmsg;
	jobacct_msg_t	jmsg;
	_mynode_msg_t	*node_msg;
	_stats_msg_t	stats;
	int		rc=SLURM_SUCCESS,
			retry;

	debug2("jobacct(%d): in _send_data_to_mynode(msgtype %d, job %u)",
			getpid(), msgtype, jrec->jobid);
	stats.msg_type = htonl(TO_MYNODE);
	stats.jobid    = htonl(jrec->jobid);
	stats.stepid   = htonl(jrec->stepid);
	stats.datalen  = sizeof(_mynode_msg_t);
	xassert(sizeof(_mynode_msg_t)<MAX_MSG_SIZE);
	/* make a mynode_msg */
	node_msg = (_mynode_msg_t *) &stats.data;
	node_msg->msgtype = msgtype;
	memcpy(&node_msg->jrec, jrec, sizeof(_jrec_t));	/* For same node, we
							   can assume the
							   same byte order */
	/* now setup the jobacct msg */
	jmsg.len = sizeof(_mynode_msg_t);
	jmsg.data = (char*)&stats;
	/* finally, set up the slurm_msg */
	msg = xmalloc(sizeof(slurm_msg_t));
	retmsg = xmalloc(sizeof(slurm_msg_t));
	slurm_set_addr(&msg->address, slurmd_port, "localhost");
	msg->msg_type  = MESSAGE_JOBACCT_DATA;
	msg->data      = &jmsg;
	msg->data_size = sizeof(jobacct_msg_t);
	debug2("jobacct(%d): attempting send_recv_node_msg(msg, %d, localhost)"
			" for job %u.%u",
			getpid(), slurmd_port, jrec->jobid, jrec->stepid);
	for (retry=0; retry<max_send_retries; retry++) {
		if (jrec->nnodes)
			_stagger_time(-1, jrec->nprocs/jrec->nnodes);
			/* avoid simultaneous msgs from all processes */
		if (( rc = slurm_send_recv_node_msg(msg, retmsg, 0)) >=0 )
				break;
		if (retry==0)
			srand( (getpid()*times(NULL))%RAND_MAX );
		sleep(1+(rand()/(RAND_MAX/max_send_retry_delay)));
	}
	if (rc<0)
		error("jobacct(%d): _send_data_to_mynode(msg, %d, localhost)"
				" says %d (%m) after %d tries",
				getpid(), slurmd_port, rc, retry);
	else {
		slurm_free_cred(retmsg->cred);
		debug2("jobacct(%d): _send_data_to_mynode(msg, %d, localhost) "
				"succeeded", getpid(), slurmd_port);
	}
	xfree(msg);
	xfree(retmsg);
	return rc;
}

/*
 * Send data to the first node in the allocation for aggregation.
 *
 * Threads: jrec must be locked by the caller.
 */

static int _send_data_to_node_0(_jrec_t *jrec) {
	slurm_msg_t	*msg,
			*retmsg;
	jobacct_msg_t	jmsg;
	_stats_msg_t	stats;
	int		rc=SLURM_SUCCESS,
			retry;

	if (!strcmp(jrec->node0, NOT_FOUND)) {
		error("jobacct(%d): job %d has no node0");
		return SLURM_SUCCESS;	/* can't do anything here */
	}

	debug2("jobacct(%d): in _send_data_to_node_0(job %u), nodes0,1=%s,%s"
			", utime=%d.%06d",
			getpid(), jrec->jobid, jrec->node0, jrec->node1,
			jrec->rusage.ru_utime.tv_sec,
			jrec->rusage.ru_utime.tv_usec);

	if (jrec->nodeid==0) { /* don't need to send it to ourselves */
		_process_node0_data(jrec);
		return rc;
	}

	/* make a stats_msg */
	stats.msg_type = htonl(TO_NODE0);
	stats.jobid    = htonl(jrec->jobid);
	stats.stepid   = htonl(jrec->stepid);
	stats.datalen  = sizeof(_jrec_t);
	xassert(sizeof(_jrec_t)<MAX_MSG_SIZE);
	_pack_jobrec((_jrec_t *)stats.data, jrec);
	/* now setup the jobacct msg */
	jmsg.len = sizeof(_stats_msg_t);
	jmsg.data = (char*)&stats;
	/* finally, set up the slurm_msg */
	msg = xmalloc(sizeof(slurm_msg_t));
	retmsg = xmalloc(sizeof(slurm_msg_t));
	slurm_set_addr(&msg->address, slurmd_port, jrec->node0);
	msg->msg_type  = MESSAGE_JOBACCT_DATA;
	msg->data      = &jmsg;
	msg->data_size = sizeof(jobacct_msg_t);
	debug2("jobacct(%d): attempting send_recv_node_msg(msg, %d, %s)",
			getpid(), slurmd_port, jrec->node0);
	for (retry=0; retry<max_send_retries; retry++) {
		_stagger_time(jrec->nodeid, jrec->nnodes);
			/* avoid simultaneous msgs from all processes */
		if ((rc = slurm_send_recv_node_msg(msg, retmsg, 0))>=0 )
			break;
		if (retry==0)
			srand( (getpid()*times(NULL))%RAND_MAX );
		sleep(1+(rand()/(RAND_MAX/max_send_retry_delay)));
	}
	if (rc<0)
		error("jobacct(%d): _send_data_to_node_0(msg, %d, %s)"
				" says %d (%m) after %d tries",
				getpid(), slurmd_port, jrec->node0, rc, retry);
	else {
		slurm_free_cred(retmsg->cred);
		debug2("jobacct(%d): _send_data_to_node_0(msg, %d, %s)"
				" succeeded",
			getpid(), slurmd_port, jrec->node0);
	}
	xfree(msg);
	xfree(retmsg);
	return rc;
}

/*
 * Send data to slurmctld to be logged.
 *
 * IN:	*jrec -- the jobstep data to be transmitted
 * 	done  == 0 if we're creating the jobstep
 * 	      <> 0 if this is the final data
 *
 * Threads: jrec must be locked by the caller.
 */

static int _send_data_to_slurmctld(_jrec_t *jrec, int done) {
#define DATETIME_SIZE 16
	char		*comp_status,
			*tbuf;
	int		nchars,
			rc=SLURM_SUCCESS;
	long		elapsed;
	_stats_msg_t	*stats;
	struct tm 	ts; /* timestamp decoder */
	time_t		now;

	debug2("jobacct(%d): in _send_data_to_slurmctld(msg,%d)",
			getpid(), done);
	if (done)
		if (jrec->status)
			comp_status = "F";
		else
			comp_status = "CD";
	else
		comp_status = "R";
	tbuf = xmalloc(DATETIME_SIZE);
	now = time(NULL);
	gmtime_r(&now, &ts);
	nchars = snprintf(tbuf, DATETIME_SIZE, "%04d%02d%02d%02d%02d%02d",
		1900+(ts.tm_year), 1+(ts.tm_mon), ts.tm_mday,
		ts.tm_hour, ts.tm_min, ts.tm_sec);
	xassert(nchars < DATETIME_SIZE);	/* Should never happen... */

	stats = xmalloc(sizeof(_stats_msg_t));
	stats->msg_type  = htonl(TO_CONTROLLER);
	stats->jobid     = htonl(jrec->jobid);
	stats->stepid    = htonl(jrec->stepid);
	if ((elapsed=time(NULL)-jrec->start_time)<0)
		elapsed=0;	/* For *very* short jobs, if clock is wrong */

	nchars = snprintf(stats->data, MAX_MSG_SIZE, _jobstep_format,
		1,				/* RECORD_VERSION */
		38,				/* NUM_FIELDS */
		jrec->stepid,			/* stepid */
		tbuf,				/* completion time */
		comp_status,			/* completion status */
		jrec->status,		/* completion code */
		jrec->nprocs,			/* number of processes */
		jrec->ncpus,			/* number of cpus */
		elapsed,			/* elapsed seconds */
		jrec->rusage.ru_utime.tv_sec	/* total cputime seconds */
		 + jrec->rusage.ru_stime.tv_sec,
		jrec->rusage.ru_utime.tv_usec	/* total cputime usecs */
		 + jrec->rusage.ru_stime.tv_usec,
		jrec->rusage.ru_utime.tv_sec,	/* user seconds */
		jrec->rusage.ru_utime.tv_usec,	/* user microseconds */
		jrec->rusage.ru_stime.tv_sec,	/* system seconds */
		jrec->rusage.ru_stime.tv_usec,	/* system microseconds */
		jrec->rusage.ru_maxrss,		/* max rss */
		jrec->rusage.ru_ixrss,		/* max ixrss */
		jrec->rusage.ru_idrss,		/* max idrss */
		jrec->rusage.ru_isrss,		/* max isrss */
		jrec->rusage.ru_minflt,		/* max minflt */
		jrec->rusage.ru_majflt,		/* max majflt */
		jrec->rusage.ru_nswap,		/* max nswap */
		jrec->rusage.ru_inblock,	/* total inblock */
		jrec->rusage.ru_oublock,	/* total outblock */
		jrec->rusage.ru_msgsnd,		/* total msgsnd */
		jrec->rusage.ru_msgrcv,		/* total msgrcv */
		jrec->rusage.ru_nsignals,	/* total nsignals */
		jrec->rusage.ru_nvcsw,		/* total nvcsw */
		jrec->rusage.ru_nivcsw,		/* total nivcsw */
		jrec->max_vsize,		/* max vsize */
		jrec->max_psize);		/* max psize */
	if (nchars >= MAX_MSG_SIZE) {
		error("_send_data_to_slurmctld buffer overflow");
		rc = SLURM_ERROR;
	} else {
		stats->datalen = htonl(nchars+1);
		rc =  _send_msg_to_slurmctld(stats);
	}
	xfree(stats);
	xfree(tbuf);
	return rc;
}

/*
 * Pause briefly to avoid flooding the receiver with simultaneous
 * messages.
 *
 * IN:	nodeid		- the relative position of the node in the
 * 			  allocation or -1, if sending to myself.
 * 	n_contenders	- the number of other senders who might also
 * 			  trying to send a message at the same time.
 *
 *  Allocate n_contenders time slots of stagger_slot_size*.001 seconds,
 *  and pause until our time slot has been reached.
 */

static void _stagger_time(long nodeid, long n_contenders) {
	long long	sleep_time;
	struct timespec	req;

	if (stagger_slot_size==0)
		return;		/* Nothing for us to do here. */

	debug3("jobacct: in _stagger_time(%ld, %ld)", nodeid, n_contenders);
	if (n_contenders<10)	/* There should be no cause for concern */
		return;

	if (nodeid<0) {	/* Randomly select a time slot */
		srand( (getpid()*times(NULL))%RAND_MAX );
		nodeid = rand()/(RAND_MAX/n_contenders);
	}
	sleep_time = (nodeid*1e6)*stagger_slot_size; /* lots and lots of
							nanoseconds */
	req.tv_sec = sleep_time / (long long)1e9;
	req.tv_nsec = sleep_time % (long long)1e9;
	debug3("jobacct(%d): will sleep %ld.%09ld seconds "
			"in _stagger_time()",
			getpid(), req.tv_sec, req.tv_nsec);
	nanosleep(&req, NULL);
	return;
}

/*
 *  Translate a jobrecord's fields from network format to host format
 *
 * Threads: Both inrec and outrec must be locked by the caller.
 */ 

static int _unpack_jobrec(_jrec_t *outrec, _jrec_t *inrec) {
	outrec->jobid = ntohl(inrec->jobid);
	outrec->stepid = ntohl(inrec->stepid);
	outrec->nnodes = ntohl(inrec->nnodes);
	outrec->nprocs = ntohl(inrec->nprocs);
	outrec->ncpus = ntohl(inrec->ncpus);
	outrec->nodeid = ntohl(inrec->nodeid);
	outrec->start_time = (time_t)ntohl(inrec->start_time);
	outrec->rusage.ru_utime.tv_sec = ntohl(inrec->rusage.ru_utime.tv_sec);
	outrec->rusage.ru_utime.tv_usec = ntohl(inrec->rusage.ru_utime.tv_usec);
	outrec->rusage.ru_stime.tv_sec = ntohl(inrec->rusage.ru_stime.tv_sec);
	outrec->rusage.ru_stime.tv_usec = ntohl(inrec->rusage.ru_stime.tv_usec);
	outrec->rusage.ru_maxrss = ntohl(inrec->rusage.ru_maxrss);
	outrec->rusage.ru_ixrss = ntohl(inrec->rusage.ru_ixrss);
	outrec->rusage.ru_idrss = ntohl(inrec->rusage.ru_idrss);
	outrec->rusage.ru_isrss = ntohl(inrec->rusage.ru_isrss);
	outrec->rusage.ru_minflt = ntohl(inrec->rusage.ru_minflt);
	outrec->rusage.ru_majflt = ntohl(inrec->rusage.ru_majflt);
	outrec->rusage.ru_nswap = ntohl(inrec->rusage.ru_nswap);
	outrec->rusage.ru_inblock = ntohl(inrec->rusage.ru_inblock);
	outrec->rusage.ru_oublock = ntohl(inrec->rusage.ru_oublock);
	outrec->rusage.ru_msgsnd = ntohl(inrec->rusage.ru_msgsnd);
	outrec->rusage.ru_msgrcv = ntohl(inrec->rusage.ru_msgrcv);
	outrec->rusage.ru_nsignals = ntohl(inrec->rusage.ru_nsignals);
	outrec->rusage.ru_nvcsw = ntohl(inrec->rusage.ru_nvcsw);
	outrec->rusage.ru_nivcsw = ntohl(inrec->rusage.ru_nivcsw);
	outrec->status = ntohl(inrec->status);
	outrec->max_vsize = ntohl(inrec->max_vsize);
	outrec->max_psize = ntohl(inrec->max_psize); 
	outrec->not_reported = ntohl(inrec->not_reported);
	strcpy(outrec->node0, inrec->node0);
	strcpy(outrec->node1, inrec->node1);
	return SLURM_SUCCESS;
}

/* _watch_tasks() -- monitor slurm jobs and track their memory usage
 *
 * IN, OUT:	Irrelevant; this is invoked by pthread_create()
 */

static void *_watch_tasks(void *arg) {

	int	tmp;

	while(1) {	/* Do this until slurm_jobacct_task_exit() stops us */
		sleep(prec_frequency);
		pthread_testcancel();
		slurm_mutex_lock(&precTable_lock);
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &tmp);
		_get_process_data();	/* Update the data */ 
		slurm_mutex_unlock(&precTable_lock);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &tmp);
	} 
}
