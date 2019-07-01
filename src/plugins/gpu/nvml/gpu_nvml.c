/*****************************************************************************\
 *  gpu_nvml.c - Support nvml interface to an Nvidia GPU.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
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

#define _GNU_SOURCE

#include "src/common/slurm_xlator.h"
#include "src/common/gres.h"
#include "src/common/log.h"
#include "src/common/list.h"

#include <nvml.h>

/*
 * #defines needed to test nvml.
 */
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

#define GPU_LOW		((unsigned int) -1)
#define GPU_MEDIUM	((unsigned int) -2)
#define GPU_HIGH_M1	((unsigned int) -3)
#define GPU_HIGH	((unsigned int) -4)

static bitstr_t	*saved_gpus = NULL;
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
const char	*plugin_name		= "GPU NVML plugin";
const char	*plugin_type		= "gpu/nvml";
const uint32_t	plugin_version		= SLURM_VERSION_NUMBER;
static log_level_t log_lvl              = LOG_LEVEL_QUIET;


/*
 * Converts a cpu_set returned from the NVML API into a Slurm bitstr_t
 *
 * This function accounts for the endianess of the machine.
 *
 * cpu_set_bitstr: (IN/OUT) A preallocated bitstr_t via bit_alloc() that is
 * 		   bitstr_size bits wide. This will get filled in.
 * cpu_set:	   (IN) The cpu_set array returned by nvmlDeviceGetCpuAffinity()
 * cpu_set_size:   (IN) The size of the cpu_set array
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

	xassert(bit_cur == -1);
	// If NVML gave us an empty CPU affinity, then something is very wrong
	if (bit_set_count(cpu_set_bitstr) == 0)
		fatal("%s: cpu_set_bitstr is empty! No CPU affinity for device",
		      __func__);
}


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

static int _sort_freq_descending(const void *a, const void *b)
{
	return (*(unsigned long*)b - *(unsigned long*)a);
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
static bool _nvml_get_mem_freqs(nvmlDevice_t device,
				unsigned int *mem_freqs_size,
				unsigned int *mem_freqs)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceGetSupportedMemoryClocks(device, mem_freqs_size,
						     mem_freqs);
	END_TIMER;
	debug3("nvmlDeviceGetSupportedMemoryClocks() took %ld microseconds",
	       DELTA_TIMER);

	if (nvml_rc != NVML_SUCCESS) {
		error("%s: Failed to get supported memory frequencies for the "
		      "GPU : %s", __func__, nvmlErrorString(nvml_rc));
		return false;
	}

	qsort(mem_freqs, *mem_freqs_size,
	      sizeof(unsigned int), _sort_freq_descending);

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
static bool _nvml_get_gfx_freqs(nvmlDevice_t device,
				unsigned int mem_freq,
				unsigned int *gfx_freqs_size,
				unsigned int *gfx_freqs)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceGetSupportedGraphicsClocks(device, mem_freq,
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

	qsort(gfx_freqs, *gfx_freqs_size,
	      sizeof(unsigned int), _sort_freq_descending);

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
static void _nvml_print_gfx_freqs(nvmlDevice_t device, unsigned int mem_freq,
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
static void _nvml_print_freqs(nvmlDevice_t device, log_level_t l)
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
 *
 * Inspired by src/common/cpu_frequency#_cpu_freq_freqspec_num()
 */
static void _get_nearest_freq(unsigned int *freq, unsigned int freqs_size,
			      unsigned int *freqs)
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
 */
static void _nvml_get_nearest_freqs(nvmlDevice_t device,
				    unsigned int *mem_freq,
				    unsigned int *gfx_freq)
{
	unsigned int mem_freqs[FREQS_SIZE] = {0};
	unsigned int mem_freqs_size = FREQS_SIZE;
	unsigned int gfx_freqs[FREQS_SIZE] = {0};
	unsigned int gfx_freqs_size = FREQS_SIZE;

	// Get the memory frequencies
	if (!_nvml_get_mem_freqs(device, &mem_freqs_size, mem_freqs))
		return;

	// Set the nearest valid memory frequency for the requested frequency
	_get_nearest_freq(mem_freq, mem_freqs_size, mem_freqs);

	// Get the graphics frequencies at this memory frequency
	if (!_nvml_get_gfx_freqs(device, *mem_freq, &gfx_freqs_size, gfx_freqs))
		return;
	// Set the nearest valid graphics frequency for the requested frequency
	_get_nearest_freq(gfx_freq, gfx_freqs_size, gfx_freqs);
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
static bool _nvml_set_freqs(nvmlDevice_t device, unsigned int mem_freq,
			    unsigned int gfx_freq)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;
	START_TIMER;
	nvml_rc = nvmlDeviceSetApplicationsClocks(device, mem_freq, gfx_freq);
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
static bool _nvml_reset_freqs(nvmlDevice_t device)
{
	nvmlReturn_t nvml_rc;
	DEF_TIMERS;

	START_TIMER;
	nvml_rc = nvmlDeviceResetApplicationsClocks(device);
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
static unsigned int _nvml_get_freq(nvmlDevice_t device, nvmlClockType_t type)
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
	nvml_rc = nvmlDeviceGetApplicationsClock(device, type, &freq);
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

static unsigned int _nvml_get_gfx_freq(nvmlDevice_t device)
{
	return _nvml_get_freq(device, NVML_CLOCK_GRAPHICS);
}

static unsigned int _nvml_get_mem_freq(nvmlDevice_t device)
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
 */
static void _reset_freq(bitstr_t *gpus)
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
		       _nvml_get_mem_freq(device));
		debug2("Graphics frequency before reset: %u",
		       _nvml_get_gfx_freq(device));
		freq_reset =_nvml_reset_freqs(device);
		debug2("Memory frequency after reset: %u",
		       _nvml_get_mem_freq(device));
		debug2("Graphics frequency after reset: %u",
		       _nvml_get_gfx_freq(device));

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
 */
static void _set_freq(bitstr_t *gpus, char *gpu_freq)
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
		_nvml_get_nearest_freqs(device, &mem_freq_num, &gpu_freq_num);

		debug2("Memory frequency before set: %u",
		       _nvml_get_mem_freq(device));
		debug2("Graphics frequency before set: %u",
		       _nvml_get_gfx_freq(device));
		freq_set = _nvml_set_freqs(device, mem_freq_num, gpu_freq_num);
		debug2("Memory frequency after set: %u",
		       _nvml_get_mem_freq(device));
		debug2("Graphics frequency after set: %u",
		       _nvml_get_gfx_freq(device));

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
static void _nvml_get_device_name(nvmlDevice_t device, char *device_name,
				 unsigned int size)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetName(device, device_name, size);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get name of the GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Allocates a string in device_brand containing the brand/type of the GPU
 * Returned string must be xfree()'d
 */
static char *_nvml_get_device_brand(nvmlDevice_t device)
{
	nvmlBrandType_t brand = NVML_BRAND_UNKNOWN;
	char *device_brand = NULL;
	nvmlReturn_t nvml_rc = nvmlDeviceGetBrand(device, &brand);
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
static void _nvml_get_device_uuid(nvmlDevice_t device, char *uuid,
				  unsigned int len)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetUUID(device, uuid, len);
	if (nvml_rc != NVML_SUCCESS) {
		error("NVML: Failed to get UUID of GPU: %s",
		      nvmlErrorString(nvml_rc));
	}
}

/*
 * Get the PCI Bus ID of the device, since device index can fluctuate
 */
static void _nvml_get_device_pci_info(nvmlDevice_t device, nvmlPciInfo_t *pci)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetPciInfo(device, pci);
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
static void _nvml_get_device_minor_number(nvmlDevice_t device,
					 unsigned int *minor)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetMinorNumber(device, minor);
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
static void _nvml_get_device_affinity(nvmlDevice_t device, unsigned int size,
				      unsigned long *cpu_set)
{
	nvmlReturn_t nvml_rc = nvmlDeviceGetCpuAffinity(device, size, cpu_set);
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
static char *_nvml_get_nvlink_remote_pcie(nvmlDevice_t device,
					  unsigned int lane)
{
	nvmlPciInfo_t pci_info;
	nvmlReturn_t nvml_rc;

	memset(&pci_info, 0, sizeof(pci_info));
	nvml_rc = nvmlDeviceGetNvLinkRemotePciInfo(device, lane, &pci_info);
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
static char *_nvml_get_nvlink_info(nvmlDevice_t device, int index,
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
		nvml_rc = nvmlDeviceGetNvLinkState(device, i, &is_active);
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
	List gres_list_system = list_create(destroy_gres_slurmd_conf);
	char driver[NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE];
	char version[NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE];
	char **device_lut;
	nvmlPciInfo_t pci_info;

	xassert(node_config->xcpuinfo_mac_to_abs);

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

		if (!_nvml_get_handle(i, &device))
			continue;

		memset(&pci_info, 0, sizeof(pci_info));
		_nvml_get_device_pci_info(device, &pci_info);
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
		char *device_file = NULL;
		char *nvlinks = NULL;
		char device_name[NVML_DEVICE_NAME_BUFFER_SIZE] = {0};
		char *device_brand = NULL;

		if (!_nvml_get_handle(i, &device)) {
			error("Creating null GRES GPU record");
			add_gres_to_list(gres_list_system, "gpu", 1,
					 node_config->cpu_cnt, NULL,
					 NULL, NULL, NULL);
			continue;
		}

		memset(&pci_info, 0, sizeof(pci_info));
		_nvml_get_device_name(device, device_name,
				      NVML_DEVICE_NAME_BUFFER_SIZE);
		_nvml_get_device_uuid(device, uuid,
				      NVML_DEVICE_UUID_BUFFER_SIZE);
		_nvml_get_device_pci_info(device, &pci_info);
		_nvml_get_device_minor_number(device, &minor_number);
		_nvml_get_device_affinity(device, CPU_SET_SIZE, cpu_set);

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

		nvlinks = _nvml_get_nvlink_info(device, i, device_lut,
						device_count);
		device_brand = _nvml_get_device_brand(device);
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
		_nvml_print_freqs(device, LOG_LEVEL_DEBUG2);

		add_gres_to_list(gres_list_system, "gpu", 1,
				 node_config->cpu_cnt, cpu_aff_abs_range,
				 device_file, device_brand, nvlinks);

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

extern int init(void)
{
	debug("%s: %s loaded", __func__, plugin_name);

	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern int gpu_p_reconfig(void)
{
	if (slurm_get_debug_flags() & DEBUG_FLAG_GRES)
		log_lvl = LOG_LEVEL_INFO;
	else
		log_lvl = LOG_LEVEL_QUIET;

	return SLURM_SUCCESS;
}


extern List gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	List gres_list_system = NULL;

	if (!(gres_list_system = _get_system_gpu_list_nvml(node_config)))
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

	if (!(tmp = strstr(tres_freq, "gpu:")))
		return;		/* No GPU frequency spec */

	freq = xstrdup(tmp + 4);
	if ((tmp = strchr(freq, ';')))
		tmp[0] = '\0';

	// Save a copy of the GPUs affected, so we can reset things afterwards
	FREE_NULL_BITMAP(saved_gpus);
	saved_gpus = bit_copy(usable_gpus);

	_nvml_init();
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
	_nvml_shutdown();
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
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

	bit_free(cpu_aff_mac_bitstr);
	return result;
}
