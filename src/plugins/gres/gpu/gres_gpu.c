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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

#include "src/common/slurm_xlator.h"
#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/gpu.h"
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/xcgroup_read_config.h"
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
static List	gres_devices		= NULL;

extern void step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	gpu_g_step_hardware_init(usable_gpus, tres_freq);
}

extern void step_hardware_fini(void)
{
	gpu_g_step_hardware_fini();
}

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job)
{
	char *global_list = NULL, *local_list = NULL, *slurm_env_var = NULL;

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
			    usable_gres, "", local_inx,  NULL,
			    &local_list, &global_list, reset, is_job, NULL);

	if (global_list) {
		env_array_overwrite(env_ptr, slurm_env_var, global_list);
		xfree(global_list);
	}

	if (local_list) {
		env_array_overwrite(
			env_ptr, "CUDA_VISIBLE_DEVICES", local_list);
		env_array_overwrite(
			env_ptr, "GPU_DEVICE_ORDINAL", local_list);
		env_array_overwrite(
			env_ptr, "ROCR_VISIBLE_DEVICES", local_list);
		xfree(local_list);
		*already_seen = true;
	}
}

/*
 * Sort gres/gpu records by descending length of type_name. If length is equal,
 * sort by ascending type_name. If still equal, sort by ascending file name.
 */
static int _sort_gpu_by_type_name(void *x, void *y)
{
	gres_slurmd_conf_t *gres_record1 = *(gres_slurmd_conf_t **)x;
	gres_slurmd_conf_t *gres_record2 = *(gres_slurmd_conf_t **)y;
	int val1, val2, ret;

	if (!gres_record1->type_name && !gres_record2->type_name)
		return 0;

	if (gres_record1->type_name && !gres_record2->type_name)
		return 1;

	if (!gres_record1->type_name && gres_record2->type_name)
		return -1;

	val1 = strlen(gres_record1->type_name);
	val2 = strlen(gres_record2->type_name);
	/*
	 * By default, qsort orders in ascending order (smallest first). We want
	 * descending order (longest first), so invert order by negating.
	 */
	ret = -(val1 - val2);

	/* Sort by type name value if type name length is equal */
	if (ret == 0)
		ret = xstrcmp(gres_record1->type_name, gres_record2->type_name);

	/* Sort by file name if type name value is equal */
	if (ret == 0)
		ret = xstrcmp(gres_record1->file, gres_record2->file);

	return ret;
}

/*
 * Find the first conf_gres record (x) with a GRES type that is a substring
 * of the GRES type of sys_gres (key).
 */
static int _find_type_in_gres_list(void *x, void *key)
{
	gres_slurmd_conf_t *conf_gres = (gres_slurmd_conf_t *)x;
	char *sys_gres_type = (char *)key;

	if (!conf_gres)
		return 0;

	/* If count is 0, then we already accounted for it */
	if (conf_gres->count == 0)
		return 0;

	xassert(conf_gres->count == 1);

	if (xstrcasestr(sys_gres_type, conf_gres->type_name))
		return 1;
	else
		return 0;
}

/*
 * Sync the GRES type of each device detected on the system (gres_list_system)
 * to its corresponding GRES type specified in [gres|slurm].conf. In effect, the
 * sys GRES type will be cut down to match the corresponding conf GRES type.
 *
 * NOTE: Both lists will be sorted in descending order by type_name
 * length. gres_list_conf_single is assumed to only have records of count == 1.
 */
static void _normalize_sys_gres_types(List gres_list_system, List gres_list_conf_single)
{
	gres_slurmd_conf_t *sys_gres, *conf_gres;
	ListIterator itr;

	/* No need to sync anything if configured GRES list is empty */
	if (!gres_list_conf_single || list_count(gres_list_conf_single) == 0)
		return;

	/*
	 * Sort conf and sys gres lists by longest GRES type to shortest, so we
	 * can avoid issues if e.g. `k20m` and `k20m1` are both specified.
	 */
	list_sort(gres_list_conf_single, _sort_gpu_by_type_name);
	list_sort(gres_list_system, _sort_gpu_by_type_name);

	/* Only match devices if the conf gres count isn't exceeded */
	itr = list_iterator_create(gres_list_system);
	while ((sys_gres = list_next(itr))) {
		conf_gres = list_find_first(gres_list_conf_single,
					    _find_type_in_gres_list,
					    sys_gres->type_name);
		if (!conf_gres) {
			info("Could not find an unused configuration record with a GRES type that is a substring of system device `%s`. Setting system GRES type to NULL",
			     sys_gres->type_name);
			xfree(sys_gres->type_name);
			continue;
		}

		xassert(conf_gres->count == 1);

		/* Temporarily set count to 0 to account for it */
		conf_gres->count = 0;

		/* Overwrite sys_gres type to match conf_gres type */
		xfree(sys_gres->type_name);
		sys_gres->type_name = xstrdup(conf_gres->type_name);
	}
	list_iterator_destroy(itr);

	/* Reset counts back to 1 */
	itr = list_iterator_create(gres_list_conf_single);
	while ((conf_gres = list_next(itr)))
		conf_gres->count = 1;
	list_iterator_destroy(itr);
}

/* See if the conf GRES matches the system GRES */
static int _match_gres(gres_slurmd_conf_t *conf_gres,
		       gres_slurmd_conf_t *sys_gres)
{
	/* This record has already been "taken" (matched another conf GRES) */
	if (sys_gres->count == 0)
		return 0;

	/*
	 * If the config gres has a type check it with what is found on the
	 * system.
	 */
	if (conf_gres->type_name &&
	    xstrcmp(conf_gres->type_name, sys_gres->type_name))
		return 0;

	/*
	 * If the config gres has a file check it with what is found on the
	 * system.
	 */
	if (conf_gres->file && xstrcmp(conf_gres->file, sys_gres->file))
		return 0;

	/* If all checks out above or nothing was defined return */
	return 1;
}

/*
 * Check that a gres.conf GRES has the same CPUs and Links as a system GRES, if
 * specified
 */
static int _validate_cpus_links(gres_slurmd_conf_t *conf_gres,
			        gres_slurmd_conf_t *sys_gres)
{
	/*
	 * If the config gres has cpus defined check it with what is found on
	 * the system.
	 */
	if (conf_gres->cpus && conf_gres->cpus_bitmap &&
	    !bit_equal(conf_gres->cpus_bitmap, sys_gres->cpus_bitmap))
		return 0;

	/*
	 * If the config gres has links defined check it with what is found on
	 * the system.
	 */
	if (conf_gres->links && sys_gres->links &&
	    xstrcmp(conf_gres->links, sys_gres->links))
		return 0;

	/* If all checks out above, return */
	return 1;
}

/* Given a file name return its numeric suffix */
static int _file_inx(char *fname)
{
	int i, len, mult = 1, num, val = 0;

	if (!fname)
		return 0;
	len = strlen(fname);
	if (len == 0)
		return val;
	for (i = 1; i <= len; i++) {
		if ((fname[len - i] < '0') ||
		    (fname[len - i] > '9'))
			break;
		num = fname[len - i] - '0';
		val += (num * mult);
		mult *= 10;
	}
	return val;
}

/* Sort gres/gpu records by "File" value */
static int _sort_gpu_by_file(void *x, void *y, bool asc)
{
	gres_slurmd_conf_t *gres_record1 = *(gres_slurmd_conf_t **) x;
	gres_slurmd_conf_t *gres_record2 = *(gres_slurmd_conf_t **) y;
	int val1, val2;

	val1 = _file_inx(gres_record1->file);
	val2 = _file_inx(gres_record2->file);

	if (asc)
		return (val1 - val2);
	else
		return -(val1 - val2);
}

/* Sort gres/gpu records by "File" value in ascending order (nulls first) */
static int _sort_gpu_by_file_desc(void *x, void *y)
{
	return _sort_gpu_by_file(x, y, false);
}

/* Sort gres/gpu records by "File" value in descending order */
static int _sort_gpu_by_file_asc(void *x, void *y)
{
	return _sort_gpu_by_file(x, y, true);
}

/*
 * Takes the merged [slurm|gres].conf records in gres_list_conf and the GPU
 * devices detected on the node in gres_list_system and returns a final merged
 * list in gres_list_conf.
 *
 * If a conf GPU corresponds to a system GPU, CPUs and Links are checked to see
 * if they are the same. If not, an error is emitted and that device is excluded
 * from the final list.
 *
 * gres_list_conf   - (in/out) The GRES records as parsed from [slurm|gres].conf
 * gres_list_system - (in) The gpu devices detected by the system. Each record
 * 		      should only have a count of 1.
 *
 * NOTES:
 * gres_list_conf_single: Same as gres_list_conf, except broken down so each
 * 			  GRES record has only one device file.
 *
 * A conf GPU and system GPU will correspond if the following fields are equal:
 * 	*type
 * 	*file
 */
static void _normalize_gres_conf(List gres_list_conf, List gres_list_system)
{
	ListIterator itr, itr2;
	gres_slurmd_conf_t *gres_record, *sys_record;
	List gres_list_conf_single, gres_list_gpu = NULL, gres_list_non_gpu;

	if (gres_list_conf == NULL) {
		error("%s: gres_list_conf is NULL. This shouldn't happen",
		      __func__);
		return;
	}

	gres_list_conf_single = list_create(destroy_gres_slurmd_conf);
	gres_list_non_gpu = list_create(destroy_gres_slurmd_conf);
	gres_list_gpu = list_create(destroy_gres_slurmd_conf);

	debug2("gres_list_conf:");
	print_gres_list(gres_list_conf, LOG_LEVEL_DEBUG2);

	// Break down gres_list_conf into 1 device per record
	itr = list_iterator_create(gres_list_conf);
	while ((gres_record = list_next(itr))) {
		int i;
		hostlist_t hl;
		char *hl_name;

		if (!gres_record->count)
			continue;

		// Just move this GRES record if it's not a GPU GRES
		if (xstrcasecmp(gres_record->name, "gpu")) {
			debug2("%s: preserving original `%s` GRES record",
			       __func__, gres_record->name);
			add_gres_to_list(gres_list_non_gpu,
					 gres_record->name,
					 gres_record->count,
					 gres_record->cpu_cnt,
					 gres_record->cpus,
					 gres_record->cpus_bitmap,
					 gres_record->file,
					 gres_record->type_name,
					 gres_record->links);
			continue;
		}

		if (gres_record->count == 1) {
			// Add device from single record
			add_gres_to_list(gres_list_conf_single,
					 gres_record->name, 1,
					 gres_record->cpu_cnt,
					 gres_record->cpus,
					 gres_record->cpus_bitmap,
					 gres_record->file,
					 gres_record->type_name,
					 gres_record->links);
			continue;
		} else if (!gres_record->file) {
			for (i = 0; i < gres_record->count; i++)
				add_gres_to_list(gres_list_conf_single,
						 gres_record->name, 1,
						 gres_record->cpu_cnt,
						 gres_record->cpus,
						 gres_record->cpus_bitmap,
						 gres_record->file,
						 gres_record->type_name,
						 gres_record->links);
			continue;
		}

		/*
		 * count > 1 and we have devices;
		 * Break down record into individual devices.
		 */
		hl = hostlist_create(gres_record->file);
		while ((hl_name = hostlist_shift(hl))) {
			add_gres_to_list(gres_list_conf_single,
					 gres_record->name, 1,
					 gres_record->cpu_cnt,
					 gres_record->cpus,
					 gres_record->cpus_bitmap, hl_name,
					 gres_record->type_name,
					 gres_record->links);
			free(hl_name);
		}
		hostlist_destroy(hl);
	}
	list_iterator_destroy(itr);

	/*
	 * Truncate the full system device types to match types in conf records
	 */
	_normalize_sys_gres_types(gres_list_system, gres_list_conf_single);

	/* Sort so null files are last for _match_gres() */
	list_sort(gres_list_conf_single, _sort_gpu_by_file_desc);
	/* Sort system devices in the same way for convenience */
	list_sort(gres_list_system, _sort_gpu_by_file_desc);

	itr = list_iterator_create(gres_list_conf_single);
	itr2 = list_iterator_create(gres_list_system);
	while ((gres_record = list_next(itr))) {
		list_iterator_reset(itr2);
		while ((sys_record = list_next(itr2))) {
			if (!_match_gres(gres_record, sys_record)) {
				continue;
			}

			/*
			 * We have a match, so if CPUs and Links are specified,
			 * see if they too match. If a value is specified and
			 * does not match the system, emit error. If null, just
			 * use the system-detected value.
			 */
			if (!_validate_cpus_links(gres_record, sys_record)) {
				/* What was specified differs from system */
				error("This GPU specified in [slurm|gres].conf has mismatching Cores or Links from the device found on the system. Ignoring it.");
				error("[slurm|gres].conf:");
				print_gres_conf(gres_record, LOG_LEVEL_ERROR);
				error("system:");
				print_gres_conf(sys_record, LOG_LEVEL_ERROR);

				xassert(sys_record->count == 1);

				/*
				 * Temporarily set the sys record count to 0 to
				 * mark it as already "used up"
				 */
				sys_record->count = 0;
				break;
			}

			/* We found a match! */
			break;
		}

		if (sys_record) {
			/*
			 * Completely ignore this conf record if Cores and/or
			 * Links do not match the corresponding system GPU
			 */
			if (sys_record->count == 0)
				continue;

			/*
			 * Since the system GPU matches up completely with a
			 * configured GPU, add the system GPU to the final list
			 */
			debug("Including the following GPU matched between system and configuration:");
			print_gres_conf(sys_record, LOG_LEVEL_DEBUG);
			list_remove(itr2);
			list_append(gres_list_gpu, sys_record);
			continue;
		}

		/* Else, config-only GPU */
		if (gres_record->file) {
			/*
			 * Add the "extra" configured GPU to the final list, but
			 * only if file is specified
			 */
			debug("Including the following config-only GPU:");
			print_gres_conf(gres_record, LOG_LEVEL_DEBUG);
			list_remove(itr);
			list_append(gres_list_gpu, gres_record);
		} else {
			/*
			 * Either the conf GPU was specified in slurm.conf only,
			 * or File (a required parameter for GPUs) was not
			 * specified in gres.conf. Either way, ignore it.
			 */
			error("Discarding the following config-only GPU due to lack of File specification:");
			print_gres_conf(gres_record, LOG_LEVEL_ERROR);
		}

	}
	list_iterator_destroy(itr);

	/* Reset the system GPU counts, in case system list is used after */
	list_iterator_reset(itr2);
	while ((sys_record = list_next(itr2)))
		if (sys_record->count == 0)
			sys_record->count = 1;
	list_iterator_destroy(itr2);

	/* Print out all the leftover system GPUs that are not being used */
	if (gres_list_system && list_count(gres_list_system)) {
		info("WARNING: The following autodetected GPUs are being ignored:");
		print_gres_list(gres_list_system, LOG_LEVEL_INFO);
	}

	/* Add GPUs + non-GPUs to gres_list_conf */
	list_flush(gres_list_conf);
	if (gres_list_gpu && list_count(gres_list_gpu)) {
		list_sort(gres_list_gpu, _sort_gpu_by_file_asc);
		debug2("gres_list_gpu");
		print_gres_list(gres_list_gpu, LOG_LEVEL_DEBUG2);
		list_transfer(gres_list_conf, gres_list_gpu);
	}
	if (gres_list_non_gpu && list_count(gres_list_non_gpu))
		list_transfer(gres_list_conf, gres_list_non_gpu);
	FREE_NULL_LIST(gres_list_gpu);
	FREE_NULL_LIST(gres_list_conf_single);
	FREE_NULL_LIST(gres_list_non_gpu);
}

/*
 * Parses fake_gpus_file for fake GPU devices and adds them to gres_list_system
 *
 * The file format is: <type>|<sys_cpu_count>|<cpu_range>|<links>|<device_file>
 *
 * Each line represents a single GPU device. Therefore, <device_file> can't
 * specify more than one file (i.e. ranges like [1-2] won't work).
 *
 * Each line has a max of 256 characters, including the newline.
 *
 * If `_` or `(null)` is specified, then the value will be left NULL or 0.
 *
 * If a <cpu_range> is of the form `~F0F0`, an array of unsigned longs will be
 * generated with the specified cpu hex mask and then converted to a bitstring.
 * This is to test converting the cpu mask from NVML to Slurm.
 * Only 0xF and 0x0 are supported.
 */
static void _add_fake_gpus_from_file(List gres_list_system,
				     char *fake_gpus_file)
{
	char buffer[256];
	int line_number = 0;
	FILE *f = fopen(fake_gpus_file, "r");
	if (f == NULL) {
		error("Unable to read \"%s\": %m", fake_gpus_file);
		return;
	}

	// Loop through each line of the file
	while (fgets(buffer, 256, f)) {
		char *save_ptr = NULL;
		char *tok;
		int i = 0;
		int cpu_count = 0;
		char *cpu_range = NULL;
		char *device_file = NULL;
		char *type = NULL;
		char *links = NULL;
		bitstr_t *cpu_aff_mac_bitstr = NULL;
		line_number++;

		/*
		 * Remove trailing newlines from fgets output
		 * See https://stackoverflow.com/a/28462221/1416379
		 */
		buffer[strcspn(buffer, "\r\n")] = '\0';

		// Ignore blank lines or lines that start with #
		if (!buffer[0] || buffer[0] == '#')
			continue;

		debug("%s", buffer);

		// Parse values from the line
		tok = strtok_r(buffer, "|", &save_ptr);
		while (tok) {
			// Leave value as null and continue
			if (xstrcmp(tok, "(null)") == 0) {
				i++;
				tok = strtok_r(NULL, "|", &save_ptr);
				continue;
			}

			switch (i) {
			case 0:
				type = xstrdup(tok);
				break;
			case 1:
				cpu_count = atoi(tok);
				break;
			case 2:
				if (tok[0] == '~')
					// accommodate special tests
					cpu_range = gpu_g_test_cpu_conv(tok);
				else
					cpu_range = xstrdup(tok);
				break;
			case 3:
				links = xstrdup(tok);
				break;
			case 4:
				device_file = xstrdup(tok);
				break;
			default:
				error("Malformed line: too many data fields");
				break;
			}
			i++;
			tok = strtok_r(NULL, "|", &save_ptr);
		}

		if (i != 5)
			error("Line #%d in fake_gpus.conf failed to parse!"
			      " Make sure that the line has no empty tokens and"
			      " that the format is <type>|<sys_cpu_count>|"
			      "<cpu_range>|<links>|<device_file>", line_number);

		cpu_aff_mac_bitstr = bit_alloc(cpu_count);
		if (bit_unfmt(cpu_aff_mac_bitstr, cpu_range))
			fatal("%s: bit_unfmt() failed for CPU range: %s",
			      __func__, cpu_range);

		// Add the GPU specified by the parsed line
		add_gres_to_list(gres_list_system, "gpu", 1, cpu_count,
				 cpu_range, cpu_aff_mac_bitstr, device_file,
				 type, links);
		FREE_NULL_BITMAP(cpu_aff_mac_bitstr);
		xfree(cpu_range);
		xfree(device_file);
		xfree(type);
		xfree(links);
	}
	fclose(f);
}

/*
 * Creates and returns a list of system GPUs if fake_gpus.conf exists
 * GPU system info will be artificially set to whatever fake_gpus.conf specifies
 * If fake_gpus.conf does not exist, or an error occurs, returns NULL
 * Caller is responsible for freeing the list if not NULL.
 */
static List _get_system_gpu_list_fake(void)
{
	List gres_list_system = NULL;
	struct stat config_stat;
	char *fake_gpus_file = NULL;

	/*
	 * Only add "fake" data if fake_gpus.conf exists
	 * If a file exists, read in from a file. Generate hard-coded test data
	 */
	fake_gpus_file = get_extra_conf_path("fake_gpus.conf");
	if (stat(fake_gpus_file, &config_stat) >= 0) {
		info("Adding fake system GPU data from %s", fake_gpus_file);
		gres_list_system = list_create(destroy_gres_slurmd_conf);
		_add_fake_gpus_from_file(gres_list_system, fake_gpus_file);
	}
	xfree(fake_gpus_file);
	return gres_list_system;
}

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}
extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
	gpu_plugin_fini();
	FREE_NULL_LIST(gres_devices);

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf or
 * slurm.conf.
 * In the general case, no code would need to be changed.
 */
extern int node_config_load(List gres_conf_list,
			    node_config_load_t *node_config)
{
	int rc = SLURM_SUCCESS;
	List gres_list_system = NULL;
	log_level_t log_lvl;

	/* Assume this state is caused by an scontrol reconfigure */
	if (gres_devices) {
		debug("%s: Resetting gres_devices", plugin_name);
		FREE_NULL_LIST(gres_devices);
	}

	gres_list_system = _get_system_gpu_list_fake();
	// Only query real system devices if there is no fake override
	if (!gres_list_system)
		gres_list_system = gpu_g_get_system_gpu_list(node_config);

	if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_VERBOSE;
	else
		log_lvl = LOG_LEVEL_DEBUG;
	if (gres_list_system) {
		if (list_is_empty(gres_list_system))
			log_var(log_lvl,
				"There were 0 GPUs detected on the system");
		log_var(log_lvl,
			"%s: Normalizing gres.conf with system GPUs",
			plugin_name);
		_normalize_gres_conf(gres_conf_list, gres_list_system);
		FREE_NULL_LIST(gres_list_system);

		if (!gres_conf_list || list_is_empty(gres_conf_list))
			log_var(log_lvl, "%s: Final normalized gres.conf list is empty",
				plugin_name);
		else {
			log_var(log_lvl, "%s: Final normalized gres.conf list:",
				plugin_name);
			print_gres_list(gres_conf_list, log_lvl);
		}
	}

	rc = common_node_config_load(gres_conf_list, gres_name, &gres_devices);

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
		 &already_seen, &local_inx, false, true);
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void step_set_env(char ***step_env_ptr, void *gres_ptr)
{
	static int local_inx = 0;
	static bool already_seen = false;

	_set_env(step_env_ptr, gres_ptr, 0, NULL,
		 &already_seen, &local_inx, false, false);
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
		 &already_seen, &local_inx, true, false);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void send_stepd(Buf buffer)
{
	common_send_stepd(buffer, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(Buf buffer)
{
	common_recv_stepd(buffer, &gres_devices);
}

/*
 * get data from a job's GRES data structure
 * IN job_gres_data  - job's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired
 * IN data_type - type of data to get from the job's data
 * OUT data - pointer to the data from job's GRES data structure
 *            DO NOT FREE: This is a pointer into the job's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int job_info(gres_job_state_t *job_gres_data, uint32_t node_inx,
		     enum gres_job_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * get data from a step's GRES data structure
 * IN step_gres_data  - step's GRES data structure
 * IN node_inx - zero-origin index of the node within the job's allocation
 *	for which data is desired. Note this can differ from the step's
 *	node allocation index.
 * IN data_type - type of data to get from the step's data
 * OUT data - pointer to the data from step's GRES data structure
 *            DO NOT FREE: This is a pointer into the step's data structure
 * RET - SLURM_SUCCESS or error code
 */
extern int step_info(gres_step_state_t *step_gres_data, uint32_t node_inx,
		     enum gres_step_data_type data_type, void *data)
{
	return EINVAL;
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern List get_devices(void)
{
	return gres_devices;
}

/*
 * Build record used to set environment variables as appropriate for a job's
 * prolog or epilog based GRES allocated to the job.
 */
extern gres_epilog_info_t *epilog_build_env(gres_job_state_t *gres_job_ptr)
{
	int i;
	gres_epilog_info_t *epilog_info;

	epilog_info = xmalloc(sizeof(gres_epilog_info_t));
	epilog_info->node_cnt = gres_job_ptr->node_cnt;
	epilog_info->gres_bit_alloc = xcalloc(epilog_info->node_cnt,
					      sizeof(bitstr_t *));
	for (i = 0; i < epilog_info->node_cnt; i++) {
		if (gres_job_ptr->gres_bit_alloc &&
		    gres_job_ptr->gres_bit_alloc[i]) {
			epilog_info->gres_bit_alloc[i] =
				bit_copy(gres_job_ptr->gres_bit_alloc[i]);
		}
	}

	return epilog_info;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 */
extern void epilog_set_env(char ***epilog_env_ptr,
			   gres_epilog_info_t *epilog_info, int node_inx)
{
	int dev_inx_first = -1, dev_inx_last, dev_inx;
	int env_inx = 0, i;
	gres_device_t *gres_device;
	char *dev_num_str = NULL, *sep = "";
	ListIterator iter;

	xassert(epilog_env_ptr);

	if (!epilog_info)
		return;

	if (!gres_devices)
		return;

	if (epilog_info->node_cnt == 0)	/* no_consume */
		return;

	if (node_inx > epilog_info->node_cnt) {
		error("%s: %s: bad node index (%d > %u)", plugin_type, __func__,
		      node_inx, epilog_info->node_cnt);
		return;
	}

	if (*epilog_env_ptr) {
		for (env_inx = 0; (*epilog_env_ptr)[env_inx]; env_inx++)
			;
		xrealloc(*epilog_env_ptr, sizeof(char *) * (env_inx + 3));
	} else {
		*epilog_env_ptr = xcalloc(3, sizeof(char *));
	}

	if (epilog_info->gres_bit_alloc &&
	    epilog_info->gres_bit_alloc[node_inx]) {
		dev_inx_first = bit_ffs(epilog_info->gres_bit_alloc[node_inx]);
	}
	if (dev_inx_first >= 0)
		dev_inx_last = bit_fls(epilog_info->gres_bit_alloc[node_inx]);
	else
		dev_inx_last = -2;
	for (dev_inx = dev_inx_first; dev_inx <= dev_inx_last; dev_inx++) {
		if (!bit_test(epilog_info->gres_bit_alloc[node_inx], dev_inx))
			continue;
		/* Translate bits to device number, may differ */
		i = -1;
		iter = list_iterator_create(gres_devices);
		while ((gres_device = list_next(iter))) {
			i++;
			if (i == dev_inx) {
				xstrfmtcat(dev_num_str, "%s%d",
					   sep,gres_device->dev_num);
				sep = ",";
				break;
			}
		}
		list_iterator_destroy(iter);
	}
	if (dev_num_str) {
		xstrfmtcat((*epilog_env_ptr)[env_inx++],
			   "CUDA_VISIBLE_DEVICES=%s", dev_num_str);
		xstrfmtcat((*epilog_env_ptr)[env_inx++],
			   "GPU_DEVICE_ORDINAL=%s", dev_num_str);
		xfree(dev_num_str);
	}

	return;
}
