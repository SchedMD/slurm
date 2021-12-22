/*****************************************************************************\
 *  gpu_rsmi.c - Support rsmi interface to an AMD GPU.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Copyright (c) 2019, Advanced Micro Devices, Inc. All rights reserved.
 *  Written by Advanced Micro Devices,
 *  who borrowed heavily from SLURM gpu and nvml plugin.
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

#include "src/common/slurm_xlator.h"
#include "src/common/cgroup.h"
#include "src/common/gres.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include <rocm_smi/rocm_smi.h>

/*
 * #defines needed to test rsmi.
 */
#define FREQS_CONCISE	5 // This must never be smaller than 5, or error

#define GPU_LOW		((unsigned int) -1)
#define GPU_MEDIUM	((unsigned int) -2)
#define GPU_HIGH_M1	((unsigned int) -3)
#define GPU_HIGH	((unsigned int) -4)

static bitstr_t	*saved_gpus;

/*
 * Buffer size large enough for RSMI string
 */
#define RSMI_STRING_BUFFER_SIZE			80

/*
 * PCI information about a GPU device.
 */
typedef struct rsmiPciInfo_st {
	union {
		struct {
#ifdef SLURM_BIGENDIAN
			uint64_t domain : 32;
			uint64_t reserved : 16;
			uint64_t bus : 8;
			uint64_t device : 5;
			uint64_t function : 3;
#else
			uint64_t function : 3;
			uint64_t device : 5;
			uint64_t bus : 8;
			uint64_t reserved : 16;
			uint64_t domain : 32;
#endif
		};
		uint64_t bdfid;
	};
} rsmiPciInfo_t;

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
const char plugin_name[] = "GPU RSMI plugin";
const char	plugin_type[]		= "gpu/rsmi";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);

	return SLURM_SUCCESS;
}

//TODO: Duplicated from NVML plugin. Move to a common directory
static unsigned int _xlate_freq_value(char *gpu_freq)
{
	unsigned int value;

	if (!gpu_freq && (gpu_freq[0] < '0') && (gpu_freq[0] > '9'))
		return 0;	/* Not a numeric value */
	value = strtoul(gpu_freq, NULL, 10);
	return value;
}

//TODO: Duplicated from NVML plugin. Move to a common directory
static unsigned int _xlate_freq_code(char *gpu_freq)
{
	//TODO: To be moved to common directory
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

//TODO: Duplicated from NVML plugin. Move to a common directory
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
				*mem_freq_code = _xlate_freq_code(sep);
				*mem_freq_value = _xlate_freq_value(sep);
				if (!(*mem_freq_code) && !(*mem_freq_value)) {
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
			*gpu_freq_code = _xlate_freq_code(tok);
			*gpu_freq_value = _xlate_freq_value(tok);
			if (!(*gpu_freq_code) && !(*gpu_freq_value))
				debug("Invalid job GPU frequency: %s", tok);
		}
		tok = strtok_r(NULL, ",", &save_ptr);
	}
	xfree(tmp);
}

//TODO: Duplicated from NVML plugin. Move to a common directory
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

//TODO: Duplicated from NVML plugin. Move to a common directory
static int _sort_freq_descending(const void *a, const void *b)
{
	return (*(unsigned long *)b - *(unsigned long *)a);
}

/*
 * Get all possible memory frequencies for the device
 *
 * dv_ind         (IN) The device index
 * mem_freqs_size (IN/OUT) The size of the mem_freqs array; this will be
 *                overwritten with the number of memory freqs found.
 * mem_freqs      (OUT) The possible memory frequencies in MHz.
 *
 * Return true if successful, false if not.
 */
static bool _rsmi_get_mem_freqs(uint32_t dv_ind,
				unsigned int *mem_freqs_size,
				unsigned int *mem_freqs)
{
	const char *status_string;
	rsmi_status_t rsmi_rc;
	rsmi_frequencies_t rsmi_freqs;

	DEF_TIMERS;
	START_TIMER;
	rsmi_rc = rsmi_dev_gpu_clk_freq_get(
		dv_ind, RSMI_CLK_TYPE_MEM, &rsmi_freqs);
	END_TIMER;
	debug3("rsmi_dev_gpu_clk_freq_get() took %ld microseconds",
	       DELTA_TIMER);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get memory frequencies error: %s",
		      status_string);
		return false;
	}

	*mem_freqs_size = rsmi_freqs.num_supported;
	for (int i = 0; i < *mem_freqs_size; i++)
		mem_freqs[i] = rsmi_freqs.frequency[i]/1000000;

	return true;
}

/*
 * Get all possible graphics frequencies for the device
 *
 * dv_ind         (IN) The device index
 * gfx_freqs_size (IN/OUT) The size of the gfx_freqs array; this will
 *                be overwritten with the number of graphics freqs found.
 * gfx_freqs      (OUT) The possible graphics frequencies in  MHz.
 *
 * Return true if successful, false if not.
 */
static bool _rsmi_get_gfx_freqs(uint32_t dv_ind,
				unsigned int *gfx_freqs_size,
				unsigned int *gfx_freqs)
{
	const char *status_string;
	rsmi_status_t rsmi_rc;
	rsmi_frequencies_t rsmi_freqs;

	DEF_TIMERS;
	START_TIMER;
	rsmi_rc = rsmi_dev_gpu_clk_freq_get(
		dv_ind, RSMI_CLK_TYPE_SYS, &rsmi_freqs);
	END_TIMER;
	debug3("rsmi_dev_gpu_clk_freq_get() took %ld microseconds",
	       DELTA_TIMER);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get graphics frequencies error: %s",
		      status_string);
		return false;
	}

	*gfx_freqs_size = rsmi_freqs.num_supported;
	for (int i = 0; i < *gfx_freqs_size; i++)
		gfx_freqs[i] = rsmi_freqs.frequency[i]/1000000;

	return true;
}

/*
 * Print out all possible memory and graphics frequencies for the given device.
 * If there are more than FREQS_CONCISE frequencies, prints a summary instead
 *
 * dv_ind (IN) The device index
 * l      (IN) The log level at which to print
 */
static void _rsmi_print_freqs(uint32_t dv_ind, log_level_t l)
{
	unsigned int mem_freqs[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int gfx_freqs[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int size = RSMI_MAX_NUM_FREQUENCIES;
	bool concise = false;
	unsigned int i;

	if (!_rsmi_get_mem_freqs(dv_ind, &size, mem_freqs))
		return;

	qsort(mem_freqs, size,
	      sizeof(unsigned int), _sort_freq_descending);
	if ((size > 1) && (mem_freqs[0] <= mem_freqs[(size)-1])) {
		error("%s: memory frequencies are not stored in descending order!",
		      __func__);
		return;
	}

	if (size > FREQS_CONCISE)
		concise = true;

	log_var(l, "        Possible GPU Memory Frequencies (%u):", size);
	log_var(l, "        ---------------------------------");
	if (!concise) {
		for (i = 0; i < size; ++i)
			log_var(l, "          *%u MHz [%u]", mem_freqs[i], i);
	} else {
		// first, next, ..., middle, ..., penultimate, last
		log_var(l, "          *%u MHz [0]", mem_freqs[0]);
		log_var(l, "          *%u MHz [1]", mem_freqs[1]);
		log_var(l, "          ...");
		log_var(l, "          *%u MHz [%u]", mem_freqs[(size - 1) / 2],
			(size - 1) / 2);
		log_var(l, "          ...");
		log_var(l, "          *%u MHz [%u]",
			mem_freqs[size - 2], size - 2);
		log_var(l, "          *%u MHz [%u]",
			mem_freqs[size - 1], size - 1);
	}

	size = RSMI_MAX_NUM_FREQUENCIES;
	if (!_rsmi_get_gfx_freqs(dv_ind, &size, gfx_freqs))
		return;

	qsort(gfx_freqs, size,
	      sizeof(unsigned int), _sort_freq_descending);
	if ((size > 1) && (gfx_freqs[0] <= gfx_freqs[(size)-1])) {
		error("%s: Graphics frequencies are not stored in descending order!",
		      __func__);
		return;
	}

	if (size > FREQS_CONCISE)
		concise = true;

	log_var(l, "        Possible GPU Graphics Frequencies (%u):", size);
	log_var(l, "        ---------------------------------");
	if (!concise) {
		for (i = 0; i < size; ++i)
			log_var(l, "          *%u MHz [%u]", gfx_freqs[i], i);
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
 * Convert frequency to nearest valid frequency found in frequency array
 *
 * freq		(IN/OUT) The frequency to check, in MHz. Also the output, if
 *		it needs to be changed.
 * freqs_size	(IN) The size of the freqs array
 * freqs	(IN) An array of frequency values in MHz, sorted highest to
 *		lowest
 *
 * Inspired by src/common/cpu_frequency#_cpu_freq_freqspec_num()
 */
//TODO: Duplicated from NVML plugin. Move to a common directory
static void _get_nearest_freq(unsigned int *freq, unsigned int freqs_size,
			      unsigned int *freqs)
{
	unsigned int i;

	if (!freq || !(*freq)) {
		log_flag(GRES, "%s: No frequency supplied", __func__);
		return;
	}
	if (!freqs || !(*freqs)) {
		log_flag(GRES, "%s: No frequency list supplied", __func__);
		return;
	}
	if (freqs_size <= 0) {
		log_flag(GRES, "%s: Frequency list is empty", __func__);
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
		log_flag(GRES, "Rounding frequency %u MHz down to %u MHz",
		         *freq, freqs[0]);
		*freq = freqs[0];
		return;
	} else if (*freq < freqs[freqs_size - 1]) {
		log_flag(GRES, "Rounding frequency %u MHz up to %u MHz",
		         *freq, freqs[freqs_size - 1]);
		*freq = freqs[freqs_size - 1];
		return;
	}

	/* check for frequency, and round up if no exact match */
	for (i = 0; i < freqs_size - 1;) {
		if (*freq == freqs[i]) {
			// No change necessary
			debug2("No change necessary. Freq: %u MHz", *freq);
			return;
		}
		i++;
		/*
		 * Step down to next element to round up.
		 * Safe to advance due to bounds checks above here
		 */
		if (*freq > freqs[i]) {
			log_flag(GRES, "Rounding frequency %u MHz up to %u MHz",
			         *freq, freqs[i - 1]);
			*freq = freqs[i - 1];
			return;
		}
	}
	error("%s: Got to the end of the function. Freq: %u MHz",
	      __func__, *freq);
}

/*
 * Get the nearest valid memory and graphics frequencies
 * Return bit masks indicating the indices of the
 * frequencies that are to be enabled (1) and disabled (0).
 *
 * dv_ind      (IN) the device index
 * mem_freq    (IN/OUT) requested/nearest valid memory frequency
 * mem_bitmask (OUT) bit mask for the nearest valid memory frequency
 * gfx_freq    (IN/OUT) requested/nearest valid graphics frequency
 * gfx_bitmask (OUT) bit mask for the nearest valid graphics frequency
 */
static void _rsmi_get_nearest_freqs(uint32_t dv_ind,
				    unsigned int *mem_freq,
				    uint64_t *mem_bitmask,
				    unsigned int *gfx_freq,
				    uint64_t *gfx_bitmask)
{
	unsigned int mem_freqs[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int mem_freqs_sort[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int mem_freqs_size = RSMI_MAX_NUM_FREQUENCIES;
	unsigned int gfx_freqs[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int gfx_freqs_sort[RSMI_MAX_NUM_FREQUENCIES] = {0};
	unsigned int gfx_freqs_size = RSMI_MAX_NUM_FREQUENCIES;

	// Get the memory frequencies
	if (!_rsmi_get_mem_freqs(dv_ind, &mem_freqs_size, mem_freqs))
		return;

	memcpy(mem_freqs_sort, mem_freqs, mem_freqs_size*sizeof(unsigned int));
	qsort(mem_freqs_sort, mem_freqs_size,
	      sizeof(unsigned int), _sort_freq_descending);
	if ((mem_freqs_size > 1) &&
	    (mem_freqs_sort[0] <= mem_freqs_sort[(mem_freqs_size)-1])) {
		error("%s: memory frequencies are not stored in descending order!",
		      __func__);
		return;
	}

	// Set the nearest valid memory frequency for the requested frequency
	_get_nearest_freq(mem_freq, mem_freqs_size, mem_freqs_sort);

	// convert the frequency to bit mask
	for (int i = 0; i < mem_freqs_size; i++)
		if (*mem_freq == mem_freqs[i]) {
			*mem_bitmask = (1 << i);
			break;
		}

	// Get the graphics frequencies
	if (!_rsmi_get_gfx_freqs(dv_ind, &gfx_freqs_size, gfx_freqs))
		return;

	memcpy(gfx_freqs_sort, gfx_freqs, gfx_freqs_size*sizeof(unsigned int));
	qsort(gfx_freqs_sort, gfx_freqs_size,
	      sizeof(unsigned int), _sort_freq_descending);
	if ((gfx_freqs_size > 1) &&
	    (gfx_freqs_sort[0] <= gfx_freqs_sort[(gfx_freqs_size)-1])) {
		error("%s: graphics frequencies are not stored in descending order!",
		      __func__);
		return;
	}

	// Set the nearest valid graphics frequency for the requested frequency
	_get_nearest_freq(gfx_freq, gfx_freqs_size, gfx_freqs_sort);

	// convert the frequency to bit mask
	for (int i = 0; i < gfx_freqs_size; i++)
		if (*gfx_freq == gfx_freqs[i]) {
			*gfx_bitmask = (1 << i);
			break;
		}
}

/*
 * Set the memory and graphics clock frequencies for the GPU
 *
 * dv_ind      (IN) The device index
 * mem_bitmask (IN) bit mask for the memory frequency.
 * gfx_bitmask (IN) bit mask for the graphics frequency.
 *
 * Returns true if successful, false if not
 */
static bool _rsmi_set_freqs(uint32_t dv_ind, uint64_t mem_bitmask,
			    uint64_t gfx_bitmask)
{
	const char *status_string;
	rsmi_status_t rsmi_rc;

	DEF_TIMERS;
	START_TIMER;
	rsmi_rc = rsmi_dev_gpu_clk_freq_set(
		dv_ind, RSMI_CLK_TYPE_MEM, mem_bitmask);
	END_TIMER;
	debug3("rsmi_dev_gpu_clk_freq_set(0x%lx) for memory took %ld microseconds",
	       mem_bitmask, DELTA_TIMER);
	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to set memory frequency GPU %u error: %s",
		      dv_ind, status_string);
		return false;
	}

	START_TIMER;
	rsmi_rc = rsmi_dev_gpu_clk_freq_set(dv_ind,
					    RSMI_CLK_TYPE_SYS, gfx_bitmask);
	debug3("rsmi_dev_gpu_clk_freq_set(0x%lx) for graphics took %ld microseconds",
	       gfx_bitmask, DELTA_TIMER);
	END_TIMER;
	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to set graphic frequency GPU %u error: %s",
		      dv_ind, status_string);
		return false;
	}
	return true;
}

/*
 * Reset the memory and graphics clock frequencies for the GPU to the same
 * default frequencies that are used after system reboot or driver reload. This
 * default cannot be changed.
 *
 * dv_ind	(IN) The device index
 *
 * Returns true if successful, false if not
 */
static bool _rsmi_reset_freqs(uint32_t dv_ind)
{
	const char *status_string;
	rsmi_status_t rsmi_rc;

	DEF_TIMERS;

	START_TIMER;
	rsmi_rc = rsmi_dev_perf_level_set(dv_ind, RSMI_DEV_PERF_LEVEL_AUTO);
	END_TIMER;
	debug3("rsmi_dev_perf_level_set() took %ld microseconds",
	       DELTA_TIMER);
	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to reset frequencies error: %s",
		      status_string);
		return false;
	}
	return true;
}

/*
 * Get the memory or graphics clock frequency that the GPU is currently running
 * at
 *
 * dv_ind	(IN) The device index
 * type		(IN) The clock type to query. Either RSMI_CLK_TYPE_SYS or
 *		RSMI_CLK_TYPE_MEM.
 *
 * Returns the clock frequency in MHz if successful, or 0 if not
 */
static unsigned int _rsmi_get_freq(uint32_t dv_ind, rsmi_clk_type_t type)
{
	const char *status_string;
	rsmi_status_t rsmi_rc;
	rsmi_frequencies_t rsmi_freqs;
	char *type_str = "unknown";

	DEF_TIMERS;

	switch (type) {
	case RSMI_CLK_TYPE_SYS:
		type_str = "graphics";
		break;
	case RSMI_CLK_TYPE_MEM:
		type_str = "memory";
		break;
	default:
		error("%s: Unsupported clock type", __func__);
		break;
	}

	START_TIMER;
	rsmi_rc = rsmi_dev_gpu_clk_freq_get(dv_ind, type, &rsmi_freqs);
	END_TIMER;
	debug3("rsmi_dev_gpu_clk_freq_get(%s) took %ld microseconds",
	       type_str, DELTA_TIMER);
	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get the GPU frequency type %s, error: %s",
		      type_str, status_string);
		return 0;
	}
	return (rsmi_freqs.frequency[rsmi_freqs.current]/1000000);
}

static unsigned int _rsmi_get_gfx_freq(uint32_t dv_ind)
{
	return _rsmi_get_freq(dv_ind, RSMI_CLK_TYPE_SYS);
}

static unsigned int _rsmi_get_mem_freq(uint32_t dv_ind)
{
	return _rsmi_get_freq(dv_ind, RSMI_CLK_TYPE_MEM);
}

/*
 * Convert a frequency value to a string
 * Returned string must be xfree()'ed
 */
//TODO: Duplicated from NVML plugin. Move to a common directory
static char *_freq_value_to_string(unsigned int freq)
{
	switch (freq) {
	case GPU_LOW:
		return xstrdup("low");
	case GPU_MEDIUM:
		return xstrdup("medium");
	case GPU_HIGH:
		return xstrdup("high");
	case GPU_HIGH_M1:
		return xstrdup("highm1");
	default:
		return xstrdup_printf("%u", freq);
	}
}

/*
 * Reset the frequencies of each GPU in the step to the hardware default
 * NOTE: RSMI must be initialized beforehand
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate.
 */
static void _reset_freq(bitstr_t *gpus)
{
	int gpu_len = bit_size(gpus);
	int i = -1, count = 0, count_set = 0;
	bool freq_reset = false;

	// Reset the frequency of each device allocated to the step
	for (i = 0; i < gpu_len; i++) {
		if (!bit_test(gpus, i))
			continue;
		count++;

		debug2("Memory frequency before reset: %u",
		       _rsmi_get_mem_freq(i));
		debug2("Graphics frequency before reset: %u",
		       _rsmi_get_gfx_freq(i));
		freq_reset = _rsmi_reset_freqs(i);
		debug2("Memory frequency after reset: %u",
		       _rsmi_get_mem_freq(i));
		debug2("Graphics frequency after reset: %u",
		       _rsmi_get_gfx_freq(i));

		// TODO: Check to make sure that the frequency reset

		if (freq_reset) {
			log_flag(GRES, "Successfully reset GPU[%d]", i);
			count_set++;
		} else {
			log_flag(GRES, "Failed to reset GPU[%d]", i);
		}
	}

	if (count_set != count) {
		log_flag(GRES, "%s: Could not reset frequencies for all GPUs %d/%d total GPUs",
		         __func__, count_set, count);
		fprintf(stderr, "Could not reset frequencies for all GPUs %d/%d total GPUs\n",
			count_set, count);
	}
}

/*
 * Set the frequencies of each GPU specified for the step
 * NOTE: RSMI must be initialized beforehand
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate.
 * gpu_freq	(IN) The frequencies to set each of the GPUs to. If a NULL or
 *		empty memory or graphics frequency is specified, then GpuFreqDef
 *		will be consulted, which defaults to "high,memory=high" if not
 *		set.
 */
static void _set_freq(bitstr_t *gpus, char *gpu_freq)
{
	bool verbose_flag = false;
	int gpu_len = 0;
	int i = -1, count = 0, count_set = 0;
	unsigned int gpu_freq_num = 0, mem_freq_num = 0;
	uint64_t mem_bitmask = 0, gpu_bitmask = 0;
	bool freq_set = false, freq_logged = false;
	char *tmp = NULL;
	bool task_cgroup = false;
	bool constrained_devices = false;
	bool cgroups_active = false;

	// Parse frequency information
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
	cgroup_conf_init();
	if (slurm_cgroup_conf.constrain_devices)
		constrained_devices = true;

	// Check if task/cgroup plugin is loaded
	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		task_cgroup = true;

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

	// Set the frequency of each device allocated to the step
	for (i = 0; i < gpu_len; i++) {
		char *sep = "";

		// Only check the global GPU bitstring if not using cgroups
		if (!cgroups_active && !bit_test(gpus, i)) {
			debug2("Passing over RSMI device %u", i);
			continue;
		}
		count++;

		debug2("Setting frequency of RSMI device %u", i);
		_rsmi_get_nearest_freqs(i, &mem_freq_num, &mem_bitmask,
					&gpu_freq_num, &gpu_bitmask);

		debug2("Memory frequency before set: %u",
		       _rsmi_get_mem_freq(i));
		debug2("Graphics frequency before set: %u",
		       _rsmi_get_gfx_freq(i));
		freq_set = _rsmi_set_freqs(i, mem_bitmask, gpu_bitmask);
		debug2("Memory frequency after set: %u",
		       _rsmi_get_mem_freq(i));
		debug2("Graphics frequency after set: %u",
		       _rsmi_get_gfx_freq(i));

		if (mem_freq_num) {
			xstrfmtcat(tmp, "%smemory_freq:%u", sep, mem_freq_num);
			sep = ",";
		}
		if (gpu_freq_num) {
			xstrfmtcat(tmp, "%sgraphics_freq:%u", sep,
				   gpu_freq_num);
		}

		if (freq_set) {
			log_flag(GRES, "Successfully set GPU[%d] %s", i, tmp);
			count_set++;
		} else {
			log_flag(GRES, "Failed to set GPU[%d] %s", i, tmp);
		}

		if (verbose_flag && !freq_logged) {
			fprintf(stderr, "GpuFreq=%s\n", tmp);
			freq_logged = true;	/* Just log for first GPU */
		}
		xfree(tmp);
	}

	if (count_set != count) {
		log_flag(GRES, "%s: Could not set frequencies for all GPUs %d/%d total GPUs",
		         __func__, count_set, count);
		fprintf(stderr, "Could not set frequencies for all GPUs %d/%d total GPUs\n",
			count_set, count);
	}
}

/*
 * Get the version of the AMD Graphics driver
 *
 * driver	(OUT) A string to return version of AMD GPU driver
 * len		(OUT) Length for version of AMD GPU driver
 */
static void _rsmi_get_driver(char *driver, unsigned int len)
{
	rsmi_version_str_get(RSMI_SW_COMP_DRIVER, driver, len);
}

/*
 * Get the version of the ROCM-SMI library
 *
 * version	(OUT) A string to return version of RSMI
 * len		(OUT) Length for version of RSMI
 */
static void _rsmi_get_version(char *version, unsigned int len)
{
	const char *status_string;
	rsmi_version_t rsmi_version;
	rsmi_status_t rsmi_rc = rsmi_version_get(&rsmi_version);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get the version error: %s",
		      status_string);
		version[0] = '\0';
	} else
		sprintf(version, "%s", rsmi_version.build);
}

/*
 * Get the total # of GPUs in the system
 *
 * device_count	(OUT) Number of available GPU devices
 */
static void _rsmi_get_device_count(unsigned int *device_count)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_num_monitor_devices(device_count);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get device count: %s", status_string);
		*device_count = 0;
	}
}

/*
 * Get the name of the GPU
 *
 * dv_ind	(IN) The device index
 * device_name	(OUT) Name of GPU devices
 * size		(OUT) Size of name
 */
static void _rsmi_get_device_name(uint32_t dv_ind, char *device_name,
				  unsigned int size)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_dev_name_get(dv_ind, device_name, size);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get name of the GPU: %s", status_string);
	}
}

/*
 * Get the brand of the GPU
 *
 * dv_ind	(IN) The device index
 * device_brand	(OUT) Brand of GPU devices
 * size		(OUT) Size of name
 */
static void _rsmi_get_device_brand(uint32_t dv_ind, char *device_brand,
				   unsigned int size)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_dev_brand_get(dv_ind, device_brand, size);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get brand of the GPU: %s",
		      status_string);
	}
}

/*
 * Retrieves minor number of the render device. Each AMD GPU will have a device node file
 * in form /dev/dri/renderD[minor_number].
 *
 * dv_ind	(IN) The device index
 * minor	(OUT) minor number of device node
 */
static void _rsmi_get_device_minor_number(uint32_t dv_ind,
					  unsigned int *minor)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_dev_drm_render_minor_get(dv_ind, minor);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get minor number of GPU: %s",
		      status_string);
	}
}

/*
 * Get the PCI Info of the GPU
 *
 * dv_ind		(IN) The device index
 * pci			(OUT) PCI Info of GPU devices
 */
static void _rsmi_get_device_pci_info(uint32_t dv_ind, rsmiPciInfo_t *pci)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_dev_pci_id_get(dv_ind, &(pci->bdfid));

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get PCI Info of the GPU: %s",
		      status_string);
	}
}

/*
 * Get the Unique ID of the GPU
 *
 * dv_ind		(IN) The device index
 * id			(OUT) Unique ID of GPU devices
 */
static void _rsmi_get_device_unique_id(uint32_t dv_ind, uint64_t *id)
{
	const char *status_string;
	rsmi_status_t rsmi_rc = rsmi_dev_unique_id_get(dv_ind, id);

	if (rsmi_rc != RSMI_STATUS_SUCCESS) {
		rsmi_rc = rsmi_status_string(rsmi_rc, &status_string);
		error("RSMI: Failed to get Unique ID of the GPU: %s",
		      status_string);
	}
}

/*
 * Creates and returns a gres conf list of detected AMD gpus on the node.
 * If an error occurs, return NULL
 * Caller is responsible for freeing the list.
 *
 * If the AMD ROCM-SMI API exists, then query GPU info,
 * so the user doesn't need to specify manually in gres.conf.
 *
 * node_config (IN/OUT) pointer of node_config_load_t passed down
 */
static List _get_system_gpu_list_rsmi(node_config_load_t *node_config)
{
	unsigned int i;
	unsigned int device_count = 0;
	List gres_list_system = list_create(destroy_gres_slurmd_conf);
	char driver[RSMI_STRING_BUFFER_SIZE];
	char version[RSMI_STRING_BUFFER_SIZE];

	rsmi_init(0);

	_rsmi_get_driver(driver, RSMI_STRING_BUFFER_SIZE);
	_rsmi_get_version(version, RSMI_STRING_BUFFER_SIZE);
	debug("AMD Graphics Driver Version: %s", driver);
	debug("RSMI Library Version: %s", version);

	_rsmi_get_device_count(&device_count);
	debug2("Device count: %d", device_count);

	// Loop through all the GPUs on the system and add to gres_list_system
	for (i = 0; i < device_count; ++i) {
		unsigned int minor_number = 0;
		char *device_file = NULL, *links = NULL;
		char device_name[RSMI_STRING_BUFFER_SIZE] = {0};
		char device_brand[RSMI_STRING_BUFFER_SIZE] = {0};
		rsmiPciInfo_t pci_info;
		uint64_t uuid = 0;

		_rsmi_get_device_name(i, device_name, RSMI_STRING_BUFFER_SIZE);
		_rsmi_get_device_brand(i, device_brand,
				       RSMI_STRING_BUFFER_SIZE);
		_rsmi_get_device_minor_number(i, &minor_number);
		pci_info.bdfid = 0;
		_rsmi_get_device_pci_info(i, &pci_info);
		_rsmi_get_device_unique_id(i, &uuid);

		/* Use links to record PCI bus ID order */
		links = gres_links_create_empty(i, device_count);

		xstrfmtcat(device_file, "/dev/dri/renderD%u", minor_number);

		debug2("GPU index %u:", i);
		debug2("    Name: %s", device_name);
		debug2("    Brand/Type: %s", device_brand);
		debug2("    UUID: %lx", uuid);
		debug2("    PCI Domain/Bus/Device/Function: %u:%u:%u.%u",
		       pci_info.domain,
		       pci_info.bus, pci_info.device, pci_info.function);
		debug2("    Links: %s", links);
		debug2("    Device File (minor number): %s", device_file);
		if (minor_number != i+128)
			debug("Note: GPU index %u is different from minor # %u",
			      i, minor_number);

		// Print out possible memory frequencies for this device
		_rsmi_print_freqs(i, LOG_LEVEL_DEBUG2);

		add_gres_to_list(gres_list_system, "gpu", 1,
				 node_config->cpu_cnt, NULL, NULL,
				 device_file, device_brand, links, NULL,
				 GRES_CONF_ENV_RSMI);

		xfree(device_file);
		xfree(links);
	}

	rsmi_shut_down();

	info("%u GPU system device(s) detected", device_count);
	return gres_list_system;
}

extern int gpu_p_reconfig(void)
{
	return SLURM_SUCCESS;
}

extern List gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	List gres_list_system = _get_system_gpu_list_rsmi(node_config);

	if (!gres_list_system)
		error("System GPU detection failed");

	return gres_list_system;
}

extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	char *freq = NULL;
	char *tmp = NULL;

	xassert(tres_freq);
	xassert(usable_gpus);

	if (!usable_gpus)
		return;		/* Job allocated no GPUs */
	if (!tres_freq)
		return;		/* No TRES frequency spec */

	tmp = strstr(tres_freq, "gpu:");
	if (!tmp)
		return;		/* No GPU frequency spec */

	freq = xstrdup(tmp + 4);
	tmp = strchr(freq, ';');
	if (tmp)
		tmp[0] = '\0';

	// Save a copy of the GPUs affected, so we can reset things afterwards
	FREE_NULL_BITMAP(saved_gpus);
	saved_gpus = bit_copy(usable_gpus);

	rsmi_init(0);

	// Set the frequency of each GPU index specified in the bitstr
	_set_freq(usable_gpus, freq);
	xfree(freq);

}

extern void gpu_p_step_hardware_fini(void)
{
	if (!saved_gpus)
		return;

	// Reset the frequencies back to the hardware default
	_reset_freq(saved_gpus);
	FREE_NULL_BITMAP(saved_gpus);
	rsmi_shut_down();
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}
