/*****************************************************************************\
 *  jobcomp_script.c - Script running slurm job completion logging plugin.
 *****************************************************************************
 *  Produced at Center for High Performance Computing, North Dakota State 
 *  University
 *  Written by Nathan Huff <nhuff@geekshanty.com>
 *  UCRL-CODE-2002-040.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif
#else
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>
#include <sys/wait.h>
#include <unistd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_jobcomp.h"
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

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	return SLURM_SUCCESS;
}


/* Set the location of the script to run*/
int slurm_jobcomp_set_location ( char * location )
{
	if (location == NULL) {
		plugin_errno = EACCES;
		return SLURM_ERROR;
	}
	xfree(script);
	script = xstrdup(location);
	return SLURM_SUCCESS;
}

/* Create a new environment pointer containing information from 
 * slurm_jobcomp_log_record so that the script can access it.
 */
static char ** _create_environment(char *job, char *user, char *job_name,
	char *job_state, char *partition, char *limit, char* start, char * end,
	char *node_list)
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
#ifdef	_PATH_STDPATH
	len += strlen(_PATH_STDPATH)+6;
#endif
	len += (11*sizeof(char *));

	if(!(envptr = (char **)try_xmalloc(len))) return NULL;

	ptr = (char *)envptr + (11*sizeof(char *));

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

#ifdef	_PATH_STDPATH
	envptr[9] = ptr;
	memcpy(ptr,"PATH=",5);
	ptr += 5;
	memcpy(ptr,_PATH_STDPATH,strlen(_PATH_STDPATH)+1);
	ptr += strlen(_PATH_STDPATH)+1;

	envptr[10] = NULL;
#else
	envptr[9] = NULL;
#endif
	
	return envptr;
}

int slurm_jobcomp_log_record ( uint32_t job_id, uint32_t user_id, char *job_name,
		char *job_state, char *partition, uint32_t time_limit,
		time_t start, time_t end_time, char *node_list)
{
	pid_t pid = -1;
	char user_id_str[32],job_id_str[32], nodes_cache[1];
	char start_str[32], end_str[32], lim_str[32];
	char * argvp[] = {script,NULL};
	int ret_value = SLURM_SUCCESS;
	char ** envp, * nodes;

	debug3("Entering slurm_jobcomp_log_record");
	snprintf(user_id_str,sizeof(user_id_str),"%u",user_id);
	snprintf(job_id_str,sizeof(job_id_str),"%u",job_id);
	snprintf(start_str, sizeof(start_str),"%lu",(long unsigned int) start);
	snprintf(end_str, sizeof(end_str),"%lu",(long unsigned int) end_time);
	nodes_cache[0] = '\0';

	if (time_limit == INFINITE) {
		strcpy(lim_str, "UNLIMITED");
	} else {
		snprintf(lim_str, sizeof(lim_str), "%lu", (unsigned long) time_limit);
	}
	
	if (node_list == NULL) {
		nodes = nodes_cache;
	} else {
		nodes = node_list;
	}

	/* Setup environment */
	envp = _create_environment(job_id_str,user_id_str,job_name,job_state,
					partition,lim_str,start_str,end_str,nodes);

	if (envp == NULL) {
		plugin_errno = ENOMEM;
		return SLURM_ERROR;
	}

	pid = fork();

	if (pid < 0) {
		/* Something bad happened */
		error("fork: %m");
		xfree(envp);
		plugin_errno = errno;
		return SLURM_ERROR;
	} else if (pid == 0) {
		/*Child process*/

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
		return SLURM_ERROR;	/* should never reach this */
	} else {
		/*Parent Processes*/

		/*
		 * Wait for the script to finish and get the exit status
		 * Not sure if this is a good idea.  Might want to just return.
		 */
		waitpid(pid, &ret_value, 0);
		xfree(envp);
		debug3("Exiting slurm_jobcomp_log_record");
		if (WIFEXITED(ret_value) && !WEXITSTATUS(ret_value)) {
			return SLURM_SUCCESS;
		} else {
			plugin_errno = WEXITSTATUS(ret_value);
			return SLURM_ERROR;
		}
	}
}

/* Return the error code of the plugin */
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

/* Called when script unloads */
int fini ( void )
{
	xfree(script);
	return SLURM_SUCCESS;
}
