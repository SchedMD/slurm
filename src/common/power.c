/*****************************************************************************\
 *  src/common/power.c - Generic power management plugin wrapper functions.
 *****************************************************************************
 *  Copyright (C) 2014-2015 SchedMD LLC.
 *  Written by Morris Jette <jette@schedmd.com>
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

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/power.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmctld/slurmctld.h"

typedef struct slurm_power_ops {
	void		(*job_resume)	(job_record_t *job_ptr);
	void		(*job_start)	(job_record_t *job_ptr);
	void		(*reconfig)	(void);
} slurm_power_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_power_ops_t.
 */
static const char *syms[] = {
	"power_p_job_resume",
	"power_p_job_start",
	"power_p_reconfig"
};

static int g_context_cnt = -1;
static slurm_power_ops_t *ops = NULL;
static plugin_context_t **g_context = NULL;
static pthread_mutex_t g_context_lock = PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/* Initialize the power plugin */
extern int power_g_init(void)
{
	int rc = SLURM_SUCCESS;
	char *last = NULL, *names, *power_plugin;
	char *plugin_type = "power";
	char *type;

	if (init_run && (g_context_cnt >= 0))
		return rc;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt >= 0)
		goto fini;

	g_context_cnt = 0;
	if (!slurm_conf.power_plugin || !slurm_conf.power_plugin[0])
		goto fini;

	names = power_plugin = xstrdup(slurm_conf.power_plugin);
	while ((type = strtok_r(names, ",", &last))) {
		xrealloc(ops, (sizeof(slurm_power_ops_t)*(g_context_cnt + 1)));
		xrealloc(g_context,
			 (sizeof(plugin_context_t *) * (g_context_cnt + 1)));
		if (xstrncmp(type, "power/", 6) == 0)
			type += 6; /* backward compatibility */
		type = xstrdup_printf("power/%s", type);
		g_context[g_context_cnt] = plugin_context_create(
			plugin_type, type, (void **)&ops[g_context_cnt],
			syms, sizeof(syms));
		if (!g_context[g_context_cnt]) {
			error("cannot create %s context for %s",
			      plugin_type, type);
			rc = SLURM_ERROR;
			xfree(type);
			break;
		}

		xfree(type);
		g_context_cnt++;
		names = NULL; /* for next iteration */
	}
	xfree(power_plugin);
	init_run = true;

fini:
	slurm_mutex_unlock(&g_context_lock);

	if (rc != SLURM_SUCCESS)
		power_g_fini();

	return rc;
}

/* Terminate the power plugin and free all memory */
extern void power_g_fini(void)
{
	int i;

	slurm_mutex_lock(&g_context_lock);
	if (g_context_cnt < 0)
		goto fini;

	init_run = false;
	for (i = 0; i < g_context_cnt; i++) {
		if (g_context[i])
			plugin_context_destroy(g_context[i]);
	}
	xfree(ops);
	xfree(g_context);
	g_context_cnt = -1;

fini:	slurm_mutex_unlock(&g_context_lock);
	return;
}

/* Read the configuration file */
extern void power_g_reconfig(void)
{
	int i;

	(void) power_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++)
		(*(ops[i].reconfig))();
	slurm_mutex_unlock(&g_context_lock);
}

/* Note that a suspended job has been resumed */
extern void power_g_job_resume(job_record_t *job_ptr)
{
	int i;

	(void) power_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++)
		(*(ops[i].job_resume))(job_ptr);
	slurm_mutex_unlock(&g_context_lock);
}

/* Note that a job has been allocated resources and is ready to start */
extern void power_g_job_start(job_record_t *job_ptr)
{
	int i;

	(void) power_g_init();
	slurm_mutex_lock(&g_context_lock);
	for (i = 0; i < g_context_cnt; i++)
		(*(ops[i].job_start))(job_ptr);
	slurm_mutex_unlock(&g_context_lock);
}

/* Pack a power management data structure */
extern void power_mgmt_data_pack(power_mgmt_data_t *power, buf_t *buffer,
				 uint16_t protocol_version)
{
	if (!power) {
		pack32(NO_VAL, buffer);
	} else {
		pack32(power->cap_watts, buffer);
	}
}

/* Unpack a power management data structure
 * Use power_mgmt_data_free() to free the returned structure */
extern int power_mgmt_data_unpack(power_mgmt_data_t **power, buf_t *buffer,
				  uint16_t protocol_version)
{
	power_mgmt_data_t *power_ptr = xmalloc(sizeof(power_mgmt_data_t));

	safe_unpack32(&power_ptr->cap_watts, buffer);
	*power = power_ptr;
	return SLURM_SUCCESS;

unpack_error:
	xfree(power_ptr);
	*power = NULL;
	return SLURM_ERROR;
}

/* Free a power management data structure */
extern void power_mgmt_data_free(power_mgmt_data_t *power)
{
	xfree(power);
}
