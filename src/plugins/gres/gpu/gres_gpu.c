/*****************************************************************************\
 *  gres_gpu.c - Support GPUs as a generic resources.
 *****************************************************************************
 *  Copyright (C) 2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#define _GNU_SOURCE

#include <ctype.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/xstring.h"

#include "../common/gres_common.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char	*plugin_name		= "Gres GPU plugin";
const char	*plugin_type		= "gres/gpu";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

static char	*gres_name		= "gpu";
static List gres_devices = NULL;

#define GPU_LOW		1
#define GPU_MEDIUM	2 
#define GPU_HIGH_M1	3
#define GPU_HIGH	4

#define GPU_MODE_FREQ	1
#define GPU_MODE_MEM	2
#define GPU_MODE_VOLT	3


static int _xlate_freq_value(char *gpu_freq)
{
	int value;

	if ((gpu_freq[0] < '0') && (gpu_freq[0] > '9'))
		return 0;	/* Not a numeric value */
	value = strtol(gpu_freq, NULL, 10);
	return value;
}

static int _xlate_freq_code(char *gpu_freq)
{
	if (!gpu_freq || !gpu_freq[0])
		return 0;
	if ((gpu_freq[0] >= '0') && (gpu_freq[0] <= '9'))
		return 0;	/* Pure numeric value */
	if (!strcasecmp(gpu_freq, "low"))
		return GPU_LOW;
	else if (!strcasecmp(gpu_freq, "medium"))
		return GPU_MEDIUM;
	else if (!strcasecmp(gpu_freq, "highm1"))
		return GPU_HIGH_M1;
	else if (!strcasecmp(gpu_freq, "high"))
		return GPU_HIGH;

	verbose("%s: %s: Invalid job GPU frequency (%s)",
		plugin_type, __func__, gpu_freq);
	return 0;	/* Bad user input */
}


static int _xlate_freq_code2value(int gpu_mode, int freq_code, int gpu_idx)
{
	int value = 0;

//FIXME: Need logic to translate freq_code (high, medium, low, etc.) to numeric value
//the translation could vary by GPU in a heterogeneous environment
	if (gpu_mode == GPU_MODE_FREQ)
		value = freq_code * 10;
	else if (gpu_mode == GPU_MODE_MEM)
		value = freq_code * 100;
	else if (gpu_mode == GPU_MODE_VOLT)
		value = freq_code * 1000;

	return value;
}

static void _parse_gpu_freq2(char *gpu_freq, int *gpu_freq_code,
			     int *gpu_freq_value, int *mem_freq_code,
			     int *mem_freq_value, int *voltage_code,
			     int *voltage_value, bool *verbose_flag)
{
	char *tmp, *tok, *sep, *save_ptr = NULL;

	tmp = xstrdup(gpu_freq);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		sep = strchr(tok, '=');
		if (sep) {
			sep[0] = '\0';
			sep++;
			if (!strcasecmp(tok, "memory")) {
				if (!(*mem_freq_code = _xlate_freq_code(sep)) &&
				    !(*mem_freq_value =_xlate_freq_value(sep))){
					debug("Invalid job GPU memory frequency: %s",
					      tok);
				}
			} else if (!strcasecmp(tok, "voltage")) {
				if (!(*voltage_code = _xlate_freq_code(sep)) &&
				    !(*voltage_value = _xlate_freq_value(sep))){
					debug("Invalid job GPU voltage: %s",
					      tok);
				}
			} else {
				debug("%s: %s: Invalid job device type (%s)",
				      plugin_type, __func__, tok);
			}
		} else if (!strcasecmp(tok, "verbose")) {
			*verbose_flag = true;
		} else {
			if (!(*gpu_freq_code = _xlate_freq_code(tok)) &&
			    !(*gpu_freq_value = _xlate_freq_value(tok))) {
				debug("Invalid job GPU frequency: %s", tok);
			}
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
}

static void _parse_gpu_freq(char *gpu_freq, int *gpu_freq_code,
			    int *gpu_freq_value, int *mem_freq_code,
			    int *mem_freq_value, int *voltage_code,
			    int *voltage_value, bool *verbose_flag)
{
	int def_gpu_freq_code = 0, def_gpu_freq_value = 0;
	int def_mem_freq_code = 0, def_mem_freq_value = 0;
	int def_voltage_code  = 0, def_voltage_value = 0;
	int job_gpu_freq_code = 0, job_gpu_freq_value = 0;
	int job_mem_freq_code = 0, job_mem_freq_value = 0;
	int job_voltage_code  = 0, job_voltage_value = 0;
	char *def_freq;

	_parse_gpu_freq2(gpu_freq, &job_gpu_freq_code, &job_gpu_freq_value,
			 &job_mem_freq_code, &job_mem_freq_value,
			 &job_voltage_code, &job_voltage_value, verbose_flag);

	def_freq = slurm_get_gpu_freq_def();
	_parse_gpu_freq2(def_freq, &def_gpu_freq_code, &def_gpu_freq_value,
			 &def_mem_freq_code, &def_mem_freq_value,
			 &def_voltage_code, &def_voltage_value, verbose_flag);
	xfree(def_freq);

	if (job_gpu_freq_code)
		*gpu_freq_code = job_gpu_freq_code;
	else if (job_gpu_freq_value)
		*gpu_freq_value = job_gpu_freq_value;
	else if (def_gpu_freq_code)
		*gpu_freq_code = def_gpu_freq_code;
	else if (def_gpu_freq_value)
		*gpu_freq_value = def_gpu_freq_value;

	if (job_mem_freq_code)
		*mem_freq_code = job_mem_freq_code;
	else if (job_mem_freq_value)
		*mem_freq_value = job_mem_freq_value;
	else if (def_mem_freq_code)
		*mem_freq_code = def_mem_freq_code;
	else if (def_mem_freq_value)
		*mem_freq_value = def_mem_freq_value;

	if (job_voltage_code)
		*voltage_code = job_voltage_code;
	else if (job_voltage_value)
		*voltage_value = job_voltage_value;
	else if (def_voltage_code)
		*voltage_code = def_voltage_code;
	else if (def_voltage_value)
		*voltage_value = def_voltage_value;
}

static void _set_freq(char *gpu_freq, char *global_list, int node_inx)
{
	bool verbose_flag = false;
	int len = strlen(global_list);
	int *gpu_index = xmalloc(sizeof(int) * len);
	int gpu_index_cnt = 0, i;
	char *tok, *tmp, *sep = "", *save_ptr = NULL;
	int gpu_freq_num = 0, mem_freq_num = 0, voltage_num = 0;
	int gpu_freq_code = 0, mem_freq_code = 0, voltage_code = 0;
	int gpu_freq_value = 0, mem_freq_value = 0, voltage_value = 0;

	/* Parse device ID information */
	tmp = xstrdup(global_list);
	tok = strtok_r(tmp, ",", &save_ptr);
	while (tok) {
		gpu_index[gpu_index_cnt] = strtol(tok, NULL, 10);
		if ((gpu_index[gpu_index_cnt] >= 0) &&
		    (gpu_index[gpu_index_cnt] < 1024)) {
			gpu_index_cnt++;
		} else {
			error("%s: %s: Invalid GPU device id (%d)",
			      plugin_type, __func__, gpu_index[gpu_index_cnt]);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);

	/*
	 * Parse frequency/voltage information
	 */
	_parse_gpu_freq(gpu_freq, &gpu_freq_code, &gpu_freq_value,
			&mem_freq_code, &gpu_freq_value, &voltage_code,
			&voltage_value, &verbose_flag);

	/*
	 * Now set the values on the devices, translate codes to numeric values
	 * depending upon the available values on each device
	 */
	for (i = 0; i < gpu_index_cnt; i++) {
//FIXME: Flesh out code to set GPU frequncy values, see bug 5520
		if (gpu_freq_code || gpu_freq_value) {
			if (gpu_freq_code) {
				gpu_freq_num  = _xlate_freq_code2value(
							GPU_MODE_FREQ,
							gpu_freq_code,
							gpu_index[i]);
			} else {
				gpu_freq_num = gpu_freq_value;
			}
			xstrfmtcat(tmp, "freq:%d", gpu_freq_num);
			sep = ",";
		}
		if (mem_freq_code || gpu_freq_value) {
			if (mem_freq_code) {
				mem_freq_num  = _xlate_freq_code2value(
							GPU_MODE_MEM,
							mem_freq_code,
							gpu_index[i]);
			} else {
				mem_freq_num = mem_freq_value;
			}
			xstrfmtcat(tmp, "%smemory_freq:%d", sep, mem_freq_num);
			sep = ",";
		}
		if (voltage_code || voltage_value) {
			if (voltage_code) {
				voltage_num  = _xlate_freq_code2value(
							GPU_MODE_VOLT,
							voltage_code,
							gpu_index[i]);
			} else {
				voltage_num = voltage_value;
			}
			xstrfmtcat(tmp, "%svoltage:%d", sep, voltage_num);
		}
		info("Set GPU[%d] %s", gpu_index[i], tmp);
		if ((node_inx == 0) && (i == 0) && verbose_flag)
			fprintf(stderr, "GpuFreq=%s\n", tmp);
		xfree(tmp);
	}

	xfree(gpu_index);
}

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job, char *gpu_freq)
{
	char *global_list = NULL, *local_list = NULL;
	char *slurm_env_var = NULL;

	if (is_job)
		slurm_env_var = "SLURM_JOB_GPUS";
	else
		slurm_env_var = "SLURM_STEP_GPUS";

	if (*already_seen) {
		global_list = xstrdup(getenvp(*env_ptr, slurm_env_var));
		local_list = xstrdup(getenvp(*env_ptr,
					     "CUDA_VISIBLE_DEVICES"));
	}

	common_gres_set_env(gres_devices, env_ptr, gres_ptr, node_inx,
			    usable_gres, "", local_inx,
			    &local_list, &global_list,
			    reset, is_job);

	if (global_list) {
		if (gpu_freq)
			_set_freq(gpu_freq, global_list, node_inx);
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	}

	if (local_list) {
		env_array_overwrite(
			env_ptr, "CUDA_VISIBLE_DEVICES", local_list);
		env_array_overwrite(
			env_ptr, "GPU_DEVICE_ORDINAL", local_list);
		xfree(local_list);
		*already_seen = true;
	}
}

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
	FREE_NULL_LIST(gres_devices);

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int node_config_load(List gres_conf_list)
{
	int rc = SLURM_SUCCESS;

	/* Assume this state is caused by an scontrol reconfigure */
	if (gres_devices) {
	        info("Resetting gres_devices");
	        FREE_NULL_LIST(gres_devices);
	}

#if 0
//FIXME: gres_conf_list contains records of type gres_slurmd_conf_t*
//FIXME: Use "nvidia-smi" tool to populate/update the records here, especially cpus & links
	ListIterator itr;
	char cpu_bit_str[64];
	gres_slurmd_conf_t *gres_slurmd_conf;
	itr = list_iterator_create(gres_conf_list);
	while ((gres_slurmd_conf = list_next(itr))) {
		info("GRES:%s(%u) Type:%s Count:%"PRIu64,
		     gres_slurmd_conf->name, gres_slurmd_conf->plugin_id,
		     gres_slurmd_conf->type_name, gres_slurmd_conf->count);
		if (gres_slurmd_conf->cpus) {
			info("  CPUs(%u):%s", gres_slurmd_conf->cpu_cnt,
			     gres_slurmd_conf->cpus);
		}
		if (gres_slurmd_conf->cpus_bitmap) {
			bit_fmt(cpu_bit_str, sizeof(cpu_bit_str),
				gres_slurmd_conf->cpus_bitmap);
			info("  CPU_bitmap:%s", cpu_bit_str);
		}
		if (gres_slurmd_conf->file)
			info("  File:%s", gres_slurmd_conf->file);
		if (gres_slurmd_conf->links)
			info("  Links:%s", gres_slurmd_conf->links);
	}
	list_iterator_destroy(itr);
#endif

	rc = common_node_config_load(gres_conf_list, gres_name,
				     &gres_devices);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);

	return rc;
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void job_set_env(char ***job_env_ptr, void *gres_ptr, int node_inx)
{
	/*
	 * Variables are not static like in step_*_env since we could be calling
	 * this from the slurmd where we are dealing with a different job each
	 * time we hit this function, so we don't want to keep track of other
	 * unrelated job's status.  This can also get called multiple times
	 * (different prologs and such) which would also result in bad info each
	 * call after the first.
	 */
	int local_inx = 0;
	bool already_seen = false;

	_set_env(job_env_ptr, gres_ptr, node_inx, NULL,
		 &already_seen, &local_inx, false, true, NULL);
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void step_set_env(char ***step_env_ptr, void *gres_ptr, char *tres_freq,
			 int local_proc_id)
{
	static int local_inx = 0;
	static bool already_seen = false;
	char  *tmp, *gpu_freq = NULL;

	if (local_proc_id == 0) {
		if (tres_freq && (tmp = strstr(tres_freq, "gpu:"))) {
			gpu_freq = xstrdup(tmp + 4);
			if ((tmp = strchr(gpu_freq, ';')))
				tmp[0] = '\0';
		} else {
			/* Make non-NULL to process default: GpuFreqDef */
			gpu_freq = xstrdup("");
		}
	}
	_set_env(step_env_ptr, gres_ptr, 0, NULL,
		 &already_seen, &local_inx, false, false, gpu_freq);
	xfree(gpu_freq);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one task)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void step_reset_env(char ***step_env_ptr, void *gres_ptr,
			   bitstr_t *usable_gres)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, usable_gres,
		 &already_seen, &local_inx, true, false, NULL);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void send_stepd(int fd)
{
	common_send_stepd(fd, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(int fd)
{
	common_recv_stepd(fd, &gres_devices);
}

extern int job_info(gres_job_state_t *job_gres_data, uint32_t node_inx,
		     enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

extern int step_info(gres_step_state_t *step_gres_data, uint32_t node_inx,
		     enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

extern List get_devices(void)
{
	return gres_devices;
}
