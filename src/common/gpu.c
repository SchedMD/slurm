/*****************************************************************************\
 *  gpu.c - driver for gpu plugin
 *****************************************************************************
 *  Copyright (C) 2019-2021 SchedMD LLC
 *  Written by Danny Auble <da@schedmd.com>
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

#include <dlfcn.h>

#include "src/common/gpu.h"
#include "src/common/plugin.h"

/* Gres symbols provided by the plugin */
typedef struct slurm_ops {
	void    (*reconfig)		(void);
	List	(*get_system_gpu_list) 	(node_config_load_t *node_conf);
	void	(*step_hardware_init)	(bitstr_t *usable_gpus,
					 char *tres_freq);
	void	(*step_hardware_fini)	(void);
	char   *(*test_cpu_conv)	(char *cpu_range);
	int     (*energy_read)          (uint32_t dv_ind, gpu_status_t *gpu);
	void    (*get_device_count)     (unsigned int *device_count);

} slurm_ops_t;

/*
 * These strings must be kept in the same order as the fields
 * declared for slurm_ops_t.
 */
static const char *syms[] = {
	"gpu_p_reconfig",
	"gpu_p_get_system_gpu_list",
	"gpu_p_step_hardware_init",
	"gpu_p_step_hardware_fini",
	"gpu_p_test_cpu_conv",
	"gpu_p_energy_read",
	"gpu_p_get_device_count",
};

/* Local variables */
static slurm_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t g_context_lock =	PTHREAD_MUTEX_INITIALIZER;
static bool init_run = false;

/*
 *  Common function to dlopen() the appropriate gpu libraries, and
 *   report back type needed.
 */
static char *_get_gpu_type(void)
{
	/*
	 *  Here we are dlopening the gpu .so to verify it exists on this node.
	 */
	uint32_t autodetect_flags = gres_get_autodetect_flags();

	if (autodetect_flags & GRES_AUTODETECT_GPU_NVML) {
#ifdef HAVE_NVML
		if (!dlopen("libnvidia-ml.so", RTLD_NOW | RTLD_GLOBAL))
			info("We were configured with nvml functionality, but that lib wasn't found on the system.");
		else
			return "gpu/nvml";
#else
		info("We were configured to autodetect nvml functionality, but we weren't able to find that lib when Slurm was configured.");
#endif
	} else if (autodetect_flags & GRES_AUTODETECT_GPU_RSMI) {
#ifdef HAVE_RSMI
		if (!dlopen("librocm_smi64.so", RTLD_NOW | RTLD_GLOBAL))
			info("Configured with rsmi, but that lib wasn't found.");
		else
			return "gpu/rsmi";
#else
		info("Configured with rsmi, but rsmi isn't enabled during the build.");
#endif
	} else if (autodetect_flags & GRES_AUTODETECT_GPU_ONEAPI) {
#ifdef HAVE_ONEAPI
		if (!dlopen("libze_loader.so", RTLD_NOW | RTLD_GLOBAL))
			info("Configured with oneAPI, but that lib wasn't found.");
		else
			return "gpu/oneapi";
#else
		info("Configured with oneAPI, but oneAPI isn't enabled during the build.");
#endif
	}

	return "gpu/generic";
}


/*
 * Initialize the GRES plugins.
 *
 * Returns a Slurm errno.
 */
extern int gpu_plugin_init(void)
{
	int retval = SLURM_SUCCESS;
	char *plugin_type = "gpu";
	char *type = NULL;

	if (init_run && g_context)
		return retval;

	slurm_mutex_lock(&g_context_lock);

	if (g_context)
		goto done;

	type = _get_gpu_type();

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

	return retval;
}

extern int gpu_plugin_fini(void)
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	slurm_mutex_lock(&g_context_lock);
	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	slurm_mutex_unlock(&g_context_lock);

	return rc;
}

extern void gpu_g_reconfig(void)
{
	if (gpu_plugin_init() < 0)
		return;
	(*(ops.reconfig))();
}

extern List gpu_g_get_system_gpu_list(node_config_load_t *node_conf)
{
	if (gpu_plugin_init() < 0)
		return NULL;

	return (*(ops.get_system_gpu_list))(node_conf);
}

extern void gpu_g_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	if (gpu_plugin_init() < 0)
		return;
	(*(ops.step_hardware_init))(usable_gpus, tres_freq);
}

extern void gpu_g_step_hardware_fini(void)
{
	if (gpu_plugin_init() < 0)
		return;
	(*(ops.step_hardware_fini))();
}

extern char *gpu_g_test_cpu_conv(char *cpu_range)
{
	if (gpu_plugin_init() < 0)
		return NULL;
	return (*(ops.test_cpu_conv))(cpu_range);

}

extern int gpu_g_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	if (gpu_plugin_init() < 0)
		return SLURM_ERROR;
	return (*(ops.energy_read))(dv_ind, gpu);
}

extern void gpu_g_get_device_count(unsigned int *device_count)
{
	if (gpu_plugin_init() < 0)
		return;
	(*(ops.get_device_count))(device_count);
}
