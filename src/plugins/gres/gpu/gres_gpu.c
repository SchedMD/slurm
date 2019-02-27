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

extern void step_configure_hardware(bitstr_t *usable_gpus, char *tres_freq)
{
	gpu_g_step_config_hardware(usable_gpus, tres_freq);
}

extern void step_unconfigure_hardware(void)
{
	gpu_g_step_unconfig_hardware();
}

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job)
{
	char *global_list = NULL, *local_list = NULL, *slurm_env_var = NULL;

	if (is_job)
		slurm_env_var = "SLURM_JOB_GRES";
	else
		slurm_env_var = "SLURM_STEP_GRES";

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
		xfree(local_list);
		*already_seen = true;
	}
}

/*
 * Takes a string of form "sdf [x]sd fsf " and returns a new string "x"
 * xfree the returned string
 * Or returns null on failure
 */
static char *_strip_brackets_xmalloc(char *str)
{
	int i = 0;
	// index to start of string
	int start = 0;
	// index to null char at end of string
	int end;
	bool done = false;

	if (str == NULL)
		return NULL;

	// Original location of the null pointer
	end = strlen(str);

	while (done == false) {
		if (!str[i]) {
			done = true;
		} else if (str[i] == '[') {
			start = i + 1;
		} else if (str[i] == ']') {
			end = i;
			done = true;
		}
		i++;
	}

	// if (start == 0 || end == 0) {
	// 	info("Warning: String did not have brackets");
	// }
	// Overwrite the trailing ] with a null char,
	if (str[end])
		str[end] = '\0';
	// duplicate the string
	return xstrdup(&str[start]);
}

/*
 * Returns 0 if range string a and b are equal, else returns 1.
 *
 * This normalizes ranges, so that "0-2" and "0,1,2" are considered equal.
 * This also strips brackets for comparison, e.g. "[0-2]"
 */
static int _compare_number_range_str(char *a, char *b)
{
	hostlist_t hl_a, hl_b;
	char *a_range_brackets;
	char *b_range_brackets;
	char *a_range;
	char *b_range;
	int rc = 0;

	// Normalize a and b, so e.g. 0-2 == 0,1,2
	hl_a = hostlist_create(a);
	hl_b = hostlist_create(b);
	a_range_brackets = (char *) hostlist_ranged_string_xmalloc(hl_a);
	b_range_brackets = (char *) hostlist_ranged_string_xmalloc(hl_b);
	// If there are brackets, remove them
	a_range = _strip_brackets_xmalloc(a_range_brackets);
	b_range = _strip_brackets_xmalloc(b_range_brackets);

	if (xstrcmp(a_range, b_range))
		rc = 1;
	hostlist_destroy(hl_a);
	hostlist_destroy(hl_b);
	xfree(a_range_brackets);
	xfree(b_range_brackets);
	xfree(a_range);
	xfree(b_range);
	return rc;
}

/*
 * Checks to see if the number range string specified by key is a subset of
 * the number range specified by x
 */
static int _key_in_number_range_str(char *x, char *key)
{
	hostlist_t hl_x, hl_key;
	char *key_name;
	int rc = 0;

	// Normalize, so e.g. 0-2 == 0,1,2. This true? --> tty0,tty1 == tty[0-1]
	hl_x = hostlist_create(x);
	hl_key = hostlist_create(key);

	// Linearly check to see if all of key's devices are in x's devices
	while ((key_name = hostlist_shift(hl_key))) {
		if (hostlist_find(hl_x, key_name) < 0)
			rc = 1;	/* Not found */
		free(key_name);
		if (rc == 1)
			break;
	}

	hostlist_destroy(hl_x);
	hostlist_destroy(hl_key);
	return rc;
}

/*
 * Returns 1 if (x==key); else returns 0.
 */
static int _find_gres_in_list(void *x, void *key)
{
	gres_slurmd_conf_t *item_a = (gres_slurmd_conf_t *) x;
	gres_slurmd_conf_t *item_b = (gres_slurmd_conf_t *) key;

	// Check if type names are equal
	if (xstrcmp(item_a->type_name, item_b->type_name))
		return 0;

	// Check if cpus are equal
	if (_compare_number_range_str(item_a->cpus, item_b->cpus))
		return 0;

	// Check if links are equal
	if (_compare_number_range_str(item_a->links, item_b->links))
		return 0;
	// Found!
	return 1;
}

/*
 * Returns 1 if a is a subset of b or vice versa; else returns 0.
 */
static int _find_gres_device_file_in_list(void *a, void *b)
{
	gres_slurmd_conf_t *item_a = (gres_slurmd_conf_t *) a;
	gres_slurmd_conf_t *item_b = (gres_slurmd_conf_t *) b;

	if (item_a->count > item_b->count) {
		if (_key_in_number_range_str(item_a->file, item_b->file))
			return 0;
	} else if (item_a->count < item_b->count) {
		if (_key_in_number_range_str(item_b->file, item_a->file))
			return 0;
	} else {
		// a and b have the same number of devices
		if (item_a->count == 1) {
			// Only a strcmp should be necessary for single devices
			if (xstrcmp(item_a->file, item_b->file))
				return 0;
		} else {
			if (_key_in_number_range_str(item_a->file, item_b->file)
					!= 0)
				return 0;
		}
	}

	// Found!
	return 1;
}

/*
 * Like _find_gres_in_list, except also checks to make sure the device file
 * range specified by the record with the smaller device count is a subset of
 * the device file range specified by the record with the larger device count.
 * e.g. /dev/tty0 == /dev/tty0 or /dev/tty[0-1] is in /dev/tty[0-2]
 * Returns 1 if a is a subset of b or vice versa; else returns 0.
 */
static int _find_gres_device_in_list(void *a, void *b)
{
	if (!a || !b)
		return 0;

	// Make sure we have the right record before checking the devices
	if (!_find_gres_in_list(a, b))
		return 0;

	if (!_find_gres_device_file_in_list(a, b))
		return 0;

	// Found!
	return 1;
}

// Duplicate an existing gres_slurmd_conf_t record and add it to a list
static void _add_record_to_gres_list(List gres_list, gres_slurmd_conf_t *record)
{
	add_gres_to_list(gres_list, record->name, record->count,
			 record->cpu_cnt, record->cpus, record->file,
			 record->type_name, record->links, record->ignore);
}

/*
 * See if a gres gpu device file already exists in a gres list
 * If the device exists, returns the gres record that contains it, else returns
 * null.
 *
 * "device file" == just gres_slurmd_conf_t->file
 */
static void *_device_file_exists_in_list(List gres_list,
					 gres_slurmd_conf_t *gres_record)
{
	if (gres_list == NULL)
		return NULL;
	return list_find_first(gres_list, _find_gres_device_file_in_list,
			       gres_record);
}

/*
 * See if a gres gpu device exists in a gres list
 * If the device exists, returns the gres record that contains it, else returns
 * null.
 *
 * "device" == type:cpus:links:file
 */
static void *_device_exists_in_list(List gres_list,
				    gres_slurmd_conf_t *gres_record)
{
	if (gres_list == NULL)
		return NULL;
	return list_find_first(gres_list, _find_gres_device_in_list,
			       gres_record);
}

/*
 * Add GRES record and device to gres_list_out only if it doesn't exist already
 * in gres_list_out.
 * Also, only adds records that are NOT ignored
 *
 * gres_list_out	(OUT) The list to add to
 * gres_record		(IN) The record to add
 */
static void _add_unique_gres_to_list(List gres_list_out,
				     gres_slurmd_conf_t *gres_record)
{
	if (gres_record->ignore) {
		debug3("Omitting adding this record due to ignore=true");
		print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		return;
	}
	if (!gres_record->file) {
		debug3("Omitting adding this record due to File=NULL");
		print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		return;
	}

	// If device file is already in the list, ignore it
	if (_device_file_exists_in_list(gres_list_out, gres_record)) {
		debug3("GRES device file is already specified! Ignoring");
		print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		return;
	}

	debug3("Adding the following GRES device:");
	print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
	_add_record_to_gres_list(gres_list_out, gres_record);
}

/*
 * Add all unique, unignored devices specified by the GRES records in
 * gres_list_in only if they don't already exist in gres_list_out.
 * Also checks to make sure gres_list_in doesn't add any records to
 * gres_list_out that are specifically ignored in gres_list_ignore.
 * I.e.:
 *     gres_list_out = gres_list_in & (~gres_list_ignore)
 *
 * gres_list_out	(OUT) The output GRES list
 * gres_list_ignore	(IN) A list of GRES ignore records that will apply
 * 			against gres_list_in
 * gres_list_in		(IN) A list of GRES records to add
 */
static void _add_unique_gres_list_to_list_ignore(List gres_list_out,
						 List gres_list_ignore,
						 List gres_list_in)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_in;
	gres_slurmd_conf_t *gres_ignore;

	if (!gres_list_in || list_is_empty(gres_list_in))
		return;

	itr = list_iterator_create(gres_list_in);
	while ((gres_in = list_next(itr))) {
		// Make sure current device isn't ignored in list compare
		gres_ignore = _device_file_exists_in_list(gres_list_ignore,
							  gres_in);
		if (gres_ignore && gres_ignore->ignore) {
			debug3("This GRES device:");
			print_gres_conf(gres_in, LOG_LEVEL_DEBUG3);
			debug3("is ignored by this record:");
			print_gres_conf(gres_ignore, LOG_LEVEL_DEBUG3);
			continue;
		}

		_add_unique_gres_to_list(gres_list_out, gres_in);
	}
	list_iterator_destroy(itr);
}

/*
 * Add all unique, unignored devices specified by the GRES records in
 * gres_list_in only if they don't already exist in gres_list_out.
 */
static void _add_unique_gres_list_to_list(List gres_list_out,
					  List gres_list_in)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record;

	if (!gres_list_in || list_is_empty(gres_list_in))
		return;

	itr = list_iterator_create(gres_list_in);
	while ((gres_record = list_next(itr))) {
		_add_unique_gres_to_list(gres_list_out, gres_record);
	}
	list_iterator_destroy(itr);
}

/* Given a file name return its numeric suffix */
static int _file_inx(char *fname)
{
	int i, len, mult = 1, num, val = 0;

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
static int _sort_gpu_by_file(void *x, void *y)
{
	gres_slurmd_conf_t *gres_record1 = *(gres_slurmd_conf_t **) x;
	gres_slurmd_conf_t *gres_record2 = *(gres_slurmd_conf_t **) y;
	int val1, val2;

	val1 = _file_inx(gres_record1->file);
	val2 = _file_inx(gres_record2->file);

	return (val1 - val2);
}

/*
 * Takes the gres.conf records and gpu devices detected on the node and either
 * merges them together or warns where they are different.
 * This function handles duplicate information specified in gres.conf
 *
 * gres_list_conf: (in/out) The GRES records as parsed from gres.conf
 * gres_list_system: (in) The gpu devices detected by the system. Note: This
 * 		          list may get mangled, so don't use afterwards.
 *
 * NOTES:
 * gres_list_conf_single: Same as gres_list_conf, except broken down so each
 * 			  GRES record has only one device file.
 *
 * Remember, this code is run for each node on the cluster
 * The records need to be unique, keyed off of:
 * 	*type
 * 	*cores/cpus
 * 	*links
 */
static void _normalize_gres_conf(List gres_list_conf, List gres_list_system)
{
	ListIterator itr_conf, itr_single, itr_system;
	gres_slurmd_conf_t *gres_record;
	List gres_list_conf_single, gres_list_gpu, gres_list_non_gpu;
	bool use_system_detected = true;
	bool log_zero = true;

	if (gres_list_conf == NULL) {
		error("%s: gres_list_conf is NULL. This shouldn't happen",
		      __func__);
		return;
	}

	gres_list_conf_single = list_create(destroy_gres_slurmd_conf);
	gres_list_gpu = list_create(destroy_gres_slurmd_conf);
	gres_list_non_gpu = list_create(destroy_gres_slurmd_conf);

	debug2("gres_list_conf:");
	print_gres_list(gres_list_conf, LOG_LEVEL_DEBUG2);

	// Break down gres_list_conf into 1 device per record
	itr_conf = list_iterator_create(gres_list_conf);
	while ((gres_record = list_next(itr_conf))) {
		int i, file_count = 0;
		hostlist_t hl;
		char **file_array;
		char *hl_name;

		// Just move this GRES record if it's not a GPU GRES
		if (xstrcasecmp(gres_record->name, "gpu")) {
			debug2("%s: preserving original `%s` GRES record",
			       __func__, gres_record->name);
			add_gres_to_list(gres_list_non_gpu,
					 gres_record->name,
					 gres_record->count,
					 gres_record->cpu_cnt,
					 gres_record->cpus,
					 gres_record->file,
					 gres_record->type_name,
					 gres_record->links,
					 gres_record->ignore);
			continue;
		}
		if (gres_record->cpus) {
			int offset = 0;
			if (gres_record->cpus[0] == '[')
				offset = 1;
			if ((gres_record->cpus[offset] < '0') ||
			    (gres_record->cpus[offset] > '9')) {
				error("%s: gres/gpu has invalid \"CPUs\" specification (%s)",
				      gres_record->cpus, __func__);
				xfree(gres_record->cpus);
			}
		}
		if (!gres_record->file) {
			error("%s: gres/gpu lacks \"File\" specification",
			      __func__);
		}
		if (gres_record->count == 0) {
			if (log_zero) {
				info("%s: gres.conf record has zero count",
				     __func__);
				log_zero = false;
			}
			// Use system-detected devices
			continue;
		}
		// Use system-detected if there are only ignore records in conf
		if (use_system_detected && (gres_record->ignore == false)) {
			use_system_detected = false;
		}

		if (gres_record->count == 1) {
			if (_device_exists_in_list(gres_list_conf_single,
						   gres_record))
				// Duplicate from single! Do not add
				continue;

			// Add device from single record
			add_gres_to_list(gres_list_conf_single,
					 gres_record->name, 1,
					 gres_record->cpu_cnt,
					 gres_record->cpus,
					 gres_record->file,
					 gres_record->type_name,
					 gres_record->links,
					 gres_record->ignore);
			continue;
		}
		// count > 1; Break down record into individual devices
		hl = hostlist_create(gres_record->file);
		// Create an array of file name pointers
		file_array = (char **) xmalloc(gres_record->count
				     * sizeof(char *));
		while ((hl_name = hostlist_shift(hl))) {
			// Create a single gres conf record to compare
			gres_slurmd_conf_t *temp_gres_conf =
					xmalloc(sizeof(gres_slurmd_conf_t));
			temp_gres_conf->type_name = gres_record->type_name;
			temp_gres_conf->cpus = gres_record->cpus;
			temp_gres_conf->links = gres_record->links;
			temp_gres_conf->file = hl_name;
			if (_device_exists_in_list(gres_list_conf_single,
						   temp_gres_conf)) {
				// Duplicate from multiple! Do not add
			} else {
				file_array[file_count] = xstrdup(hl_name);
				file_count++;
			}
			xfree(temp_gres_conf);
			free(hl_name);
		}
		hostlist_destroy(hl);

		// Break down file into individual file names
		// Create an array of these file names, and index off of i
		for (i = 0; i < file_count; ++i) {
			add_gres_to_list(gres_list_conf_single,
					 gres_record->name, 1,
					 gres_record->cpu_cnt,
					 gres_record->cpus, file_array[i],
					 gres_record->type_name,
					 gres_record->links,
					 gres_record->ignore);
			xfree(file_array[i]);
		}
		xfree(file_array);
	}
	list_iterator_destroy(itr_conf);

	// Warn about mismatches between detected devices and gres.conf
	debug2("GPUs specified in gres.conf that are found on node:");
	itr_single = list_iterator_create(gres_list_conf_single);
	while ((gres_record = list_next(itr_single))) {
		if (_device_exists_in_list(gres_list_system, gres_record))
			print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
	}

	debug2("GPUs specified in gres.conf that are NOT found on node:");
	list_iterator_reset(itr_single);
	while ((gres_record = list_next(itr_single))) {
		if (!_device_exists_in_list(gres_list_system, gres_record))
			print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
	}
	list_iterator_destroy(itr_single);

	debug2("GPUs detected on the node, but not specified in gres.conf:");
	if (gres_list_system) {
		itr_system = list_iterator_create(gres_list_system);
		while ((gres_record = list_next(itr_system))) {
			if (!_device_exists_in_list(gres_list_conf_single,
						    gres_record))
				print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
		}
		list_iterator_destroy(itr_system);
	}

	debug2("Adding unique USER-SPECIFIED devices:");
	_add_unique_gres_list_to_list(gres_list_gpu, gres_list_conf_single);

	// If gres.conf is empty or only ignored devices, fill with system info
	if (use_system_detected) {
		debug2("Adding unique unignored SYSTEM-DETECTED devices:");
		// gres_list_conf_single only contains ignore records here
		_add_unique_gres_list_to_list_ignore(gres_list_gpu,
						     gres_list_conf_single,
						     gres_list_system);
	}

	debug2("gres_list_gpu:");
	print_gres_list(gres_list_gpu, LOG_LEVEL_DEBUG2);

	/*
	 * Free old records in gres_list_conf
	 * Replace gres_list_conf with gres_list_gpu
	 * Note: list_transfer won't work because lists don't have same delFunc
	 * Note: list_append_list won't work because delFunc is non-null
	 */
	list_flush(gres_list_conf);
	list_sort(gres_list_gpu, _sort_gpu_by_file);
	while ((gres_record = list_pop(gres_list_gpu))) {
		list_append(gres_list_conf, gres_record);
	}
	while ((gres_record = list_pop(gres_list_non_gpu))) {
		list_append(gres_list_conf, gres_record);
	}

	FREE_NULL_LIST(gres_list_conf_single);
	FREE_NULL_LIST(gres_list_gpu);
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
			if (tok[0] == '_' || xstrcmp(tok, "(null)") == 0) {
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

		// Add the GPU specified by the parsed line
		add_gres_to_list(gres_list_system, "gpu", 1, cpu_count,
				 cpu_range, device_file, type, links, false);
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
	FREE_NULL_LIST(gres_devices);

	return SLURM_SUCCESS;
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
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
		debug("Resetting gres_devices");
		FREE_NULL_LIST(gres_devices);
	}

	gpu_g_get_system_gpu_list(node_config);

	gres_list_system = _get_system_gpu_list_fake();
	// Only query real system devices if there is no fake override
	if (!gres_list_system)
		gres_list_system = gpu_g_get_system_gpu_list(node_config);

	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_DEBUG;
	if (gres_list_system) {
		if (list_is_empty(gres_list_system))
			log_var(log_lvl,
				"There were 0 GPUs detected on the system");
		log_var(log_lvl,
			"%s: Normalizing gres.conf with system devices",
			plugin_name);
		_normalize_gres_conf(gres_conf_list, gres_list_system);
		FREE_NULL_LIST(gres_list_system);

		log_var(log_lvl, "%s: Final normalized gres.conf list:",
			plugin_name);
		print_gres_list(gres_conf_list, log_lvl);
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
extern void send_stepd(int fd)
{
	common_send_stepd(fd, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void recv_stepd(int fd)
{
	common_recv_stepd(fd, &gres_devices);
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
