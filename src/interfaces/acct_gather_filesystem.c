/*****************************************************************************\
 *  slurm_acct_gather_filesystem.c - implementation-independent job filesystem
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2013 Bull.
 *  Written by Yiannis Georgiou <yiannis.georgiou@bull.net>
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com>.
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_SYS_PRCTL_H
#  include <sys/prctl.h>
#endif

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/interfaces/acct_gather_filesystem.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_acct_gather_filesystem_ops {
	int (*node_update)	(void);
	void (*conf_options)	(s_p_options_t **full_options,
				 int *full_options_cnt);
	void (*conf_set)	(s_p_hashtbl_t *tbl);
	void (*conf_values)        (List *data);
	int (*get_data)		(acct_gather_data_t *data);
} slurm_acct_gather_filesystem_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_filesystem_ops_t.
 */
static const char *syms[] = {
	"acct_gather_filesystem_p_node_update",
	"acct_gather_filesystem_p_conf_options",
	"acct_gather_filesystem_p_conf_set",
	"acct_gather_filesystem_p_conf_values",
	"acct_gather_filesystem_p_get_data",
};

static slurm_acct_gather_filesystem_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static bool acct_shutdown = true;
static int freq = 0;
static pthread_t watch_node_thread_id = 0;
static acct_gather_profile_timer_t *profile_timer =
	&acct_gather_profile_timer[PROFILE_FILESYSTEM];

static void *_watch_node(void *arg)
{
#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "acctg_fs", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m", __func__, "acctg_fs");
	}
#endif

	while (init_run && acct_gather_profile_test()) {
		/* Do this until shutdown is requested */
		slurm_mutex_lock(&g_context_lock);
		(*(ops.node_update))();
		slurm_mutex_unlock(&g_context_lock);

		slurm_mutex_lock(&profile_timer->notify_mutex);
		slurm_cond_wait(&profile_timer->notify,
				&profile_timer->notify_mutex);
		slurm_mutex_unlock(&profile_timer->notify_mutex);
	}
	return NULL;
}

extern int acct_gather_filesystem_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "acct_gather_filesystem";
	char *type = NULL;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_acct_gather_filesystem_type();

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	slurm_mutex_unlock(&g_context_lock);
	if (retval != SLURM_SUCCESS)
		fatal("can not open the %s plugin", type);
	xfree(type);

	return retval;
}

extern int acct_gather_filesystem_fini(void)
{
	int rc = SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	if (g_context) {
		init_run = false;

		if (watch_node_thread_id) {
			slurm_mutex_unlock(&g_context_lock);
			slurm_mutex_lock(&profile_timer->notify_mutex);
			slurm_cond_signal(&profile_timer->notify);
			slurm_mutex_unlock(&profile_timer->notify_mutex);
			pthread_join(watch_node_thread_id, NULL);
			slurm_mutex_lock(&g_context_lock);
		}

		rc = plugin_context_destroy(g_context);
		g_context = NULL;
	}
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

/*
 * This is sent an array that will be filled in from the plugin(s).  It is not a
 * direct pointer since we could have (in the future) this be stackable.
 */
extern int acct_gather_filesystem_g_get_data(acct_gather_data_t *data)
{
	int retval = SLURM_SUCCESS;

	xassert(init_run);
	retval = (*(ops.get_data))(data);
	return retval;
}

extern int acct_gather_filesystem_startpoll(uint32_t frequency)
{
	int retval = SLURM_SUCCESS;

	xassert(init_run);

	if (!acct_shutdown) {
		error("acct_gather_filesystem_startpoll: "
		      "poll already started!");
		return retval;
	}

	acct_shutdown = false;

	freq = frequency;

	if (frequency == 0) {   /* don't want dynamic monitoring? */
		debug2("acct_gather_filesystem dynamic logging disabled");
		return retval;
	}

	/* create polling thread */
	slurm_thread_create(&watch_node_thread_id, _watch_node, NULL);

	debug3("acct_gather_filesystem dynamic logging enabled");

	return retval;
}


extern int acct_gather_filesystem_g_conf_options(s_p_options_t **full_options,
						  int *full_options_cnt)
{
	xassert(init_run);
        (*(ops.conf_options))(full_options, full_options_cnt);
	return SLURM_SUCCESS;
}

extern int acct_gather_filesystem_g_conf_set(s_p_hashtbl_t *tbl)
{
	xassert(init_run);

        (*(ops.conf_set))(tbl);
	return SLURM_SUCCESS;
}


extern int acct_gather_filesystem_g_conf_values(void *data)
{
	xassert(init_run);

	(*(ops.conf_values))(data);
	return SLURM_SUCCESS;
}
