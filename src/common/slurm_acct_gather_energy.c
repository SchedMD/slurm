/*****************************************************************************\
 *  slurm_acct_gather_energy.c - implementation-independent job energy
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2012 Bull-HN-PHX.
 *  Written by Bull-HN-PHX/d.rusak,
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
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
** Define slurm-specific aliases for use by plugins, see slurm_xlator.h
** for details.
 */
strong_alias(acct_gather_energy_destroy, slurm_acct_gather_energy_destroy);

typedef struct slurm_acct_gather_energy_ops {
	int (*update_node_energy) (void);
	int (*get_data)           (enum acct_energy_type data_type, void *data);
	int (*set_data)           (enum acct_energy_type data_type, void *data);
	void (*conf_options)      (s_p_options_t **full_options,
				   int *full_options_cnt);
	void (*conf_set)          (s_p_hashtbl_t *tbl);
	void (*conf_values)        (List *data);
} slurm_acct_gather_energy_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_acct_gather_energy_ops_t.
 */
static const char *syms[] = {
	"acct_gather_energy_p_update_node_energy",
	"acct_gather_energy_p_get_data",
	"acct_gather_energy_p_set_data",
	"acct_gather_energy_p_conf_options",
	"acct_gather_energy_p_conf_set",
	"acct_gather_energy_p_conf_values",
};

static slurm_acct_gather_energy_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;
static bool acct_shutdown = true;
static int freq = 0;
static pthread_t watch_node_thread_id = 0;
static acct_gather_profile_timer_t *profile_timer =
	&acct_gather_profile_timer[PROFILE_ENERGY];

static void *_watch_node(void *arg)
{
	int delta = profile_timer->freq - 1;

#if HAVE_SYS_PRCTL_H
	if (prctl(PR_SET_NAME, "acctg_energy", NULL, NULL, NULL) < 0) {
		error("%s: cannot set my name to %s %m",
		      __func__, "acctg_energy");
	}
#endif

	while (init_run && acct_gather_profile_test()) {
		/* Do this until shutdown is requested */
		slurm_mutex_lock(&g_context_lock);
		(*(ops.set_data))(ENERGY_DATA_PROFILE, &delta);
		slurm_mutex_unlock(&g_context_lock);

		slurm_mutex_lock(&profile_timer->notify_mutex);
		slurm_cond_wait(&profile_timer->notify,
				&profile_timer->notify_mutex);
		slurm_mutex_unlock(&profile_timer->notify_mutex);
	}

	return NULL;
}


extern int slurm_acct_gather_energy_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "acct_gather_energy";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_acct_gather_energy_type();

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
	if (retval == SLURM_SUCCESS)
		retval = acct_gather_conf_init();
	if (retval != SLURM_SUCCESS)
		fatal("can not open the %s plugin", type);
	xfree(type);

	return retval;
}

extern int acct_gather_energy_fini(void)
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

extern acct_gather_energy_t *acct_gather_energy_alloc(uint16_t cnt)
{
	acct_gather_energy_t *energy =
		xmalloc(sizeof(struct acct_gather_energy) * cnt);

	return energy;
}

extern void acct_gather_energy_destroy(acct_gather_energy_t *energy)
{
	xfree(energy);
}

extern void acct_gather_energy_pack(acct_gather_energy_t *energy, Buf buffer,
				    uint16_t protocol_version)
{
	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		if (!energy) {
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack32(0, buffer);
			pack64(0, buffer);
			pack_time(0, buffer);
			return;
		}

		pack64(energy->base_consumed_energy, buffer);
		pack32(energy->base_watts, buffer);
		pack64(energy->consumed_energy, buffer);
		pack32(energy->current_watts, buffer);
		pack64(energy->previous_consumed_energy, buffer);
		pack_time(energy->poll_time, buffer);
	}
}

extern int acct_gather_energy_unpack(acct_gather_energy_t **energy, Buf buffer,
				     uint16_t protocol_version, bool need_alloc)
{
	acct_gather_energy_t *energy_ptr;

	if (need_alloc) {
		energy_ptr = acct_gather_energy_alloc(1);
		*energy = energy_ptr;
	} else {
		energy_ptr = *energy;
	}

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		safe_unpack64(&energy_ptr->base_consumed_energy, buffer);
		safe_unpack32(&energy_ptr->base_watts, buffer);
		safe_unpack64(&energy_ptr->consumed_energy, buffer);
		safe_unpack32(&energy_ptr->current_watts, buffer);
		safe_unpack64(&energy_ptr->previous_consumed_energy, buffer);
		safe_unpack_time(&energy_ptr->poll_time, buffer);
	}

	return SLURM_SUCCESS;

unpack_error:
	if (need_alloc) {
		acct_gather_energy_destroy(energy_ptr);
		*energy = NULL;
	} else
		memset(energy_ptr, 0, sizeof(acct_gather_energy_t));

	return SLURM_ERROR;
}

extern int acct_gather_energy_g_update_node_energy(void)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.update_node_energy))();

	return retval;
}

extern int acct_gather_energy_g_get_data(enum acct_energy_type data_type,
					 void *data)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.get_data))(data_type, data);

	return retval;
}

extern int acct_gather_energy_g_set_data(enum acct_energy_type data_type,
					 void *data)
{
	int retval = SLURM_ERROR;

	if (slurm_acct_gather_energy_init() < 0)
		return retval;

	retval = (*(ops.set_data))(data_type, data);

	return retval;
}

extern int acct_gather_energy_startpoll(uint32_t frequency)
{
	int retval = SLURM_SUCCESS;

	if (slurm_acct_gather_energy_init() < 0)
		return SLURM_ERROR;

	if (!acct_shutdown) {
		error("acct_gather_energy_startpoll: "
		      "poll already started!");
		return retval;
	}

	acct_shutdown = false;

	freq = frequency;

	if (frequency == 0) {   /* don't want dynamic monitoring? */
		debug2("acct_gather_energy dynamic logging disabled");
		return retval;
	}

	/* create polling thread */
	slurm_thread_create(&watch_node_thread_id, _watch_node, NULL);

	debug3("acct_gather_energy dynamic logging enabled");

	return retval;
}

extern int acct_gather_energy_g_conf_options(s_p_options_t **full_options,
					      int *full_options_cnt)
{
	if (slurm_acct_gather_energy_init() < 0)
		return SLURM_ERROR;

	(*(ops.conf_options))(full_options, full_options_cnt);
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_g_conf_set(s_p_hashtbl_t *tbl)
{
	if (slurm_acct_gather_energy_init() < 0)
		return SLURM_ERROR;

	(*(ops.conf_set))(tbl);
	return SLURM_SUCCESS;
}

extern int acct_gather_energy_g_conf_values(void *data)
{
	if (slurm_acct_gather_energy_init() < 0)
		return SLURM_ERROR;

	(*(ops.conf_values))(data);
	return SLURM_SUCCESS;
}
