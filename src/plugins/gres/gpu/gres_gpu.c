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
#include "src/common/gres.h"
#include "src/common/list.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xstring.h"

#include "../common/gres_common.h"

#ifdef HAVE_NVML
#include <nvml.h>
#endif

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
static uint64_t	debug_flags		= 0;
static char	*gres_name		= "gpu";
static List	gres_devices		= NULL;

#define GPU_LOW		((unsigned int) -1)
#define GPU_MEDIUM	((unsigned int) -2)
#define GPU_HIGH_M1	((unsigned int) -3)
#define GPU_HIGH	((unsigned int) -4)
#define GPU_MODE_FREQ	1
#define GPU_MODE_MEM	2
#define MAX_CPUS	0x8000
#define ULONG_BYTES	(sizeof(unsigned long))
#define ULONG_BITS	(ULONG_BYTES * 8)
/*
 * The # of unsigned longs needed to accommodate a bitmask array capable
 * of representing MAX_CPUS cpus (will vary if 32-bit or 64-bit)
 * E.g. for a 130 CPU 64-bit machine: (130 + 63) / 64 = 3.02
 * -> Integer division floor -> 3 ulongs to represent 130 CPUs
 */
#define CPU_SET_SIZE	((MAX_CPUS + (ULONG_BITS-1)) / ULONG_BITS)
#define NVLINK_SELF	-1
#define NVLINK_NONE	0
#define FREQS_SIZE	512
#define FREQS_CONCISE	5 // This must never be smaller than 5, or error

#ifdef HAVE_NVML
static bitstr_t	*saved_gpus		= NULL;

/*
 * Initialize the NVML library. This takes a few seconds
 */
static void _nvml_init(void)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlInit();
	END_TIMER;
	debug3("nvmlInit() took %ld microseconds", DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS)
		error("Failed to initialize NVML: %s",
		      nvmlErrorString(nvml_rc));
	else
		info("Successfully initialized NVML");
}

/*
 * Undo _nvml_init
 */
static void _nvml_shutdown(void)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlShutdown();
	END_TIMER;
	debug3("nvmlShutdown() took %ld microseconds", DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS)
		error("Failed to shut down NVML: %s", nvmlErrorString(nvml_rc));
	else
		info("Successfully shut down NVML");
}

static unsigned int _xlate_freq_value(char *gpu_freq)
{
	unsigned int value;

	if ((gpu_freq[0] < '0') && (gpu_freq[0] > '9'))
		return 0;	/* Not a numeric value */
	value = strtoul(gpu_freq, NULL, 10);
	return value;
}

static unsigned int _xlate_freq_code(char *gpu_freq)
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

	debug("%s: %s: Invalid job GPU frequency (%s)",
	      plugin_type, __func__, gpu_freq);
	return 0;	/* Bad user input */
}

static void _parse_gpu_freq2(char *gpu_freq, unsigned int *gpu_freq_code,
			     unsigned int *gpu_freq_value,
			     unsigned int *mem_freq_code,
			     unsigned int *mem_freq_value, bool *verbose_flag)
{
	char *tmp, *tok, *sep, *save_ptr = NULL;
	if (!gpu_freq || !gpu_freq[0])
		return;
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
			} else {
				debug("%s: %s: Invalid job device frequency type: %s",
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

static void _parse_gpu_freq(char *gpu_freq, unsigned int *gpu_freq_num,
			    unsigned int *mem_freq_num, bool *verbose_flag)
{
	unsigned int def_gpu_freq_code = 0, def_gpu_freq_value = 0;
	unsigned int def_mem_freq_code = 0, def_mem_freq_value = 0;
	unsigned int job_gpu_freq_code = 0, job_gpu_freq_value = 0;
	unsigned int job_mem_freq_code = 0, job_mem_freq_value = 0;
	char *def_freq;

	_parse_gpu_freq2(gpu_freq, &job_gpu_freq_code, &job_gpu_freq_value,
			 &job_mem_freq_code, &job_mem_freq_value, verbose_flag);

	// Defaults to high for both mem and gfx
	def_freq = slurm_get_gpu_freq_def();
	_parse_gpu_freq2(def_freq, &def_gpu_freq_code, &def_gpu_freq_value,
			 &def_mem_freq_code, &def_mem_freq_value, verbose_flag);
	xfree(def_freq);

	if (job_gpu_freq_code)
		*gpu_freq_num = job_gpu_freq_code;
	else if (job_gpu_freq_value)
		*gpu_freq_num = job_gpu_freq_value;
	else if (def_gpu_freq_code)
		*gpu_freq_num = def_gpu_freq_code;
	else if (def_gpu_freq_value)
		*gpu_freq_num = def_gpu_freq_value;

	if (job_mem_freq_code)
		*mem_freq_num = job_mem_freq_code;
	else if (job_mem_freq_value)
		*mem_freq_num = job_mem_freq_value;
	else if (def_mem_freq_code)
		*mem_freq_num = def_mem_freq_code;
	else if (def_mem_freq_value)
		*mem_freq_num = def_mem_freq_value;
}

#endif // HAVE_NVML

/*
 * Sort an array of unsigned ints in descending order using the bubble sort
 * algorithm. If the array is already sorted, then this will only take O(n).
 * If it's perfectly unsorted, this will take O(n^2).
 *
 * arr		(IN) An array of frequencies to sort
 * size		(IN) The number of frequency elements in arr
 */
static void _bubble_sort_descending(unsigned int *arr, unsigned int size)
{
	bool sorted = false;
	unsigned int i, count;
	if (!arr) {
		error("%s: array is null", __func__);
		return;
	}
	if (size <= 0) {
		error("%s: array size is <= 0", __func__);
		return;
	}
	count = size - 1;

	do {
		// Temporarily set to true to see if array is sorted
		sorted = true;
		// for each pair of adjacent elements
		for (i = 0; i < count; i++) {
			// if the pair is not sorted DESC, swap them
			if (arr[i] < arr[i + 1]) {
				unsigned int tmp = arr[i];
				arr[i] = arr[i + 1];
				arr[i + 1] = tmp;
				// reset sorted, because we swapped
				sorted = false;
			}
		}
		// Every pass requires processing one less element
		count--;
	} while (!sorted);
}

#ifdef HAVE_NVML

/*
 * Get the handle to the GPU for the passed index
 *
 * index 	(IN) The GPU index (corresponds to PCI Bus ID order)
 * device	(OUT) The device handle
 *
 * Returns true if successful, false if not
 */
static bool _nvml_get_handle(int index, nvmlDevice_t *device)
{
	nvmlReturn_t nvml_rc;
	nvml_rc = nvmlDeviceGetHandleByIndex(index, device);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get device handle for GPU %d: %s", index,
		      nvmlErrorString(nvml_rc));
		return false;
	}
	return true;
}

/*
 * Get all possible memory frequencies for the device
 *
 * device		(IN) The device handle
 * mem_freqs_size	(IN/OUT) The size of the mem_freqs array; this will be
 * 			overwritten with the number of memory freqs found.
 * mem_freqs		(OUT) The possible memory frequencies, sorted in
 * 			descending order
 *
 * Return true if successful, false if not.
 */
static bool _nvml_get_mem_freqs(nvmlDevice_t *device,
				unsigned int *mem_freqs_size,
				unsigned int *mem_freqs)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceGetSupportedMemoryClocks(*device, mem_freqs_size,
						     mem_freqs);
	END_TIMER;
	debug3("nvmlDeviceGetSupportedMemoryClocks() took %ld microseconds",
	       DELTA_TIMER);

	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to get supported memory frequencies for the "
		      "GPU : %s", __func__, nvmlErrorString(nvml_rc));
		return false;
	}

	_bubble_sort_descending(mem_freqs, *mem_freqs_size);

	if ((*mem_freqs_size > 1) &&
	    (mem_freqs[0] <= mem_freqs[(*mem_freqs_size)-1])) {
		error("%s: mem frequencies are not stored in descending order!",
		      __func__);
		return false;
	}
	return true;
}

/*
 * Get all possible graphics frequencies for the device
 *
 * device		(IN) The device handle
 * mem_freq		(IN) The memory frequency to get graphics freqs for.
 * gfx_freqs_size	(IN/OUT) The size of the gfx_freqs array; this will
 * 			be overwritten with the number of graphics freqs found.
 * gfx_freqs		(OUT) The possible graphics frequencies, sorted in
 * 			descending order
 *
 * Return true if successful, false if not.
 */
static bool _nvml_get_gfx_freqs(nvmlDevice_t *device,
				unsigned int mem_freq,
				unsigned int *gfx_freqs_size,
				unsigned int *gfx_freqs)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceGetSupportedGraphicsClocks(*device, mem_freq,
						       gfx_freqs_size,
						       gfx_freqs);
	END_TIMER;
	debug3("nvmlDeviceGetSupportedGraphicsClocks() took %ld microseconds",
	       DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to get supported graphics frequencies for the"
		      " GPU at mem frequency %u: %s", __func__, mem_freq,
		      nvmlErrorString(nvml_rc));
		return false;
	}

	_bubble_sort_descending(gfx_freqs, *gfx_freqs_size);

	if ((*gfx_freqs_size > 1) &&
	    (gfx_freqs[0] <= gfx_freqs[(*gfx_freqs_size)-1])) {
		error("%s: gfx frequencies are not stored in descending order!",
		      __func__);
		return false;
	}
	return true;
}

/*
 * Print out all possible graphics frequencies for the given device and mem
 * freq. If there are many frequencies, only prints out a few.
 *
 * device		(IN) The device handle
 * mem_freq		(IN) The memory frequency to get graphics freqs for.
 * gfx_freqs_size	(IN) The size of the gfx_freqs array
 * gfx_freqs		(IN) A preallocated empty array of size gfx_freqs_size
 * 			to fill with possible graphics frequencies
 * l			(IN) The log level at which to print
 *
 * NOTE: The contents of gfx_freqs will be modified during use.
 */
static void _nvml_print_gfx_freqs(nvmlDevice_t *device, unsigned int mem_freq,
				  unsigned int gfx_freqs_size,
				  unsigned int *gfx_freqs, log_level_t l)
{
	unsigned int size = gfx_freqs_size;
	bool concise = false;
	unsigned int i;

	if (!_nvml_get_gfx_freqs(device, mem_freq, &size, gfx_freqs))
		return;

	if (size > FREQS_CONCISE)
		concise = true;

	log_var(l, "        Possible GPU Graphics Frequencies (%u):", size);
	log_var(l, "        ---------------------------------");
	if (!concise) {
		for (i = 0; i < size; ++i) {
			log_var(l, "          *%u MHz [%u]", gfx_freqs[i], i);
		}
		return;
	}
	// first, next, ..., middle, ..., penultimate, last
	log_var(l, "          *%u MHz [0]", gfx_freqs[0]);
	log_var(l, "          *%u MHz [1]", gfx_freqs[1]);
	log_var(l, "          ...");
	log_var(l, "          *%u MHz [%u]", gfx_freqs[(size - 1) / 2],
	     (size - 1) / 2);
	log_var(l, "          ...");
	log_var(l, "          *%u MHz [%u]", gfx_freqs[size - 2], size - 2);
	log_var(l, "          *%u MHz [%u]", gfx_freqs[size - 1], size - 1);
}

/*
 * Print out all possible memory and graphics frequencies for the given device.
 * If there are more than FREQS_SIZE frequencies, prints a summary instead
 *
 * device	(IN) The device handle
 * l		(IN) The log level at which to print
 */
static void _nvml_print_freqs(nvmlDevice_t *device, log_level_t l)
{
	unsigned int mem_size = FREQS_SIZE;
	unsigned int mem_freqs[FREQS_SIZE] = {0};
	unsigned int gfx_freqs[FREQS_SIZE] = {0};
	unsigned int i;
	bool concise = false;

	if (!_nvml_get_mem_freqs(device, &mem_size, mem_freqs))
		return;

	if (mem_size > FREQS_CONCISE)
		concise = true;

	log_var(l, "Possible GPU Memory Frequencies (%u):", mem_size);
	log_var(l, "-------------------------------");
	if (concise) {
		// first, next, ..., middle, ..., penultimate, last
		unsigned int tmp;
		log_var(l, "    *%u MHz [0]", mem_freqs[0]);
		_nvml_print_gfx_freqs(device, mem_freqs[0], FREQS_SIZE,
				      gfx_freqs, l);
		log_var(l, "    *%u MHz [1]", mem_freqs[1]);
		_nvml_print_gfx_freqs(device, mem_freqs[1], FREQS_SIZE,
				      gfx_freqs, l);
		log_var(l, "    ...");
		tmp = (mem_size - 1) / 2;
		log_var(l, "    *%u MHz [%u]", mem_freqs[tmp], tmp);
		_nvml_print_gfx_freqs(device, mem_freqs[tmp], FREQS_SIZE,
				      gfx_freqs, l);
		log_var(l, "    ...");
		tmp = mem_size - 2;
		log_var(l, "    *%u MHz [%u]", mem_freqs[tmp], tmp);
		_nvml_print_gfx_freqs(device, mem_freqs[tmp], FREQS_SIZE,
				      gfx_freqs, l);
		tmp = mem_size - 1;
		log_var(l, "    *%u MHz [%u]", mem_freqs[tmp], tmp);
		_nvml_print_gfx_freqs(device, mem_freqs[tmp], FREQS_SIZE,
				      gfx_freqs, l);
		return;
	}

	for (i = 0; i < mem_size; ++i) {
		log_var(l,"    *%u MHz [%u]", mem_freqs[i], i);
		_nvml_print_gfx_freqs(device, mem_freqs[i], FREQS_SIZE,
				      gfx_freqs, l);
	}
}

/*
 * Convert frequency to nearest valid frequency found in frequency array
 *
 * freq		(IN/OUT) The frequency to check, in MHz. Also the output, if
 * 		it needs to be changed.
 * freqs_size	(IN) The size of the freqs array
 * freqs	(IN) An array of frequency values in MHz, sorted highest to
 * 		lowest
 * log_lvl	(IN) The log level at which to print
 *
 * Inspired by src/common/cpu_frequency#_cpu_freq_freqspec_num()
 */
static void _get_nearest_freq(unsigned int *freq, unsigned int freqs_size,
			      unsigned int *freqs, log_level_t log_lvl)
{
	unsigned int i;

	if (!freq || !(*freq)) {
		log_var(log_lvl, "%s: No frequency supplied", __func__);
		return;
	}
	if (!freqs || !(*freqs)) {
		log_var(log_lvl, "%s: No frequency list supplied", __func__);
		return;
	}
	if (freqs_size <= 0) {
		log_var(log_lvl, "%s: Frequency list is empty", __func__);
		return;
	}

	// Check for special case values; freqs is sorted in descending order
	switch ((*freq)) {
	case GPU_LOW:
		*freq = freqs[freqs_size - 1];
		debug2("Frequency GPU_LOW: %u MHz", *freq);
		return;

	case GPU_MEDIUM:
		*freq = freqs[(freqs_size - 1) / 2];
		debug2("Frequency GPU_MEDIUM: %u MHz", *freq);
		return;

	case GPU_HIGH_M1:
		if (freqs_size == 1)
			*freq = freqs[0];
		else
			*freq = freqs[1];
		debug2("Frequency GPU_HIGH_M1: %u MHz", *freq);
		return;

	case GPU_HIGH:
		*freq = freqs[0];
		debug2("Frequency GPU_HIGH: %u MHz", *freq);
		return;

	default:
		debug2("Freq is not a special case. Continue...");
		break;
	}

	/* check if freq is out of bounds of freqs */
	if (*freq > freqs[0]) {
		log_var(log_lvl, "Rounding requested frequency %u MHz down to "
			"%u MHz (highest available)", *freq, freqs[0]);
		*freq = freqs[0];
		return;
	} else if (*freq < freqs[freqs_size - 1]) {
		log_var(log_lvl, "Rounding requested frequency %u MHz up to %u "
			"MHz (lowest available)", *freq, freqs[freqs_size - 1]);
		*freq = freqs[freqs_size - 1];
		return;
	}

	/* check for frequency, and round up if no exact match */
	for (i = 0; i < freqs_size - 1;) {
		if (*freq == freqs[i])
			// No change necessary
			debug2("No change necessary. Freq: %u MHz", *freq);
			return;
		i++;
		/*
		 * Step down to next element to round up.
		 * Safe to advance due to bounds checks above here
		 */
		if (*freq > freqs[i]) {
			log_var(log_lvl, "Rounding requested frequency %u MHz "
				"up to %u MHz (next available)", *freq,
				freqs[i - 1]);
			*freq = freqs[i - 1];
			return;
		}
	}
	error("%s: Got to the end of the function. This shouldn't happen. Freq:"
	      " %u MHz", __func__, *freq);
}

/*
 * Get the nearest valid memory and graphics clock frequencies
 *
 * device		(IN) The NVML GPU device handle
 * mem_freq		(IN/OUT) The requested memory frequency, in MHz. This
 * 			will be overwritten with the output value, if different.
 * gfx_freq 		(IN/OUT) The requested graphics frequency, in MHz. This
 * 			will be overwritten with the output value, if different.
 * log_lvl		(IN) The log level at which to print
 */
static void _nvml_get_nearest_freqs(nvmlDevice_t *device,
				    unsigned int *mem_freq,
				    unsigned int *gfx_freq, log_level_t log_lvl)
{
	unsigned int mem_freqs[FREQS_SIZE] = {0};
	unsigned int mem_freqs_size = FREQS_SIZE;
	unsigned int gfx_freqs[FREQS_SIZE] = {0};
	unsigned int gfx_freqs_size = FREQS_SIZE;

	// Get the memory frequencies
	if (!_nvml_get_mem_freqs(device, &mem_freqs_size, mem_freqs))
		return;

	// Set the nearest valid memory frequency for the requested frequency
	_get_nearest_freq(mem_freq, mem_freqs_size, mem_freqs, log_lvl);

	// Get the graphics frequencies at this memory frequency
	if (!_nvml_get_gfx_freqs(device, *mem_freq, &gfx_freqs_size, gfx_freqs))
		return;
	// Set the nearest valid graphics frequency for the requested frequency
	_get_nearest_freq(gfx_freq, gfx_freqs_size, gfx_freqs, log_lvl);
}

/*
 * Set the memory and graphics clock frequencies for the GPU
 *
 * device	(IN) The NVML GPU device handle
 * mem_freq	(IN) The memory clock frequency, in MHz
 * gfx_freq	(IN) The graphics clock frequency, in MHz
 *
 * Returns true if successful, false if not
 */
static bool _nvml_set_freqs(nvmlDevice_t *device, unsigned int mem_freq,
			    unsigned int gfx_freq)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceSetApplicationsClocks(*device, mem_freq, gfx_freq);
	END_TIMER;
	debug3("nvmlDeviceSetApplicationsClocks(%u, %u) took %ld microseconds",
	       mem_freq, gfx_freq, DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to set memory and graphics clock frequency "
		      "pair (%u, %u) for the GPU: %s", __func__, mem_freq,
		      gfx_freq, nvmlErrorString(nvml_rc));
		return false;
	}
	return true;
}

/*
 * Reset the memory and graphics clock frequencies for the GPU to the same
 * default frequencies that are used after system reboot or driver reload. This
 * default cannot be changed.
 *
 * device	(IN) The NVML GPU device handle
 *
 * Returns true if successful, false if not
 */
static bool _nvml_reset_freqs(nvmlDevice_t *device)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;

	START_TIMER;
	nvml_rc = nvmlDeviceResetApplicationsClocks(*device);
	END_TIMER;
	debug3("nvmlDeviceResetApplicationsClocks() took %ld microseconds",
	       DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to reset GPU frequencies to the hardware default: %s",
		      __func__, nvmlErrorString(nvml_rc));
		return false;
	}
	return true;
}

/*
 * Get the memory or graphics clock frequency that the GPU is currently running
 * at
 *
 * device	(IN) The NVML GPU device handle
 * type		(IN) The clock type to query. Either NVML_CLOCK_GRAPHICS or
 * 		NVML_CLOCK_MEM.
 *
 * Returns the clock frequency in MHz if successful, or 0 if not
 */
static unsigned int _nvml_get_freq(nvmlDevice_t *device, nvmlClockType_t type)
{
	nvmlReturn_t nvml_rc;
	unsigned int freq = 0;
	char *type_str = "unknown";
	DEF_TIMERS;

	switch (type) {
	case NVML_CLOCK_GRAPHICS:
		type_str = "graphics";
		break;
	case NVML_CLOCK_MEM:
		type_str = "memory";
		break;
	default:
		error("%s: Unsupported clock type", __func__);
		break;
	}

	START_TIMER;
	nvml_rc = nvmlDeviceGetApplicationsClock(*device, type, &freq);
	END_TIMER;
	debug3("nvmlDeviceGetApplicationsClock(%s) took %ld microseconds",
	       type_str, DELTA_TIMER);
	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to get the GPU %s frequency: %s", __func__,
		      type_str, nvmlErrorString(nvml_rc));
		return 0;
	}
	return freq;
}

static unsigned int _nvml_get_gfx_freq(nvmlDevice_t *device)
{
	return _nvml_get_freq(device, NVML_CLOCK_GRAPHICS);
}

static unsigned int _nvml_get_mem_freq(nvmlDevice_t *device)
{
	return _nvml_get_freq(device, NVML_CLOCK_MEM);
}

/*
 * Convert a frequency value to a string
 * Returned string must be xfree()'ed
 */
static char *_freq_value_to_string(unsigned int freq)
{
	switch (freq) {
	case GPU_LOW:
		return xstrdup("low");
		break;
	case GPU_MEDIUM:
		return xstrdup("medium");
		break;
	case GPU_HIGH:
		return xstrdup("high");
		break;
	case GPU_HIGH_M1:
		return xstrdup("highm1");
		break;
	default:
		return xstrdup_printf("%u", freq);
		break;
	}
}

/*
 * Reset the frequencies of each GPU in the step to the hardware default
 * NOTE: NVML must be initialized beforehand
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate.
 * log_lvl	(IN) The log level at which to print
 */
static void _reset_freq(bitstr_t *gpus, log_level_t log_lvl)
{
	int gpu_len = bit_size(gpus);
	int i = -1, count = 0, count_set = 0;
	bool freq_reset = false;

	/*
	 * Reset the frequency of each device allocated to the step
	 */
	for (i = 0; i < gpu_len; i++) {
		nvmlDevice_t device;
		if (!bit_test(gpus, i))
			continue;
		count++;

		if (!_nvml_get_handle(i, &device))
			continue;

		debug2("Memory frequency before reset: %u",
		       _nvml_get_mem_freq(&device));
		debug2("Graphics frequency before reset: %u",
		       _nvml_get_gfx_freq(&device));
		freq_reset =_nvml_reset_freqs(&device);
		debug2("Memory frequency after reset: %u",
		       _nvml_get_mem_freq(&device));
		debug2("Graphics frequency after reset: %u",
		       _nvml_get_gfx_freq(&device));

		// TODO: Check to make sure that the frequency reset

		if (freq_reset) {
			log_var(log_lvl, "Successfully reset GPU[%d]", i);
			count_set++;
		} else {
			log_var(log_lvl, "Failed to reset GPU[%d]", i);
		}
	}

	if (count_set != count) {
		log_var(log_lvl,
			"%s: Could not reset frequencies for all GPUs. "
			"Set %d/%d total GPUs", __func__, count_set, count);
		fprintf(stderr, "Could not reset frequencies for all GPUs. "
			"Set %d/%d total GPUs\n", count_set, count);
	}
}

/*
 * Set the frequencies of each GPU specified for the step
 * NOTE: NVML must be initialized beforehand
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate.
 * gpu_freq	(IN) The frequencies to set each of the GPUs to. If a NULL or
 * 		empty memory or graphics frequency is specified, then GpuFreqDef
 * 		will be consulted, which defaults to "high,memory=high" if not
 * 		set.
 * log_lvl	(IN) The log level at which to print
 */
static void _set_freq(bitstr_t *gpus, char *gpu_freq, log_level_t log_lvl)
{
	bool verbose_flag = false;
	int gpu_len = 0;
	int i = -1, count = 0, count_set = 0;
	unsigned int gpu_freq_num = 0, mem_freq_num = 0;
	bool freq_set = false, freq_logged = false;
	char *tmp = NULL;
	slurm_cgroup_conf_t *cg_conf;
	bool task_cgroup = false;
	bool constrained_devices = false;
	bool cgroups_active = false;
	char *task_plugin_type = NULL;

	/*
	 * Parse frequency information
	 */
	debug2("_parse_gpu_freq(%s)", gpu_freq);
	_parse_gpu_freq(gpu_freq, &gpu_freq_num, &mem_freq_num, &verbose_flag);
	if (verbose_flag)
		debug2("verbose_flag ON");

	tmp = _freq_value_to_string(mem_freq_num);
	debug2("Requested GPU memory frequency: %s", tmp);
	xfree(tmp);
	tmp = _freq_value_to_string(gpu_freq_num);
	debug2("Requested GPU graphics frequency: %s", tmp);
	xfree(tmp);

	if (!mem_freq_num || !gpu_freq_num) {
		debug2("%s: No frequencies to set", __func__);
		return;
	}

	// Check if GPUs are constrained by cgroups
	slurm_mutex_lock(&xcgroup_config_read_mutex);
	cg_conf = xcgroup_get_slurm_cgroup_conf();
	if (cg_conf && cg_conf->constrain_devices)
		constrained_devices = true;
	slurm_mutex_unlock(&xcgroup_config_read_mutex);

	// Check if task/cgroup plugin is loaded
	task_plugin_type = slurm_get_task_plugin();
	if (strstr(task_plugin_type, "cgroup"))
		task_cgroup = true;
	xfree(task_plugin_type);

	// If both of these are true, then GPUs will be constrained
	if (constrained_devices && task_cgroup) {
		cgroups_active = true;
		gpu_len = bit_set_count(gpus);
		debug2("%s: cgroups are configured. Using LOCAL GPU IDs",
		       __func__);
	} else {
	 	gpu_len = bit_size(gpus);
		debug2("%s: cgroups are NOT configured. Assuming GLOBAL GPU IDs",
		       __func__);
	}

	/*
	 * Set the frequency of each device allocated to the step
	 */
	for (i = 0; i < gpu_len; i++) {
		char *sep = "";
		nvmlDevice_t device;

		// Only check the global GPU bitstring if not using cgroups
		if (!cgroups_active && !bit_test(gpus, i)) {
			debug2("Passing over NVML device %u", i);
			continue;
		}
		count++;

		if (!_nvml_get_handle(i, &device))
			continue;
		debug2("Setting frequency of NVML device %u", i);
		_nvml_get_nearest_freqs(&device, &mem_freq_num, &gpu_freq_num,
					log_lvl);

		debug2("Memory frequency before set: %u",
		       _nvml_get_mem_freq(&device));
		debug2("Graphics frequency before set: %u",
		       _nvml_get_gfx_freq(&device));
		freq_set = _nvml_set_freqs(&device, mem_freq_num, gpu_freq_num);
		debug2("Memory frequency after set: %u",
		       _nvml_get_mem_freq(&device));
		debug2("Graphics frequency after set: %u",
		       _nvml_get_gfx_freq(&device));

		if (mem_freq_num) {
			xstrfmtcat(tmp, "%smemory_freq:%u", sep, mem_freq_num);
			sep = ",";
		}
		if (gpu_freq_num) {
			xstrfmtcat(tmp, "%sgraphics_freq:%u", sep,
				   gpu_freq_num);
		}

		if (freq_set) {
			log_var(log_lvl, "Successfully set GPU[%d] %s", i, tmp);
			count_set++;
		} else {
			log_var(log_lvl, "Failed to set GPU[%d] %s", i, tmp);
		}

		if (verbose_flag && !freq_logged) {
			fprintf(stderr, "GpuFreq=%s\n", tmp);
			freq_logged = true;	/* Just log for first GPU */
		}
		xfree(tmp);
	}

	if (count_set != count) {
		log_var(log_lvl,
			"%s: Could not set frequencies for all GPUs. "
			"Set %d/%d total GPUs", __func__, count_set, count);
		fprintf(stderr, "Could not set frequencies for all GPUs. "
			"Set %d/%d total GPUs\n", count_set, count);
	}
}

#endif // HAVE_NVML

extern void step_configure_hardware(bitstr_t *usable_gpus, char *tres_freq)
{
#ifdef HAVE_NVML
	char *freq = NULL;
#endif
	char *tmp = NULL;
	log_level_t log_lvl;

	if (!usable_gpus)
		return;		/* Job allocated no GPUs */
	if (!tres_freq)
		return;		/* No TRES frequency spec */
	if (!(tmp = strstr(tres_freq, "gpu:")))
		return;		/* No GPU frequency spec */

	if (debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_DEBUG;

#ifdef HAVE_NVML
	// Strip "gpu:" from tres_freq
	freq = xstrdup(tmp + 4);
	if ((tmp = strchr(freq, ';')))
		tmp[0] = '\0';

	// Save a copy of the GPUs affected, so we can reset things afterwards
	saved_gpus = bit_copy(usable_gpus);

	_nvml_init();
	// Set the frequency of each GPU index specified in the bitstr
	_set_freq(usable_gpus, freq, log_lvl);
	xfree(freq);
#else
	log_var(log_lvl, "Slurm is not configured with NVIDIA NVML support. "
		"Cannot set GPU frequency");
	fprintf(stderr,	"Slurm is not configured with NVIDIA NVML support. "
		"Cannot set GPU frequency\n");
#endif
}

extern void step_unconfigure_hardware(void)
{
#ifdef HAVE_NVML
	log_level_t log_lvl;

	if (!saved_gpus)
		return;
	if (debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_DEBUG;

	// Reset the frequencies back to the hardware default
	_reset_freq(saved_gpus, log_lvl);
	FREE_NULL_BITMAP(saved_gpus);
	_nvml_shutdown();
#endif
}

static void _set_env(char ***env_ptr, void *gres_ptr, int node_inx,
		     bitstr_t *usable_gres,
		     bool *already_seen, int *local_inx,
		     bool reset, bool is_job)
{
	char *global_list = NULL, *local_list = NULL;
	char *slurm_env_var = NULL;

	if (is_job)
		// TODO: This env var doesn't exist. Should it be SLURM_GRES?
		slurm_env_var = "SLURM_JOB_GRES";
		// slurm_env_var = "SLURM_GRES";
	else
		slurm_env_var = "SLURM_STEP_GRES";

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
 * Print the GRES conf record on a single line
 */
static void _print_gres_conf(gres_slurmd_conf_t *gres_slurmd_conf,
			     log_level_t log_lvl)
{
	log_var(log_lvl, "    GRES[%s](%"PRIu64"): %8s | Cores(%d): %6s | "
		"Links: %6s | %15s%s", gres_slurmd_conf->name,
		gres_slurmd_conf->count, gres_slurmd_conf->type_name,
		gres_slurmd_conf->cpu_cnt, gres_slurmd_conf->cpus,
		gres_slurmd_conf->links, gres_slurmd_conf->file,
		gres_slurmd_conf->ignore ? " | IGNORE":"");
}

/*
 * Print the gres.conf record in a parsable format
 * Do NOT change the format of this without also changing test39.17!
 */
static void _print_gres_conf_parsable(gres_slurmd_conf_t *gres_slurmd_conf,
				      log_level_t log_lvl)
{
	log_var(log_lvl, "GRES_PARSABLE[%s](%"PRIu64"):%s|%d|%s|%s|%s|%s",
		gres_slurmd_conf->name, gres_slurmd_conf->count,
		gres_slurmd_conf->type_name, gres_slurmd_conf->cpu_cnt,
		gres_slurmd_conf->cpus, gres_slurmd_conf->links,
		gres_slurmd_conf->file, gres_slurmd_conf->ignore ? "IGNORE":"");
}

/*
 * Prints out each gres_slurmd_conf_t record in the list
 */
static void _print_gres_list_helper(List gres_list, log_level_t log_lvl,
				    bool parsable)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record;

	if (gres_list == NULL)
		return;
	itr = list_iterator_create(gres_list);
	while ((gres_record = list_next(itr))) {
		if (parsable)
			_print_gres_conf_parsable(gres_record, log_lvl);
		else
			_print_gres_conf(gres_record, log_lvl);
	}
	list_iterator_destroy(itr);
}

/*
 * Print each gres_slurmd_conf_t record in the list
 */
static void _print_gres_list(List gres_list, log_level_t log_lvl)
{
	_print_gres_list_helper(gres_list, log_lvl, false);
}

/*
 * Print each record in the list in a parsable manner, for test consumption
 */
static void _print_gres_list_parsable(List gres_list)
{
	_print_gres_list_helper(gres_list, LOG_LEVEL_INFO, true);
}

static void _delete_gres_list(void *x)
{
	gres_slurmd_conf_t *p = (gres_slurmd_conf_t *) x;
	xfree(p->cpus);
	FREE_NULL_BITMAP(p->cpus_bitmap);
	xfree(p->file);
	xfree(p->links);
	xfree(p->name);
	xfree(p->type_name);
	xfree(p);
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

/*
 * Creates a gres_slurmd_conf_t record to add to a list of gres_slurmd_conf_t
 * records
 */
static void _add_gres_to_list(List gres_list, char *name, int device_cnt,
			      int cpu_cnt, char *cpu_aff_abs_range,
			      char *device_file, char *type, char *nvlinks,
			      bool ignore)
{
	gres_slurmd_conf_t *gpu_record;
	bool use_empty_first_record = false;
	ListIterator itr = list_iterator_create(gres_list);

	/*
	 * If the first record already exists and has a count of 0 then
	 * overwrite it.
	 * This is a placeholder record created in gres.c#_no_gres_conf()
	 */
	gpu_record = list_next(itr);
	if (gpu_record && (gpu_record->count == 0))
		use_empty_first_record = true;
	else
		gpu_record = xmalloc(sizeof(gres_slurmd_conf_t));
	gpu_record->cpu_cnt = cpu_cnt;
	gpu_record->cpus_bitmap = bit_alloc(gpu_record->cpu_cnt);
	if (bit_unfmt(gpu_record->cpus_bitmap, cpu_aff_abs_range)) {
		error("%s: bit_unfmt(dst_bitmap, src_str) failed", __func__);
		error("    Is the CPU range larger than the CPU count allows?");
		error("    src_str: %s", cpu_aff_abs_range);
		error("    dst_bitmap_size: %"BITSTR_FMT,
		      bit_size(gpu_record->cpus_bitmap));
		error("    cpu_cnt: %d", gpu_record->cpu_cnt);
		bit_free(gpu_record->cpus_bitmap);
		if (!use_empty_first_record)
			xfree(gpu_record);
		list_iterator_destroy(itr);
		return;
	}
	if (device_file)
		gpu_record->config_flags |= GRES_CONF_HAS_FILE;
	if (type)
		gpu_record->config_flags |= GRES_CONF_HAS_TYPE;
	gpu_record->cpus = xstrdup(cpu_aff_abs_range);
	gpu_record->type_name = xstrdup(type);
	gpu_record->name = xstrdup(name);
	gpu_record->file = xstrdup(device_file);
	gpu_record->links = xstrdup(nvlinks);
	gpu_record->count = device_cnt;
	gpu_record->plugin_id = gres_plugin_build_id(name);
	gpu_record->ignore = ignore;
	if (!use_empty_first_record)
		list_append(gres_list, gpu_record);
	list_iterator_destroy(itr);
}

// Duplicate an existing gres_slurmd_conf_t record and add it to a list
static void _add_record_to_gres_list(List gres_list, gres_slurmd_conf_t *record)
{
	_add_gres_to_list(gres_list, record->name, record->count,
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
 * See if a gres gpu record exists in a gres list
 *
 * "gres" == type:cpus:links
 * Notably this excludes devices (device files).
 */
static void *_gres_exists_in_list(List gres_list,
				  gres_slurmd_conf_t *gres_record)
{
	if (gres_list == NULL)
		return NULL;
	return list_find_first(gres_list, _find_gres_in_list,
			       gres_record);
}

/*
 * Takes a gres_slurmd_conf_t record, searches for a matching gres_slurmd_conf_t
 * in gres_list, and attempts to merge their `file` data members (devices).
 * The merged file string is stored in the found gres_slurmd_conf_t record in
 * gres_list.
 */
static void _merge_device_into_list(List gres_list, gres_slurmd_conf_t *device)
{
	hostlist_t record_hl;
	int devices_added = 0;
	gres_slurmd_conf_t *record;

	if (!device || !device->file) {
		debug2("Warning: device to merge was null or had null file");
		return;
	}

	record = _gres_exists_in_list(gres_list, device);
	record_hl = hostlist_create(record->file);
	// Merge each device into the record's devices
	devices_added = hostlist_push(record_hl, device->file);
	if (devices_added == 0)
		debug2("Warning: No devices merged in...");
	record->count += devices_added;
	xfree(record->file);
	record->file = (char *) hostlist_ranged_string_xmalloc(record_hl);

	hostlist_destroy(record_hl);
}

/*
 * Add GRES record and device to list out only if it doesn't exist already in
 * list_out.
 * Also, only adds records that are NOT ignored
 */
static void _add_unique_gres_to_list(List gres_list_out,
				     gres_slurmd_conf_t *gres_record)
{
	if (gres_record->ignore) {
		debug3("Omitting adding this record due to ignore=true:");
		_print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		return;
	}

	// If device file is already in the list, ignore it
	if (_device_file_exists_in_list(gres_list_out, gres_record)) {
		debug3("GRES device file is already specified! Ignoring");
		_print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		return;
	}

	// If GRES (type:cpus:links) isn't found, add it, with device info
	if (!_gres_exists_in_list(gres_list_out, gres_record)) {
		debug3("Adding the following GRES device:");
		_print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		_add_record_to_gres_list(gres_list_out, gres_record);
		return;
	}

	// If GRES is found, see if device (type:cpus:links:file) is found
	if (_device_exists_in_list(gres_list_out, gres_record)) {
		debug3("GRES device is already specified! Ignoring");
	} else {
		// If device isn't found, add it
		debug3("This GRES exists, but device doesn't. Merge it in:");
		_print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
		_merge_device_into_list(gres_list_out, gres_record);
	}
}

/*
 * Add all unique, unignored devices specified by the GRES records in list in
 * only if they don't already exist in list out.
 * Also checks to make sure list in doesn't add any records to list out that are
 * specifically ignored in list compare.
 */
static void _add_unique_gres_list_to_list_compare(List gres_list_out,
						  List gres_list_compare,
						  List gres_list_in)
{
	ListIterator itr;
	gres_slurmd_conf_t *gres_record;
	gres_slurmd_conf_t *gres_compare;

	if (!gres_list_in || list_is_empty(gres_list_in))
		return;

	itr = list_iterator_create(gres_list_in);
	while ((gres_record = list_next(itr))) {
		// Make sure current device isn't ignored in list compare
		gres_compare = _device_exists_in_list(gres_list_compare,
						      gres_record);
		if (gres_compare && gres_compare->ignore) {
			debug3("This GRES device:");
			_print_gres_conf(gres_record, LOG_LEVEL_DEBUG3);
			debug3("is ignored by this record:");
			_print_gres_conf(gres_compare, LOG_LEVEL_DEBUG3);
			continue;
		}

		_add_unique_gres_to_list(gres_list_out, gres_record);
	}
	list_iterator_destroy(itr);
}

/*
 * Add all unique, unignored devices specified by the GRES records in list in
 * only if they don't already exist in list out.
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
 * gres_list_final: The final normalized and compressed gres.conf list to be
 * 		    returned in the gres_list_conf list pointer.
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
	List gres_list_conf_single, gres_list_final, non_gpu_list;
	bool use_system_detected = true;
	bool log_zero = true;

	if (gres_list_conf == NULL) {
		error("%s: gres_list_conf is NULL. This shouldn't happen",
		      __func__);
		return;
	}

	gres_list_conf_single = list_create(_delete_gres_list);
	gres_list_final = list_create(_delete_gres_list);
	non_gpu_list = list_create(_delete_gres_list);

	debug2("gres_list_conf:");
	_print_gres_list(gres_list_conf, LOG_LEVEL_DEBUG2);

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
			_add_gres_to_list(non_gpu_list,
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
		if (use_system_detected && gres_record->ignore == false) {
			use_system_detected = false;
		}

		if (gres_record->count == 1) {
			if (_device_exists_in_list(gres_list_conf_single,
						   gres_record))
				// Duplicate from single! Do not add
				continue;

			// Add device from single record
			_add_gres_to_list(gres_list_conf_single,
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
			temp_gres_conf->type_name =
					xstrdup(gres_record->type_name);
			temp_gres_conf->cpus = xstrdup(gres_record->cpus);
			temp_gres_conf->links =
					xstrdup(gres_record->links);
			temp_gres_conf->file = xstrdup(hl_name);
			if (_device_exists_in_list(gres_list_conf_single,
						   temp_gres_conf)) {
				// Duplicate from multiple! Do not add
			} else {
				file_array[file_count] = xstrdup(hl_name);
				file_count++;
			}
			xfree(temp_gres_conf->type_name);
			xfree(temp_gres_conf->cpus);
			xfree(temp_gres_conf->links);
			xfree(temp_gres_conf->file);
			xfree(temp_gres_conf);
			free(hl_name);
		}
		hostlist_destroy(hl);

		// Break down file into individual file names
		// Create an array of these file names, and index off of i
		for (i = 0; i < file_count; ++i) {
			_add_gres_to_list(gres_list_conf_single,
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
			_print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
	}

	debug2("GPUs specified in gres.conf that are NOT found on node:");
	list_iterator_reset(itr_single);
	while ((gres_record = list_next(itr_single))) {
		if (!_device_exists_in_list(gres_list_system, gres_record))
			_print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
	}
	list_iterator_destroy(itr_single);

	debug2("GPUs detected on the node, but not specified in gres.conf:");
	if (gres_list_system) {
		itr_system = list_iterator_create(gres_list_system);
		while ((gres_record = list_next(itr_system))) {
			if (!_device_exists_in_list(gres_list_conf_single,
						    gres_record))
				_print_gres_conf(gres_record, LOG_LEVEL_DEBUG2);
		}
		list_iterator_destroy(itr_system);
	}

	// Combine single device records into multiple device records
	debug2("Adding unique unignored USER-SPECIFIED devices:");
	_add_unique_gres_list_to_list(gres_list_final, gres_list_conf_single);

	// If gres.conf is empty or only ignored devices, fill with system info
	if (use_system_detected) {
		debug2("Adding unique unignored SYSTEM-DETECTED devices:");
		_add_unique_gres_list_to_list_compare(gres_list_final,
						      gres_list_conf_single,
						      gres_list_system);
	}

	debug2("gres_list_final:");
	_print_gres_list(gres_list_final, LOG_LEVEL_DEBUG2);

	/*
	 * Free old records in gres_list_conf
	 * Replace gres_list_conf with gres_list_final
	 * Note: list_transfer won't work because lists don't have same delFunc
	 * Note: list_append_list won't work because delFunc is non-null
	 */
	list_flush(gres_list_conf);
	while ((gres_record = list_pop(gres_list_final))) {
		list_append(gres_list_conf, gres_record);
	}
	while ((gres_record = list_pop(non_gpu_list))) {
		list_append(gres_list_conf, gres_record);
	}

	FREE_NULL_LIST(gres_list_conf_single);
	FREE_NULL_LIST(gres_list_final);
	FREE_NULL_LIST(non_gpu_list);
}

#ifdef HAVE_NVML

/*
 * Get the version of the system's graphics driver
 */
static void _nvml_get_driver(char *driver, unsigned int len)
{
	nvmlReturn_t nvml_rc = nvmlSystemGetDriverVersion(driver, len);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get the version of the system's graphics"
		      "driver: %s", nvmlErrorString(nvml_rc));
		driver[0] = '\0';
	}
}

/*
 * Get the version of the NVML library
 */
static void _nvml_get_version(char *version, unsigned int len)
{
	nvmlReturn_t nvml_rc = nvmlSystemGetNVMLVersion(version, len);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get the version of the system's graphics"
		      "version: %s", nvmlErrorString(nvml_rc));
		version[0] = '\0';
	}
}

/*
 * Get the total # of GPUs in the system
 */
static void _nvml_get_device_count(unsigned int *device_count)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetCount(device_count);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get device count: %s",
		      nvmlErrorString(nvml_rc));
		*device_count = 0;
	}
}

/*
 * Get the name of the GPU
 */
static void _nvml_get_device_name(nvmlDevice_t *device, char *device_name,
				 unsigned int size)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetName(*device, device_name, size);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get name of the GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Allocates a string in device_brand containing the brand/type of the GPU
 * Returned string must be xfree()'d
 */
static char *_nvml_get_device_brand(nvmlDevice_t *device)
{
	nvmlBrandType_t brand = NVML_BRAND_UNKNOWN;
	char *device_brand = NULL;
	nvmlReturn_t nvml_rc = nvmlDeviceGetBrand(*device, &brand);
	if (nvml_rc == NVML_ERROR_INVALID_ARGUMENT) {
		debug3("NVML: Device is invalid or brand type is null");
		return NULL;
	} else if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get brand/type of the GPU: %s",
		      nvmlErrorString(nvml_rc));
		return NULL;
	}

	switch (brand) {
	case NVML_BRAND_TESLA:
		device_brand = xstrdup("tesla");
		break;
	case NVML_BRAND_QUADRO:
		device_brand = xstrdup("quadro");
		break;
	case NVML_BRAND_GEFORCE:
		device_brand = xstrdup("geforce");
		break;
// TODO: How to determine NVML version or if NVML_BRAND_TITAN is defined enum?
#ifdef HAVE_NVML_TITAN
	case NVML_BRAND_TITAN:
		device_brand = xstrdup("titan");
		break;
#endif
	case NVML_BRAND_NVS:
		device_brand = xstrdup("nvs");
		break;
	case NVML_BRAND_GRID:
		device_brand = xstrdup("grid");
		break;
	case NVML_BRAND_COUNT:
		device_brand = xstrdup("count");
		break;
	case NVML_BRAND_UNKNOWN:
	default:
		device_brand = xstrdup("unknown");
		break;
	}

	return device_brand;
}

/*
 * Get the UUID of the device, since device index can fluctuate
 */
static void _nvml_get_device_uuid(nvmlDevice_t *device, char *uuid,
				  unsigned int len)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetUUID(*device, uuid, len);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get UUID of GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Get the PCI Bus ID of the device, since device index can fluctuate
 */
static void _nvml_get_device_pci_info(nvmlDevice_t *device, nvmlPciInfo_t *pci)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetPciInfo(*device, pci);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get PCI info of GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Retrieves minor number for the device. The minor number for the device is
 * such that the Nvidia device node file for each GPU will have the form
 * /dev/nvidia[minor_number].
 */
static void _nvml_get_device_minor_number(nvmlDevice_t *device,
					 unsigned int *minor)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetMinorNumber(*device, minor);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get minor number of GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Retrieves an array of unsigned ints (sized to cpuSetSize) of bitmasks with
 * the ideal CPU affinity for the GPU.
 *
 * cpu_set: an array reference in which to return a bitmask of CPUs. 64 CPUs per
 * unsigned long on 64-bit machines, 32 on 32-bit machines.
 *
 * For example, on 32-bit machines, if processors 0, 1, 32, and 33 are ideal for
 * the device and cpuSetSize == 2, result[0] = 0x3, result[1] = 0x3.
 */
static void _nvml_get_device_affinity(nvmlDevice_t *device, unsigned int size,
				      unsigned long *cpu_set)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetCpuAffinity(*device, size, cpu_set);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get cpu affinity of GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Returns the busId string of the connected endpoint device of an nvlink lane.
 * If query fails, an empty string is returned.
 * The returned string must be xfree'd.
 *
 * device - the GPU device
 * lane - the nvlink lane that we are checking
 *
 * device <---lane---> endpoint/remote device
 */
static char *_nvml_get_nvlink_remote_pcie(nvmlDevice_t *device,
					  unsigned int lane)
{
	nvmlPciInfo_t pci_info = {{0}};
	nvmlReturn_t nvml_rc = nvmlDeviceGetNvLinkRemotePciInfo(*device, lane,
								&pci_info);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get PCI info of endpoint device for lane"
		      " %d: %s", lane, nvmlErrorString(nvml_rc));
		return xstrdup("");
	} else {
		return xstrdup(pci_info.busId);
	}
}

/*
 * Does a linear search for string str in array of strings str_arr, starting
 * from index 0.
 * Returns the index of the first match found, else returns -1 if not found.
 *
 * str - the string to search for
 * str_array - the array of strings to search in
 * size - the size of str_arr
 */
static int _get_index_from_str_arr(char *str, char **str_arr, unsigned int size)
{
	int i;
	if (str_arr == NULL || str == NULL)
		return -1;
	for (i = 0; i < size; ++i) {
		if (xstrcmp(str, str_arr[i]) == 0) {
			return i;
		}
	}
	return -1;
}

/*
 * Allocates and returns a string that is a comma separated list of nvlinks of
 * the device. If no links are specified, then an empty string will be returned.
 * The string must be xfree'd.
 *
 * device - the current GPU to get the nvlink info for
 * index - the index of the current GPU as returned by NVML. Based on PCI bus id
 * device_lut - an array of PCI busid's for each GPU. The index is the GPU index
 * device_count - the size of device_lut
 */
static char *_nvml_get_nvlink_info(nvmlDevice_t *device, int index,
				   char **device_lut, unsigned int device_count)
{
	unsigned int i;
	nvmlReturn_t nvml_rc;
	nvmlEnableState_t is_active;
	int *links = xmalloc(sizeof(int) * device_count);
	char *links_str = NULL, *sep = "";

	// Initialize links, xmalloc() initialized the array to 0 or NVLINK_NONE
	links[index] = NVLINK_SELF;

	// Query all nvlink lanes
	for (i = 0; i < NVML_NVLINK_MAX_LINKS; ++i) {
		nvml_rc = nvmlDeviceGetNvLinkState(*device, i, &is_active);
		if (nvml_rc == NVML_ERROR_INVALID_ARGUMENT) {
			debug3("NVML: Device/lane %d is invalid", i);
			continue;
		} else if (nvml_rc == NVML_ERROR_NOT_SUPPORTED) {
			debug3("NVML: Device %d does not support "
			       "nvmlDeviceGetNvLinkState()", i);
			break;
		} else if (nvml_rc != NVML_SUCCESS) {
			error("NVML: Failed to get nvlink info from GPU: %s",
			      nvmlErrorString(nvml_rc));
		}
		// See if nvlink lane is active
		if (is_active == NVML_FEATURE_ENABLED) {
			char *busid;
			int k;
			debug3("NVML: nvlink %d is enabled", i);

			/*
			 * Count link endpoints to determine single and double
			 * links. E.g. if already a single link (1), increment
			 * to a double (2).
			 */
			busid = _nvml_get_nvlink_remote_pcie(device, i);
			k = _get_index_from_str_arr(busid, device_lut,
						    device_count);
			// Ignore self and not-founds
			if ((k != index) && (k != -1)) {
				links[k]++;
			}
			xfree(busid);
		} else
			debug3("NVML: nvlink %d is disabled", i);
	}

	// Convert links to comma separated string
	for (i = 0; i < device_count; ++i) {
		xstrfmtcat(links_str, "%s%d", sep, links[i]);
		sep = ",";
	}

	xfree(links);
	if (!links_str)
		links_str = xstrdup("");
	return links_str;
}

#endif // HAVE_NVML

/*
 * Converts a cpu_set returned from the NVML API into a Slurm bitstr_t
 *
 * This function accounts for the endianess of the machine.
 *
 * cpu_set_bitstr: An bitstr_t preallocated via the bit_alloc command to
 * 		   be bitstr_size bits wide.
 * cpu_set: The cpu_set array returned by nvmlDeviceGetCpuAffinity()
 * cpu_set_size: The size of the cpu_set array
 */
static void _set_cpu_set_bitstr(bitstr_t *cpu_set_bitstr,
				unsigned long *cpu_set,
				unsigned int cpu_set_size)
{
	int j, k, b;
	int bit_cur;
	int bitstr_bits = (int) bit_size(cpu_set_bitstr);
	int cpu_set_bits = (cpu_set_size * ULONG_BITS);

	// If this fails, then something went horribly wrong
	if (bitstr_bits != cpu_set_bits)
		fatal("%s: bitstr_bits != cpu_set_bits", __func__);

	bit_cur = bitstr_bits - 1;

	// Iterate through each cpu_set long int
	for (j = cpu_set_size - 1; j >= 0; --j) {
		// Iterate through the bytes of the jth ulong bitmask
		char *bitmask = (char *) &cpu_set[j];
#ifdef SLURM_BIGENDIAN
		for (k = 0; k < ULONG_BYTES; ++k) {
#else
		for (k = ULONG_BYTES - 1; k >= 0; --k) {
#endif // SLURM_BIGENDIAN
			unsigned char byte = bitmask[k];
			unsigned char mask;
			// If byte is zero, nothing to set
			if (byte == 0) {
				bit_cur -= 8;
				continue;
			}

			// Test each bit of byte, from MSB to LSB. Set if needed
			mask = 0x80;
			for (b = 0; b < 8; ++b) {
				if (byte & mask)
					bit_set(cpu_set_bitstr, bit_cur);
				mask >>= 1;
				bit_cur--;
			}
			xassert(mask == 0x00);
		}
	}

	// If this isn't -1, then something went horribly wrong
	if (bit_cur != -1)
		fatal("%s: bit_cur(%d) != -1", __func__, bit_cur);
}

#ifdef HAVE_NVML
/*
 * Creates and returns a gres conf list of detected nvidia gpus on the node.
 * If an error occurs, return NULL
 * Caller is responsible for freeing the list.
 *
 * If the NVIDIA NVML API exists (comes with CUDA), then query GPU info,
 * so the user doesn't need to specify manually in gres.conf.
 * Specifically populate cpu affinity and nvlink information
 */
static List _get_system_gpu_list_nvml(node_config_load_t *node_config)
{
	unsigned int i;
	unsigned int device_count = 0;
	List gres_list_system = list_create(_delete_gres_list);
	char driver[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE];
	char version[NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE];
	char **device_lut;

	_nvml_init();
	_nvml_get_driver(driver, NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE);
	_nvml_get_version(version, NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE);
	debug("Systems Graphics Driver Version: %s", driver);
	debug("NVML Library Version: %s", version);

	_nvml_get_device_count(&device_count);

	debug2("MAX_CPUS: %d", MAX_CPUS);
	debug2("CPU_SET_SIZE (# of ulongs needed to hold MAX_CPUS bits): %lu",
	       CPU_SET_SIZE);
	debug2("Total CPU count: %d", node_config->cpu_cnt);
	debug2("Device count: %d", device_count);

	// Create a device index --> PCI Bus ID lookup table
	device_lut = xmalloc(sizeof(char *) * device_count);

	/*
	 * Loop through to create device to PCI busId lookup table
	 */
	for (i = 0; i < device_count; ++i) {
		nvmlDevice_t device;
		nvmlPciInfo_t pci_info = {{0}};

		// Initialize to null
		device_lut[i] = NULL;

		if (!_nvml_get_handle(i, &device))
			continue;
		_nvml_get_device_pci_info(&device, &pci_info);
		if (pci_info.busId)
			device_lut[i] = xstrdup(pci_info.busId);
	}

	/*
	 * Loop through all the GPUs on the system and add to gres_list_system
	 */
	for (i = 0; i < device_count; ++i) {
		nvmlDevice_t device;
		char uuid[NVML_DEVICE_UUID_BUFFER_SIZE] = {0};
		unsigned int minor_number = 0;
		unsigned long cpu_set[CPU_SET_SIZE] = {0};
		bitstr_t *cpu_aff_mac_bitstr = NULL;
		char *cpu_aff_mac_range = NULL;
		char *cpu_aff_abs_range = NULL;
		nvmlPciInfo_t pci_info = {{0}};
		char *device_file = NULL;
		char *nvlinks = NULL;
		char device_name[NVML_DEVICE_NAME_BUFFER_SIZE] = {0};
		char *device_brand = NULL;

		if (!_nvml_get_handle(i, &device)) {
			error("Creating null GRES GPU record");
			_add_gres_to_list(gres_list_system, "gpu", 1,
					  node_config->cpu_cnt, NULL,
					  NULL, NULL, NULL, false);
			continue;
		}
		_nvml_get_device_name(&device, device_name,
				      NVML_DEVICE_NAME_BUFFER_SIZE);
		_nvml_get_device_uuid(&device, uuid,
				      NVML_DEVICE_UUID_BUFFER_SIZE);
		_nvml_get_device_pci_info(&device, &pci_info);
		_nvml_get_device_minor_number(&device, &minor_number);
		_nvml_get_device_affinity(&device, CPU_SET_SIZE, cpu_set);

		// Convert from nvml cpu bitmask to slurm bitstr_t (machine fmt)
		cpu_aff_mac_bitstr = bit_alloc(MAX_CPUS);
		_set_cpu_set_bitstr(cpu_aff_mac_bitstr, cpu_set, CPU_SET_SIZE);

		// Convert from bitstr_t to cpu range str
		cpu_aff_mac_range = bit_fmt_full(cpu_aff_mac_bitstr);
		bit_free(cpu_aff_mac_bitstr);

		// Convert cpu range str from machine to abstract(slurm) format
		if (node_config->xcpuinfo_mac_to_abs(cpu_aff_mac_range,
						     &cpu_aff_abs_range)) {
			error("    Conversion from machine to abstract failed");
			xfree(cpu_aff_mac_range);
			continue;
		}

		nvlinks = _nvml_get_nvlink_info(&device, i, device_lut,
						device_count);
		device_brand = _nvml_get_device_brand(&device);
		xstrfmtcat(device_file, "/dev/nvidia%u", minor_number);

		debug2("GPU index %u:", i);
		debug2("    Name: %s", device_name);
		debug2("    Brand/Type: %s", device_brand);
		debug2("    UUID: %s", uuid);
		debug2("    PCI Domain/Bus/Device: %u:%u:%u", pci_info.domain,
		       pci_info.bus, pci_info.device);
		debug2("    PCI Bus ID: %s", pci_info.busId);
		debug2("    NVLinks: %s", nvlinks);
		debug2("    Device File (minor number): %s", device_file);
		if (minor_number != i)
			debug("Note: GPU index %u is different from minor "
			      "number %u", i, minor_number);
		debug2("    CPU Affinity Range: %s", cpu_aff_mac_range);
		debug2("    CPU Affinity Range Abstract: %s",cpu_aff_abs_range);
		// Print out possible memory frequencies for this device
		_nvml_print_freqs(&device, LOG_LEVEL_DEBUG2);

		_add_gres_to_list(gres_list_system, "gpu", 1,
				  node_config->cpu_cnt, cpu_aff_abs_range,
				  device_file, device_brand, nvlinks, false);

		xfree(cpu_aff_mac_range);
		xfree(cpu_aff_abs_range);
		xfree(nvlinks);
		xfree(device_brand);
		xfree(device_file);
	}

	/*
	 * Free lookup table
	 */
	for (i = 0; i < device_count; ++i)
		xfree(device_lut[i]);
	xfree(device_lut);
	_nvml_shutdown();

	info("%u GPU system device(s) detected", device_count);
	return gres_list_system;
}

#endif // HAVE_NVML

/*
 * Exercise code that converts NVML cpu affinity to a Slurm bitstring on systems
 * without NVML.
 */
static char *_test_nvml_cpu_conv(char *cpu_range)
{
	unsigned long cpu_set[CPU_SET_SIZE];
	bitstr_t *cpu_aff_mac_bitstr;
	int i;
	char *result;
	info("%s: cpu_range: %s", __func__, cpu_range);

	if (!cpu_range) {
		error("cpu_range is null");
		return xstrdup("");
	}

	if (cpu_range[0] != '~') {
		error("cpu_range doesn't start with `~`!");
		return xstrdup("");
	}

	// Initialize cpu_set to 0
	for (i = 0; i < CPU_SET_SIZE; ++i) {
		cpu_set[i] = 0;
	}

	if (xstrcmp(cpu_range, "~zero") == 0) {
		// nothing
	} else if (xstrcmp(cpu_range, "~max") == 0) {
		for (i = 0; i < CPU_SET_SIZE; ++i) {
			cpu_set[i] = (unsigned long)-1;
		}
	} else if (xstrcmp(cpu_range, "~one") == 0) {
		cpu_set[0] = 1;
	} else if (xstrcmp(cpu_range, "~three") == 0) {
		cpu_set[0] = 3;
	} else if (xstrcmp(cpu_range, "~half") == 0) {
		cpu_set[0] = 0xff00;
	} else if (cpu_range[1] == 'X') {
		/*
		 * Put in all -1's for each X
		 * Limit to CPU_SET_SIZE
		 */
		int count = MIN(strlen(&cpu_range[1]), CPU_SET_SIZE);
		for (i = 0; i < count; ++i) {
			cpu_set[i] = (unsigned long)-1;
		}
		for (i = count; i < CPU_SET_SIZE; ++i) {
			cpu_set[i] = 0;
		}
	} else {
		error("Unknown test keyword");
		return xstrdup("");
	}

	// Print out final cpu set
	for (i = 0; i < CPU_SET_SIZE; ++i) {
		if ((signed) cpu_set[i] == -1)
			printf("X");
		else {
			if (cpu_set[i] > 9)
				printf("(%lu)", cpu_set[i]);
			else
				printf("%lu", cpu_set[i]);
		}
	}
	printf("\n");

	cpu_aff_mac_bitstr = bit_alloc(MAX_CPUS);
	// Convert from nvml cpu bitmask to slurm bitstr_t (machine fmt)
	_set_cpu_set_bitstr(cpu_aff_mac_bitstr, cpu_set, CPU_SET_SIZE);

	// Convert from bitstr_t to cpu range str
	result = bit_fmt_full(cpu_aff_mac_bitstr);

	// TODO: Test converting CPU range from machine to abstract format?

	bit_free(cpu_aff_mac_bitstr);
	return result;
}

static void _print_failed_sort(unsigned int *arr, unsigned int size)
{
	unsigned int i;
	info("GRES_PARSABLE:failed");
	info("Array that failed to sort:");
	for (i = 0; i < size; ++i) {
		info("*%u", arr[i]);
	}
}

// If failed, return false. Else, return true
static bool _test_check_sort(unsigned int *arr, unsigned int size)
{
	// Make sure we are dealing with at least two elements
	if (size <= 1)
		return true;

	if (arr[0] < arr[size - 1]) {
		_print_failed_sort(arr, size);
		return false;
	}

	if (arr[0] < arr[1]) {
		_print_failed_sort(arr, size);
		return false;
	}

	if (arr[size - 2] < arr[size - 1]) {
		_print_failed_sort(arr, size);
		return false;
	}
	return true;
}

/*
 * Test bubble sort code
 */
static void _test_bubble_sort(void)
{
	unsigned int arr_asc[]  = {0,1,2,3,4,5,6,7};
	unsigned int arr_desc[] = {7,6,5,4,3,2,1,0};
	unsigned int arr_rand[] = {5,4,7,2,1,0,6,3};
	unsigned int arr_same[] = {5,5,5,5,5,5,5,5};
	unsigned int arr_dup[]  = {0,0,5,5,4,4,2,2};
	unsigned int arr_one[]  = {1000};
	unsigned int arr_none[] = {};
	unsigned int *arr_null = NULL;
	info("Testing arr_asc...");
	_bubble_sort_descending(arr_asc, 8);
	if (!_test_check_sort(arr_asc, 8))
		return;
	info("Testing arr_desc...");
	_bubble_sort_descending(arr_desc, 8);
	if (!_test_check_sort(arr_desc, 8))
		return;
	info("Testing arr_rand...");
	_bubble_sort_descending(arr_rand, 8);
	if (!_test_check_sort(arr_rand, 8))
		return;
	info("Testing arr_same...");
	_bubble_sort_descending(arr_same, 8);
	if (!_test_check_sort(arr_same, 8))
		return;
	info("Testing arr_dup...");
	_bubble_sort_descending(arr_dup, 8);
	if (!_test_check_sort(arr_dup, 8))
		return;

	// These just need to not crash
	info("Testing arr_one...");
	_bubble_sort_descending(arr_one, 1);
	info("Testing arr_none...");
	_bubble_sort_descending(arr_none, 0);
	info("Testing arr_null...");
	_bubble_sort_descending(arr_null, 1);

	info("GRES_PARSABLE:succeeded");
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

		// Put in hooks to test out specific portions of code
		if (buffer[0] == 'b') {
			// Test bubble sort!
			_test_bubble_sort();
			continue;
		}


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
					cpu_range = _test_nvml_cpu_conv(tok);
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
		_add_gres_to_list(gres_list_system, "gpu", 1, cpu_count,
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
		gres_list_system = list_create(_delete_gres_list);
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
	bool using_fake_system = false;
	log_level_t log_lvl;

	/* Assume this state is caused by an scontrol reconfigure */
	debug_flags = slurm_get_debug_flags();
	if (gres_devices) {
		debug("Resetting gres_devices");
		FREE_NULL_LIST(gres_devices);
	}

	gres_list_system = _get_system_gpu_list_fake();
	if (gres_list_system)
		using_fake_system = true;
#ifdef HAVE_NVML
	// Only query real system devices if there is no fake override
	if (using_fake_system == false)
		gres_list_system = _get_system_gpu_list_nvml(node_config);
	if (gres_list_system == NULL)
		error("System GPU detection failed");
#endif
	if (debug_flags & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_DEBUG;
	if (gres_list_system && list_is_empty(gres_list_system))
		log_var(log_lvl, "There were 0 GPUs detected on the system");
	log_var(log_lvl, "%s: Normalizing gres.conf with system devices",
		plugin_name);
	_normalize_gres_conf(gres_conf_list, gres_list_system);
	FREE_NULL_LIST(gres_list_system);

	log_var(log_lvl, "%s: Final normalized gres.conf list:", plugin_name);
	_print_gres_list(gres_conf_list, log_lvl);

	// Print in parsable format for tests if fake system is in use
	if (using_fake_system) {
		info("Final normalized gres.conf list (parsable):");
		_print_gres_list_parsable(gres_conf_list);
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
