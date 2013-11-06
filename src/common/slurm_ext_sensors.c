/*****************************************************************************\
 *  slurm_ext_sensors.c - implementation-independent external sensors plugin
 *  definitions
 *****************************************************************************
 *  Copyright (C) 2013 Bull-HN-PHX.
 *  Written by Bull-HN-PHX/Martin Perry,
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/macros.h"
#include "src/common/parse_config.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_protocol_api.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"


typedef struct slurm_ext_sensors_ops {
	int (*update_component_data) (void);
	int (*get_stepstartdata)     (struct step_record *step_rec);
	int (*get_stependdata)       (struct step_record *step_rec);
	List (*get_config)           (void);
} slurm_ext_sensors_ops_t;
/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_ext_sensors_ops_t.
 */
static const char *syms[] = {
	"ext_sensors_p_update_component_data",
	"ext_sensors_p_get_stepstartdata",
	"ext_sensors_p_get_stependdata",
	"ext_sensors_p_get_config",
};

static slurm_ext_sensors_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

extern int ext_sensors_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "ext_sensors";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = slurm_get_ext_sensors_type();

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
	xfree(type);

	return retval;
}

extern int ext_sensors_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;

	return rc;
}

extern ext_sensors_data_t *ext_sensors_alloc(void)
{
	ext_sensors_data_t *ext_sensors =
		xmalloc(sizeof(struct ext_sensors_data));

	ext_sensors->consumed_energy = NO_VAL;
	ext_sensors->temperature = NO_VAL;

	return ext_sensors;
}

extern void ext_sensors_destroy(ext_sensors_data_t *ext_sensors)
{
	xfree(ext_sensors);
}

extern void ext_sensors_data_pack(ext_sensors_data_t *ext_sensors, Buf buffer,
				    uint16_t protocol_version)
{
	if (!ext_sensors) {
		pack32(0, buffer);
		pack32(0, buffer);
		pack_time((time_t)0, buffer);
		pack32(0, buffer);
		return;
	}

	pack32(ext_sensors->consumed_energy, buffer);
	pack32(ext_sensors->temperature, buffer);
	pack_time(ext_sensors->energy_update_time, buffer);
	pack32(ext_sensors->current_watts, buffer);
}

extern int ext_sensors_data_unpack(ext_sensors_data_t **ext_sensors, Buf buffer,
				     uint16_t protocol_version)
{
	ext_sensors_data_t *ext_sensors_ptr = ext_sensors_alloc();
	*ext_sensors = ext_sensors_ptr;
	if (ext_sensors_ptr == NULL)
		return SLURM_ERROR;

	safe_unpack32(&ext_sensors_ptr->consumed_energy, buffer);
	safe_unpack32(&ext_sensors_ptr->temperature, buffer);
	safe_unpack_time(&ext_sensors_ptr->energy_update_time, buffer);
	safe_unpack32(&ext_sensors_ptr->current_watts, buffer);

	return SLURM_SUCCESS;

unpack_error:
	ext_sensors_destroy(ext_sensors_ptr);
	*ext_sensors = NULL;
	return SLURM_ERROR;
}

extern int ext_sensors_g_update_component_data(void)
{
	int retval = SLURM_ERROR;

	if (ext_sensors_init() < 0)
		return retval;

	retval = (*(ops.update_component_data))();

	return retval;
}

extern int ext_sensors_g_get_stepstartdata(struct step_record *step_rec)
{
	int retval = SLURM_ERROR;

	if (ext_sensors_init() < 0)
		return retval;

	retval = (*(ops.get_stepstartdata))(step_rec);

	return retval;
}

extern int ext_sensors_g_get_stependdata(struct step_record *step_rec)
{
	int retval = SLURM_ERROR;

	if (ext_sensors_init() < 0)
		return retval;

	retval = (*(ops.get_stependdata))(step_rec);

	return retval;
}

extern int ext_sensors_g_get_config(void *data)
{

	List *tmp_list = (List *) data;

	if (ext_sensors_init() < 0)
		return SLURM_ERROR;

	*tmp_list = (*(ops.get_config))();

	return SLURM_SUCCESS;
}
