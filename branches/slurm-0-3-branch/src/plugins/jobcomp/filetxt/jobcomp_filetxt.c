/*****************************************************************************\
 *  jobcomp_filetxt.c - text file slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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

#if HAVE_CONFIG_H
#   include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

#define JOB_FORMAT "JobId=%lu UserId=%s(%lu) Name=%s JobState=%s Partition=%s"\
		" TimeLimit=%s StartTime=%s EndTime=%s NodeList=%s\n"
 
/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"}
};

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
const char plugin_name[]       	= "Job completion text file logging plugin";
const char plugin_type[]       	= "jobcomp/filetxt";
const uint32_t plugin_version	= 90;

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/* File descriptor used for logging */
static pthread_mutex_t  file_lock = PTHREAD_MUTEX_INITIALIZER;
static char *           log_name  = NULL;
static int              job_comp_fd = -1;
/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM job completion
 * logging API.
 */

int slurm_jobcomp_set_location ( char * location )
{
	int rc = SLURM_SUCCESS;

	if (location == NULL) {
		plugin_errno = EACCES;
		return SLURM_ERROR;
	}
	xfree(log_name);
	log_name = xstrdup(location);

	slurm_mutex_lock( &file_lock );
	if (job_comp_fd >= 0)
		close(job_comp_fd);
	job_comp_fd = open(location, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (job_comp_fd == -1) {
		fatal("open %s: %m", location);
		plugin_errno = errno;
		rc = SLURM_ERROR;
	} else
		fchmod(job_comp_fd, 0644);
	slurm_mutex_unlock( &file_lock );
	return rc;
}

/* get the user name for the give user_id */
static void
_get_user_name(uint32_t user_id, char *user_name, int buf_size)
{
	static uint32_t cache_uid      = 0;
	static char     cache_name[32] = "root";
	struct passwd * user_info      = NULL;

	if (user_id == cache_uid)
		snprintf(user_name, buf_size, cache_name);
	else {
		user_info = getpwuid((uid_t) user_id);
		if (user_info && user_info->pw_name[0])
			snprintf(user_name, buf_size, "%s",user_info->pw_name);
		else
			snprintf(user_name, buf_size, "Unknown");
		cache_uid = user_id;
		snprintf(cache_name, sizeof(cache_name), user_name);
	}
}

/*
 * make_time_str - convert time_t to string with "month/date hour:min:sec" 
 * IN time     - a time stamp
 * IN str_size - size of string buffer
 * OUT string  - pointer user defined buffer
 */
static void
_make_time_str (time_t *time, char *string, int str_size)
{
	struct tm time_tm;

	localtime_r (time, &time_tm);
	snprintf ( string, str_size, "%2.2u/%2.2u-%2.2u:%2.2u:%2.2u",
		(time_tm.tm_mon+1), time_tm.tm_mday,
		time_tm.tm_hour, time_tm.tm_min, time_tm.tm_sec);
}

int slurm_jobcomp_log_record ( struct job_record *job_ptr )
{
	int rc = SLURM_SUCCESS;
	char job_rec[256];
	char usr_str[32], start_str[32], end_str[32], lim_str[32];
	size_t offset = 0, tot_size, wrote;
	enum job_states job_state;

	if ((log_name == NULL) || (job_comp_fd < 0)) {
		error("JobCompLoc log file %s not open", log_name);
		return SLURM_ERROR;
	}

	slurm_mutex_lock( &file_lock );
	_get_user_name(job_ptr->user_id, usr_str, sizeof(usr_str));
	if (job_ptr->time_limit == INFINITE)
		strcpy(lim_str, "UNLIMITED");
	else
		snprintf(lim_str, sizeof(lim_str), "%lu", 
				(unsigned long) job_ptr->time_limit);

	/* Job will typically be COMPLETING when this is called. 
	 * We remove this flag to get the eventual completion state:
	 * JOB_FAILED, JOB_TIMEOUT, etc. */
	job_state = job_ptr->job_state & (~JOB_COMPLETING);

	_make_time_str(&(job_ptr->start_time), start_str, sizeof(start_str));
	_make_time_str(&(job_ptr->end_time),   end_str,   sizeof(end_str));

	snprintf(job_rec, sizeof(job_rec), JOB_FORMAT,
			(unsigned long) job_ptr->job_id, usr_str, 
			(unsigned long) job_ptr->user_id, job_ptr->name, 
			job_state_string(job_state), 
			job_ptr->partition, lim_str, start_str, 
			end_str, job_ptr->nodes);
	tot_size = strlen(job_rec);

	while ( offset < tot_size ) {
		wrote = write(job_comp_fd, job_rec + offset,
			tot_size - offset);
		if (wrote == -1) {
			if (errno == EAGAIN)
				continue;
			else {
				plugin_errno = errno;
				rc = SLURM_ERROR;
				break;
			}
		}
		offset += wrote;
	}
	slurm_mutex_unlock( &file_lock );
	return rc;
}

extern int slurm_jobcomp_get_errno( void )
{
	return plugin_errno;
}

/* 
 * Linear search through table of errno values and strings,
 * returns NULL on error, string on success.
 */
static char *_lookup_slurm_api_errtab(int errnum)
{
	char *res = NULL;
	int i;

	for (i = 0; i < sizeof(slurm_errtab) / sizeof(slurm_errtab_t); i++) {
		if (slurm_errtab[i].xe_number == errnum) {
			res = slurm_errtab[i].xe_message;
			break;
		}
	}
	return res;
}

extern char *slurm_jobcomp_strerror( int errnum )
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}

int fini ( void )
{
	if (job_comp_fd >= 0)
		close(job_comp_fd);
	xfree(log_name);
	return SLURM_SUCCESS;
}
