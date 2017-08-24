
/*****************************************************************************\
 *  slurm_jobcomp.c - implementation-independent job completion logging
 *  functions
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>, Morris Jette <jette1@llnl.com>
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
\*****************************************************************************/

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_jobcomp.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, job completion
 * logging plugins will stop working.  If you need to add fields, add them
 * at the end of the structure.
 */
typedef struct slurm_jobcomp_ops {
	int          (*set_loc)   ( char *loc );
	int          (*job_write) ( struct job_record *job_ptr);
	int          (*sa_errno)  ( void );
	char *       (*job_strerror)  ( int errnum );
	List         (*get_jobs)  ( slurmdb_job_cond_t *params );
	int          (*archive)   ( slurmdb_archive_cond_t *params );
} slurm_jobcomp_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_jobcomp_ops_t.
 */
static const char *syms[] = {
	"slurm_jobcomp_set_location",
	"slurm_jobcomp_log_record",
	"slurm_jobcomp_get_errno",
	"slurm_jobcomp_strerror",
	"slurm_jobcomp_get_jobs",
	"slurm_jobcomp_archive"
};

static slurm_jobcomp_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern void
jobcomp_destroy_job(void *object)
{
	jobcomp_job_rec_t *job = (jobcomp_job_rec_t *)object;
	if (job) {
		xfree(job->partition);
		xfree(job->start_time);
		xfree(job->end_time);
		xfree(job->uid_name);
		xfree(job->gid_name);
		xfree(job->nodelist);
		xfree(job->jobname);
		xfree(job->state);
		xfree(job->timelimit);
		xfree(job->blockid);
		xfree(job->connection);
		xfree(job->reboot);
		xfree(job->rotate);
		xfree(job->geo);
		xfree(job->bg_start_point);
		xfree(job->work_dir);
		xfree(job);
	}
}


extern int
g_slurm_jobcomp_init( char *jobcomp_loc )
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "jobcomp";
	char *type = NULL;

	slurm_mutex_lock( &context_lock );

	if (init_run && g_context)
		goto done;

	if (g_context)
		plugin_context_destroy(g_context);

	type = slurm_get_jobcomp_type();
	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	xfree(type);
	if (g_context)
		retval = (*(ops.set_loc))(jobcomp_loc);
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int
g_slurm_jobcomp_fini(void)
{
	slurm_mutex_lock( &context_lock );

	if ( !g_context)
		goto done;

	init_run = false;
	plugin_context_destroy ( g_context );
	g_context = NULL;

done:
	slurm_mutex_unlock( &context_lock );
	return SLURM_SUCCESS;
}

extern int
g_slurm_jobcomp_write(struct job_record *job_ptr)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(ops.job_write))(job_ptr);
	else {
		error ("slurm_jobcomp plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern int
g_slurm_jobcomp_errno(void)
{
	int retval = SLURM_SUCCESS;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(ops.sa_errno))();
	else {
		error ("slurm_jobcomp plugin context not initialized");
		retval = ENOENT;
	}
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern char *
g_slurm_jobcomp_strerror(int errnum)
{
	char *retval = NULL;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		retval = (*(ops.job_strerror))(errnum);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return retval;
}

extern List
g_slurm_jobcomp_get_jobs(slurmdb_job_cond_t *job_cond)
{
	List job_list = NULL;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		job_list = (*(ops.get_jobs))(job_cond);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return job_list;
}

extern int
g_slurm_jobcomp_archive(slurmdb_archive_cond_t *arch_cond)
{
	int rc = SLURM_ERROR;

	slurm_mutex_lock( &context_lock );
	if ( g_context )
		rc = (*(ops.archive))(arch_cond);
	else
		error ("slurm_jobcomp plugin context not initialized");
	slurm_mutex_unlock( &context_lock );
	return rc;
}
