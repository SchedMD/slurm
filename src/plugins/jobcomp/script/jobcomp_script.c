/*****************************************************************************\
 *  jobcomp_script.c - Script running slurm job completion logging plugin.
 *  $Id$
 *****************************************************************************
 *  Produced at Center for High Performance Computing, North Dakota State
 *  University
 *  Written by Nathan Huff <nhuff@acm.org>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/list.h"
#include "src/slurmctld/slurmctld.h"
#include "job_record.h"

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
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the job completion logging API 
 * matures.
 */
const char plugin_name[]       	= "Job completion logging script plugin";
const char plugin_type[]       	= "jobcomp/script";
const uint32_t plugin_version	= 90;

static int plugin_errno = SLURM_SUCCESS;
static char * script = NULL;
static char error_str[256];
static List comp_list = NULL;

static pthread_t script_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t comp_list_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t comp_list_cond = PTHREAD_COND_INITIALIZER;
static int agent_exit = 0;

/*
 * Check if the script exists and if we can execute it.
 */
static int 
_check_script_permissions(char * location)
{
	struct stat filestat;
	uid_t user;
	gid_t group;

	if(stat(location,&filestat) < 0) {
		plugin_errno = errno;
		return error("jobcomp/script script does not exist");
	}

	if(!(filestat.st_mode&S_IFREG)) {
		plugin_errno = EACCES;
		return error("jobcomp/script script isn't a regular file");
	}
	
	user = geteuid();
	group = getegid();
	if(user == filestat.st_uid) {
		if (filestat.st_mode&S_IXUSR) {
			return SLURM_SUCCESS;
		} else {
			plugin_errno = EACCES;
			return error("jobcomp/script script is not executable");
		}
	} else if(group == filestat.st_gid) {
		if (filestat.st_mode&S_IXGRP) {
			return SLURM_SUCCESS;
		} else {
			plugin_errno = EACCES;
			return error("jobcomp/script script is not executable");
		}
	} else if(filestat.st_mode&S_IXOTH) {
		return SLURM_SUCCESS;
	}

	plugin_errno = EACCES;
	return error("jobcomp/script script is not executable");
}


/* Create a new environment pointer containing information from 
 * slurm_jobcomp_log_record so that the script can access it.
 */
static char ** _create_environment(char *job, char *user, char *job_name,
	char *job_state, char *partition, char *limit, char* start, char * end,
	char * submit, char * batch, char *node_list)
{
	int len = 0;
	char ** envptr;
	char *ptr;

	len += strlen(job)+7;
	len += strlen(user)+5;
	len += strlen(job_name)+9;
	len += strlen(job_state)+10;
	len += strlen(partition)+11;
	len += strlen(limit)+7;
	len += strlen(start)+7;
	len += strlen(end)+5;
	len += strlen(node_list)+7;
        len += strlen(submit)+8;
        len += strlen(batch)+7;
	/* Add new entries here as need and increase ENV_COUNT */
#define ENV_COUNT 13
#ifdef _PATH_STDPATH
	len += strlen(_PATH_STDPATH)+6;
#endif
	len += (ENV_COUNT * sizeof(char *));
	len += 16;	/* some extra, just to be safe */

	if(!(envptr = (char **)try_xmalloc(len)))
		return NULL;

	ptr = (char *)envptr + (ENV_COUNT * sizeof(char *));

	envptr[0] = ptr;
	memcpy(ptr,"JOBID=",6);
	ptr += 6;
	memcpy(ptr,job,strlen(job)+1);
	ptr += strlen(job)+1;
	
	envptr[1] = ptr;
	memcpy(ptr,"UID=",4);
	ptr += 4;
	memcpy(ptr,user,strlen(user)+1);
	ptr += strlen(user)+1;
	
	envptr[2] = ptr;
	memcpy(ptr,"JOBNAME=",8);
	ptr += 8;
	memcpy(ptr,job_name,strlen(job_name)+1);
	ptr += strlen(job_name)+1;
	
	envptr[3] = ptr;
	memcpy(ptr,"JOBSTATE=",9);
	ptr += 9;
	memcpy(ptr,job_state,strlen(job_state)+1);
	ptr += strlen(job_state)+1;

	envptr[4] = ptr;
	memcpy(ptr,"PARTITION=",10);
	ptr += 10;
	memcpy(ptr,partition,strlen(partition)+1);
	ptr += strlen(partition)+1;

	envptr[5] = ptr;
	memcpy(ptr,"LIMIT=",6);
	ptr += 6;
	memcpy(ptr,limit,strlen(limit)+1);
	ptr += strlen(limit)+1;

	envptr[6] = ptr;
	memcpy(ptr,"START=",6);
	ptr += 6;
	memcpy(ptr,start,strlen(start)+1);
	ptr += strlen(start)+1;

	envptr[7] = ptr;
	memcpy(ptr,"END=",4);
	ptr += 4;
	memcpy(ptr,end,strlen(end)+1);
	ptr += strlen(end)+1;

	envptr[8] = ptr;
	memcpy(ptr,"NODES=",6);
	ptr += 6;
	memcpy(ptr,node_list,strlen(node_list)+1);
	ptr += strlen(node_list)+1;

	envptr[9] = ptr;
	memcpy(ptr,"SUBMIT=",7);
	ptr += 7;
	memcpy(ptr,submit,strlen(submit)+1);
	ptr += strlen(submit)+1;

	envptr[10] = ptr;
	memcpy(ptr, "BATCH=",6);
	ptr += 6;
	memcpy(ptr,batch,strlen(batch)+1);
	ptr += strlen(batch)+1;

#ifdef _PATH_STDPATH
	envptr[11] = ptr;
	memcpy(ptr,"PATH=",5);
	ptr += 5;
	memcpy(ptr,_PATH_STDPATH,strlen(_PATH_STDPATH)+1);
	ptr += strlen(_PATH_STDPATH)+1;

	envptr[12] = NULL;
#else
	envptr[11] = NULL;
#endif
	
	return envptr;
}

/*
 * Thread function that executes a script
 */
void *script_agent (void *args) {
	pid_t pid = -1;
	char user_id_str[32],job_id_str[32], nodes_cache[1];
	char start_str[32], end_str[32], lim_str[32];
	char submit_str[32], *batch_str;
	char * argvp[] = {script, NULL};
	int status;
	char ** envp, * nodes;
	job_record job;

	while(1) {
		pthread_mutex_lock(&comp_list_mutex);
		while ((list_is_empty(comp_list) != 0) && (agent_exit == 0)) {
			pthread_cond_wait(&comp_list_cond, &comp_list_mutex);
		}
		if (agent_exit) {
			pthread_mutex_unlock(&comp_list_mutex);
			return NULL;
		}
		job = (job_record)list_pop(comp_list);
		pthread_mutex_unlock(&comp_list_mutex);

		snprintf(user_id_str,sizeof(user_id_str),"%u",job->user_id);
		snprintf(job_id_str,sizeof(job_id_str),"%u",job->job_id);
		snprintf(start_str, sizeof(start_str),"%lu",
			(unsigned long)job->start);
		snprintf(end_str, sizeof(end_str),"%lu",
			(unsigned long)job->end);
		snprintf(submit_str, sizeof(submit_str),"%lu",
			(unsigned long)job->submit);
		if (job->batch_flag)
			batch_str = "yes";
		else
			batch_str = "no"; 
		nodes_cache[0] = '\0';

		if (job->limit == INFINITE) {
			strcpy(lim_str, "UNLIMITED");
		} else {
			snprintf(lim_str, sizeof(lim_str), "%lu", 
				(unsigned long) job->limit);
		}
	
		if(job->node_list == NULL) {
			nodes = nodes_cache;
		} else {
			nodes = job->node_list;
		}

		/*Setup environment*/
		envp = _create_environment(job_id_str,user_id_str,
					job->job_name, job->job_state,
					job->partition, lim_str,
					start_str,end_str,submit_str,
					batch_str,nodes);

		if(envp == NULL) {
			plugin_errno = ENOMEM;
		}

		pid = fork();

		if (pid < 0) {
			xfree(envp);
			job_record_destroy((void *)job);
			plugin_errno = errno;
		} else if (pid == 0) {
			/*Change directory to tmp*/
			if (chdir(_PATH_TMP) != 0) {
				exit(errno);
			}

			/*Redirect stdin, stderr, and stdout to /dev/null*/
			if (freopen(_PATH_DEVNULL, "rb", stdin) == NULL) {
				exit(errno);
			}
			if (freopen(_PATH_DEVNULL, "wb", stdout) == NULL) {
				exit(errno);
			}
			if (freopen(_PATH_DEVNULL, "wb", stderr) == NULL) {
				exit(errno);
			}
		
			/*Exec Script*/
			execve(script,argvp,envp);
			exit(0);
		} else {
			xfree(envp);
			job_record_destroy((void *)job);
			waitpid(pid,&status,0);
			if(WIFEXITED(status) && WEXITSTATUS(status)) {
				plugin_errno = WEXITSTATUS(status);
			}
		}
	}
}

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	pthread_attr_t attr;

	verbose("jobcomp/script plugin loaded init");

	pthread_mutex_lock(&thread_flag_mutex);

	comp_list = list_create((ListDelF) job_record_destroy);
	if(comp_list == NULL) {
		return SLURM_ERROR;
	}

	if (script_thread) {
		debug2( "Script thread already running, not starting another");
		pthread_mutex_unlock(&thread_flag_mutex);
		return SLURM_ERROR;
	}
	slurm_attr_init(&attr);
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create(&script_thread, &attr, script_agent, NULL);
	
	pthread_mutex_unlock(&thread_flag_mutex);
	slurm_attr_destroy(&attr);

	return SLURM_SUCCESS;
}

/* Set the location of the script to run*/
int slurm_jobcomp_set_location ( char * location )
{
	if (location == NULL) {
		plugin_errno = EACCES;
		return error("jobcomp/script JobCompLoc needs to be set");
	}

	if (_check_script_permissions(location) != SLURM_SUCCESS) {
		return SLURM_ERROR;
	}

	xfree(script);
	script = xstrdup(location);
	
	return SLURM_SUCCESS;
}

int slurm_jobcomp_log_record ( struct job_record *job_ptr )
{
	job_record job;
	enum job_states job_state;
	time_t submit;

	debug3("Entering slurm_jobcomp_log_record");
	/* Job will typically be COMPLETING when this is called. 
	 * We remove this flag to get the eventual completion state:
	 * JOB_FAILED, JOB_TIMEOUT, etc. */
	job_state = job_ptr->job_state & (~JOB_COMPLETING);

	if (job_ptr->details)
		submit = job_ptr->details->submit_time;
	else
		submit = job_ptr->start_time;

	job = job_record_create(job_ptr->job_id, job_ptr->user_id, job_ptr->name,
				job_state_string(job_state), 
				job_ptr->partition, job_ptr->time_limit,
				job_ptr->start_time, job_ptr->end_time,
				submit, job_ptr->batch_flag, job_ptr->nodes);
	pthread_mutex_lock(&comp_list_mutex);
	list_append(comp_list,(void *)job);
	pthread_mutex_unlock(&comp_list_mutex);
	pthread_cond_broadcast(&comp_list_cond);
	return SLURM_SUCCESS;
}

/* Return the error code of the plugin*/
int slurm_jobcomp_get_errno( void )
{
	return plugin_errno;
}

/* Return a string representation of the error */
char *slurm_jobcomp_strerror( int errnum )
{
	strerror_r(errnum,error_str,sizeof(error_str));
	return error_str;
}

static int _wait_for_thread (pthread_t thread_id)
{
	int i;

	for (i=0; i<4; i++) {
		if (pthread_kill(thread_id, 0))
			return SLURM_SUCCESS;
		usleep(1000);
	}
	error("Could not kill jobcomp script pthread");
	return SLURM_ERROR;
}

/* Called when script unloads */
int fini ( void )
{
	int rc = SLURM_SUCCESS;

	pthread_mutex_lock(&thread_flag_mutex);
	if (script_thread) {
		verbose("Script Job Completion plugin shutting down");
		agent_exit = 1;
		pthread_cond_broadcast(&comp_list_cond);
		rc = _wait_for_thread(script_thread);
		script_thread = 0;
	}
	pthread_mutex_unlock(&thread_flag_mutex);

	xfree(script);
	if (rc == SLURM_SUCCESS) {
		pthread_mutex_lock(&comp_list_mutex);
		list_destroy(comp_list);
		pthread_mutex_unlock(&comp_list_mutex);
	}

	return rc;
}
