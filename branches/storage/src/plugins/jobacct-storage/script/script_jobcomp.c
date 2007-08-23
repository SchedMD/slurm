/*****************************************************************************\
 *  script_jobcomp.c - text file slurm job completion logging plugin.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
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
#include "src/common/node_select.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/parse_time.h"

#include "script_jobcomp_process.h"

#define JOB_FORMAT "JobId=%lu UserId=%s(%lu) Name=%s JobState=%s Partition=%s "\
		"TimeLimit=%s StartTime=%s EndTime=%s NodeList=%s NodeCnt=%u %s\n"
 
/* Type for error string table entries */
typedef struct {
	int xe_number;
	char *xe_message;
} slurm_errtab_t;

static slurm_errtab_t slurm_errtab[] = {
	{0, "No error"},
	{-1, "Unspecified error"}
};

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

/* File descriptor used for logging */
static pthread_mutex_t  file_lock = PTHREAD_MUTEX_INITIALIZER;
static char *           log_name  = NULL;
static int              job_comp_fd = -1;

/* get the user name for the give user_id */
static void
_get_user_name(uint32_t user_id, char *user_name, int buf_size)
{
	static uint32_t cache_uid      = 0;
	static char     cache_name[32] = "root";
	struct passwd * user_info      = NULL;

	if (user_id == cache_uid)
		snprintf(user_name, buf_size, "%s", cache_name);
	else {
		user_info = getpwuid((uid_t) user_id);
		if (user_info && user_info->pw_name[0])
			snprintf(cache_name, sizeof(cache_name), "%s", 
				user_info->pw_name);
		else
			snprintf(cache_name, sizeof(cache_name), "Unknown");
		cache_uid = user_id;
		snprintf(user_name, buf_size, "%s", cache_name);
	}
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

extern int script_jobcomp_init(char *location)
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

extern int script_jobcomp_fini ( void )
{
	if (job_comp_fd >= 0)
		close(job_comp_fd);
	xfree(log_name);
	return SLURM_SUCCESS;
}

extern int script_jobcomp_get_errno( void )
{
	return plugin_errno;
}

extern int script_jobcomp_log_record ( struct job_record *job_ptr )
{
	int rc = SLURM_SUCCESS;
	char job_rec[512+MAX_JOBNAME_LEN];
	char usr_str[32], start_str[32], end_str[32], lim_str[32];
	char select_buf[128];
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

	slurm_make_time_str(&(job_ptr->start_time),
			    start_str, sizeof(start_str));
	slurm_make_time_str(&(job_ptr->end_time), end_str, sizeof(end_str));

	select_g_sprint_jobinfo(job_ptr->select_jobinfo,
		select_buf, sizeof(select_buf), SELECT_PRINT_MIXED);

	snprintf(job_rec, sizeof(job_rec), JOB_FORMAT,
			(unsigned long) job_ptr->job_id, usr_str, 
			(unsigned long) job_ptr->user_id, job_ptr->name, 
			job_state_string(job_state), 
			job_ptr->partition, lim_str, start_str, 
			end_str, job_ptr->nodes, job_ptr->node_cnt,
			select_buf);
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

extern char *script_jobcomp_strerror( int errnum )
{
	char *res = _lookup_slurm_api_errtab(errnum);
	return (res ? res : strerror(errnum));
}


/* 
 * get info from the storage 
 * in/out job_list List of job_rec_t *
 * note List needs to be freed when called
 */
extern void script_jobcomp_get_jobs(List job_list, 
				      List selected_steps, List selected_parts,
				      void *params)
{
	script_jobcomp_process_get_jobs(job_list, 
					  selected_steps, selected_parts,
					  params);	
	return;
}

/* 
 * expire old info from the storage 
 */
extern void script_jobcomp_archive(List selected_parts,
				     void *params)
{
	script_jobcomp_process_archive(selected_parts, params);
	return;
}
