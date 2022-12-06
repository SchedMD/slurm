/*****************************************************************************\
 *  gpu_oneapi.c - Support oneAPI interface to an Intel GPU.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Copyright (C) 2022 Intel Corporation
 *  Written by Kemp Ke <kemp.ke@intel.com>
 *  Based on gpu_nvml.c, written by Danny Auble <da@schedmd.com>
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

#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <regex.h>
#include <sys/types.h>
#include <ze_api.h>
#include <zes_api.h>

#include "src/plugins/gpu/common/gpu_common.h"
#include "src/common/strlcpy.h"
#include "src/common/xregex.h"

#define MAX_GPU_NUM 256
#define MAX_NUM_FREQUENCIES 256
#define CPU_LINE_SIZE 256
#define CARD_NAME_LEN 256

#define MAX_CPUS 0x8000
#define ULONG_BYTES (sizeof(unsigned long))
#define ULONG_BITS (ULONG_BYTES * 8)

/*
 * The # of unsigned longs needed to accommodate a bitmask array capable
 * of representing MAX_CPUS cpus (will vary if 32-bit or 64-bit)
 * E.g. for a 130 CPU 64-bit machine: (130 + 63) / 64 = 3.02
 * -> Integer division floor -> 3 ulongs to represent 130 CPUs
 */
#define CPU_SET_SIZE ((MAX_CPUS + (ULONG_BITS - 1)) / ULONG_BITS)

static bitstr_t	*saved_gpus;

const char plugin_name[] = "GPU oneAPI plugin";
const char plugin_type[] = "gpu/oneapi";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/* Duplicated from NVML plugin */
static void _set_cpu_set_bitstr(bitstr_t *cpu_set_bitstr,
				unsigned long *cpu_set,
				unsigned int cpu_set_size)
{
	int j, k, b;
	int bit_cur;
	int bitstr_bits = (int) bit_size(cpu_set_bitstr);
	int cpu_set_bits = (cpu_set_size * ULONG_BITS);

	/* If this fails, then something went horribly wrong */
	if (bitstr_bits != cpu_set_bits)
		fatal("%s: bitstr_bits != cpu_set_bits", __func__);

	bit_cur = bitstr_bits - 1;

	/* Iterate through each cpu_set long int */
	for (j = cpu_set_size - 1; j >= 0; --j) {
		/* Iterate through the bytes of the jth ulong bitmask */
		char *bitmask = (char *) &cpu_set[j];
#ifdef SLURM_BIGENDIAN
		for (k = 0; k < ULONG_BYTES; ++k) {
#else
		for (k = ULONG_BYTES - 1; k >= 0; --k) {
#endif
			unsigned char byte = bitmask[k];
			unsigned char mask;
			/* If byte is zero, nothing to set */
			if (byte == 0) {
				bit_cur -= 8;
				continue;
			}

			/*
			 * Test each bit of byte, from MSB to LSB.
			 * Set if needed.
			 */
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
	if (bit_set_count(cpu_set_bitstr) == 0)
		fatal("%s: cpu_set_bitstr is empty! No CPU affinity for device",
		      __func__);
}

/*
 * Print GPU driver version and API version
 *
 * driver	(IN) The driver handle
 *
 */
static void _oneapi_print_driver_info(ze_driver_handle_t driver)
{
	ze_driver_properties_t driver_prop;
	ze_api_version_t api_version;
	ze_result_t oneapi_rc;

	/* Print driver version */
	oneapi_rc = zeDriverGetProperties(driver, &driver_prop);
	if (oneapi_rc != ZE_RESULT_SUCCESS)
		error("Failed to get driver properties: 0x%x", oneapi_rc);
	else
		debug("Systems Graphics Driver Version: %u",
		      driver_prop.driverVersion);

	/* Print API version */
	oneapi_rc = zeDriverGetApiVersion(driver, &api_version);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get driver API version: 0x%x", oneapi_rc);
	} else {
		/*
		 * The value is encoded as a 16-bit major and 16-bit minor
		 * part. Split apart when printing.
		 */
		debug("Supported Driver API Version: %u.%u", api_version >> 16,
		      api_version & 0x0000ffff);
	}
}

/*
 * Get all of GPU device handles
 *
 * gpu_handles		(IN/OUT) The device handles
 * gpu_size 		(IN/OUT) The size of the gpu_handles array. This will
 *			be overwritten with the number of device handles found.
 * print_version	(IN) Print driver version and device count information
 *
 */
static void _oneapi_get_device_handles(ze_device_handle_t *gpu_handles,
				       uint32_t *gpu_size,
				       bool print_version)
{
	ze_result_t oneapi_rc;
	uint32_t driver_count = 0;
	int gpu_count = 0;
	uint32_t device_count = 0;
	ze_driver_handle_t *all_drivers = NULL;
	ze_device_handle_t *all_devices = NULL;
	ze_device_properties_t device_properties;
	bool gpu_driver = false;

	/* Get driver count */
	oneapi_rc = zeDriverGet(&driver_count, NULL);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get driver count: 0x%x", oneapi_rc);
		return;
	}

	/* Get drivers */
	all_drivers = xcalloc(driver_count, sizeof(ze_driver_handle_t));
	oneapi_rc = zeDriverGet(&driver_count, all_drivers);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get driver: 0x%x", oneapi_rc);
		return;
	}

	for (int i = 0; i < driver_count; i++) {
		/* Get device count */
		gpu_driver = false;
		device_count = 0;
		oneapi_rc = zeDeviceGet(all_drivers[i], &device_count, NULL);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to get device count: 0x%x", oneapi_rc);
			continue;
		}

		/* Get devices */
		all_devices = xcalloc(device_count,
				      sizeof(ze_device_handle_t));
		oneapi_rc = zeDeviceGet(all_drivers[i], &device_count,
					all_devices);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to get device: 0x%x", oneapi_rc);
			continue;
		}

		for (int j = 0; j < device_count; j++) {
			/* Get device properties */
			oneapi_rc = zeDeviceGetProperties(all_devices[j],
							  &device_properties);
			if (oneapi_rc != ZE_RESULT_SUCCESS) {
				error("Failed to get device property: 0x%x",
				     oneapi_rc);
				continue;
			}

			/* Filter non-GPU devices */
			if (ZE_DEVICE_TYPE_GPU != device_properties.type)
				continue;
			gpu_driver = true;

			/*
			 * If the number of GPU exceeds the buffer length,
			 * return the limited number of devices
			 */
			if (gpu_count + 1 > *gpu_size)
				break;

			gpu_handles[gpu_count++] = all_devices[j];

		}

		xfree(all_devices);

		if (print_version && gpu_driver)
			_oneapi_print_driver_info(all_drivers[i]);
	}

	if (print_version)
		debug2("Device count: %d", gpu_count);

	xfree(all_drivers);
	*gpu_size = gpu_count;
}

/*
 * Get available clocks of a frequency handle
 *
 * freq_handle  (IN) the frequency handle
 * freqs	(IN/OUT) array of frequencies in units of MHz and sorted from
 *		slowest to fastest. if freq_count is less than the number of
 *		frequencies that are available, then only that number of
 *		frequencies will be returned
 * freq_count   (IN/OUT) pointer to the size of freqs.
 *		if freq_count is greater than the number of frequencies
 *		that are available, then it will be updated with the correct
 *		number of frequencies.
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_get_available_clocks(zes_freq_handle_t freq_handle,
					 unsigned int *freqs,
					 uint32_t *freq_count)
{
	double *clocks = NULL;
	ze_result_t oneapi_rc;

	xassert(*freq_count > 0);

	/* Get available clocks */
	clocks = xcalloc(*freq_count, sizeof(double));
	oneapi_rc = zesFrequencyGetAvailableClocks(freq_handle, freq_count,
						   clocks);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get available clocks: 0x%x", oneapi_rc);
		xfree(clocks);
		return false;
	}

	for (int i = 0; i < *freq_count; i++)
		freqs[i] = (unsigned int) clocks[i];

	xfree(clocks);
	return true;
}

/*
 * Get the nearest valid frequencies
 *
 * freq_handle  (IN) the frequency handle
 * freq		(IN/OUT) requested/nearest valid frequency
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_get_nearest_freq(zes_freq_handle_t freq_handle,
				     unsigned int *freq)
{
	unsigned int freqs[MAX_NUM_FREQUENCIES] = {0};
	unsigned int freqs_sort[MAX_NUM_FREQUENCIES] = {0};
	unsigned int freqs_size = MAX_NUM_FREQUENCIES;

	/* Get available clocks */
	if (!_oneapi_get_available_clocks(freq_handle, freqs, &freqs_size))
		return false;

	memcpy(freqs_sort, freqs, freqs_size * sizeof(unsigned int));
	qsort(freqs_sort, freqs_size, sizeof(unsigned int),
	      gpu_common_sort_freq_descending);

	/* Set the nearest valid frequency for the requested frequency */
	gpu_common_get_nearest_freq(freq, freqs_size, freqs_sort);
	return true;
}

/*
 * Print frequency information
 *
 * freq_prop    (IN) The pointer of the frequency property
 * l		(IN) The log level at which to print
 *
 * Returns true if successful, false if not
 */
static void _oneapi_print_freq_info(zes_freq_properties_t *freq_prop,
				    log_level_t l)
{
	if ((freq_prop->type != ZES_FREQ_DOMAIN_GPU) &&
	    (freq_prop->type != ZES_FREQ_DOMAIN_MEMORY))
		return;

	log_var(l, "%s frequency min: %u, max: %u, onSubdevice: %s, subdeviceId: %d, canControl: %s",
		freq_prop->type == ZES_FREQ_DOMAIN_GPU ? "Graphics" : "Memory",
		(unsigned int) freq_prop->min,
		(unsigned int) freq_prop->max,
		freq_prop->onSubdevice ? "true" : "false",
		freq_prop->subdeviceId,
		freq_prop->canControl ? "true" : "false");
}

/*
 * Print out all possible memory and graphics frequencies for the given device
 *
 * device      	(IN) The device handle
 * l		(IN) The log level at which to print
 *
 * Returns true if successful, false if not
 *
 * NOTE: Intel GPU supports tiles. One GPU may have two tiles, so the
 * 	 frequencies of all of tiles needs to be printed.
 */
static void _oneapi_print_freqs(ze_device_handle_t device, log_level_t l)
{
	zes_freq_handle_t freq_handles[MAX_NUM_FREQUENCIES];
	uint32_t freq_handle_size = MAX_NUM_FREQUENCIES;
	zes_freq_properties_t freq_prop;
	ze_result_t oneapi_rc;

	/* Get all of frequency handles */
	oneapi_rc = zesDeviceEnumFrequencyDomains((zes_device_handle_t)device,
						  &freq_handle_size,
						  freq_handles);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to enumerate frequency domains: 0x%x",
		      oneapi_rc);
		return;
	}

	/* Loop all of frequency handles and print frequency */
	for (int i = 0; i < freq_handle_size; i++) {
		unsigned int freqs[MAX_NUM_FREQUENCIES] = {0};
		unsigned int freqs_size = MAX_NUM_FREQUENCIES;

		/* Get available clocks */
		if (!_oneapi_get_available_clocks(freq_handles[i], freqs,
						  &freqs_size))
			continue;
		qsort(freqs, freqs_size, sizeof(unsigned int),
		      gpu_common_sort_freq_descending);

		/* Get frequency property */
		oneapi_rc = zesFrequencyGetProperties(freq_handles[i],
						      &freq_prop);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to get freq properties: 0x%x",
			      oneapi_rc);
			continue;
		}

		_oneapi_print_freq_info(&freq_prop, l);

		if (freq_prop.type == ZES_FREQ_DOMAIN_GPU)
			gpu_common_print_freqs(freqs, freqs_size, l,
					       "GPU Graphics", 8);
		else if (freq_prop.type == ZES_FREQ_DOMAIN_MEMORY)
			gpu_common_print_freqs(freqs, freqs_size, l,
					       "GPU Memory", 8);
		else
			log_var(l, "Unsupported frequency domain: %u",
				freq_prop.type);
	}
}

/*
 * Print current freqeuncy range
 *
 * freq_handler      (IN) the freqeuncy handler
 * freq_type	     (IN) the freqeuncy type
 *
*/
static void _oneapi_print_freq_range(zes_freq_handle_t freq_handler,
				     uint32_t freq_type)
{
	zes_freq_range_t freq_range;
	ze_result_t oneapi_rc;

	if (freq_type != ZES_FREQ_DOMAIN_GPU &&
	    freq_type != ZES_FREQ_DOMAIN_MEMORY)
		return;

	oneapi_rc = zesFrequencyGetRange(freq_handler, &freq_range);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get frequency range");
		return;
	}

	debug2("%s frequency: %u~%u",
		freq_type == ZES_FREQ_DOMAIN_GPU ? "Graphics" :
		"Memory", (unsigned int)freq_range.min,
		(unsigned int)freq_range.max);
}

/*
 * Set frequency for the GPU
 *
 * device      	(IN) The device handle
 * reset       	(IN) If ture, the device will be reset to default frequencies
 * gpu_freq_num (IN) The gpu frequency code. It will be ingorned
		if reset is true.
 * mem_freq_num (IN) The memory frequency code. It will be ingorned
		if reset is true.
 * freq_msg     (OUT) Frequency log message and must be freed by the caller
 *
 * Returns true if successful, false if not
 *
 * NOTE: Intel GPU supports tiles. One GPU may have two tiles, so all of tiles
 *       need to be set with the frequencies.
 */
static bool _oneapi_set_freqs(ze_device_handle_t device,
			      bool reset,
			      unsigned int gpu_freq_num,
			      unsigned int mem_freq_num,
			      char **freq_msg)
{
	uint32_t freq_handle_size = MAX_NUM_FREQUENCIES;
	zes_freq_handle_t freq_handles[MAX_NUM_FREQUENCIES];
	zes_freq_properties_t freq_prop;
	zes_freq_range_t freq_range;
	ze_result_t oneapi_rc;
	unsigned int freq = 0;

	/* Get all of frequency handles */
	oneapi_rc = zesDeviceEnumFrequencyDomains((zes_device_handle_t)device,
						  &freq_handle_size,
						  freq_handles);
	if (oneapi_rc != ZE_RESULT_SUCCESS) {
		error("Failed to get freq domains: 0x%x", oneapi_rc);
		return false;
	}

	/* Loop all of frequency handles and set range of frequency */
	for (int i = 0; i < freq_handle_size; i++) {
		/* Get frequency property */
		oneapi_rc = zesFrequencyGetProperties(freq_handles[i],
						      &freq_prop);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to get freq properties: 0x%x",
			      oneapi_rc);
			return false;
		}

		/*
		 * If the frequency is not GPU or memory fequency or it cannot
		 * be controlled, ignore it
		 */
		if (((freq_prop.type != ZES_FREQ_DOMAIN_GPU) &&
		     (freq_prop.type != ZES_FREQ_DOMAIN_MEMORY)) ||
		    !freq_prop.canControl) {
			debug2("Unsupported frequency. domain: %u, onSubdevice: %u, subdeviceId: %d, canControl:%s",
			       freq_prop.type, freq_prop.onSubdevice,
			       freq_prop.subdeviceId,
			       freq_prop.canControl ? "true" : "false");
			continue;
		}

		if (!reset) {
			/* Get nearest frequency */
			freq = (freq_prop.type == ZES_FREQ_DOMAIN_GPU) ?
				gpu_freq_num : mem_freq_num;
			if (!_oneapi_get_nearest_freq(freq_handles[i],
						      &freq)) {
				error("Failed to get nearest freq: %u", freq);
				return false;
			}
			freq_range.max = freq_range.min = freq;
		} else {
			/*
			* "-1" means the device will be set to the default
			* frequencies
			*/
			freq_range.max = freq_range.min = -1;
		}

		/* Print frequency before setting */
		debug2("Before %s frequency", reset ? "reset" : "set");
		_oneapi_print_freq_range(freq_handles[i], freq_prop.type);

		/* Set frequency range with a fixed value */
		oneapi_rc = zesFrequencySetRange(freq_handles[i], &freq_range);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to set frequency range: %f~%f, error:0x%x",
			      freq_range.min, freq_range.max, oneapi_rc);
			return false;
		}

		/* Print frequency after setting */
		debug2("After %s frequency", reset ? "reset" : "set");
		_oneapi_print_freq_range(freq_handles[i], freq_prop.type);

		if (freq_msg) {
			if (*freq_msg)
				xstrcat(*freq_msg, ",");
			if (freq_prop.type == ZES_FREQ_DOMAIN_GPU)
				xstrfmtcat(*freq_msg, "graphics_freq:%u",
					   freq);
			else
				xstrfmtcat(*freq_msg, "memory_freq:%u", freq);
		}
	}

	return true;
}

/*
 * Reset the frequencies for the GPU to the same default frequencies
 * that are used after system reboot or driver reload. This default
 * cannot be changed.
 *
 * device	(IN) The device handle
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_reset_freqs(ze_device_handle_t device)
{
	if (!_oneapi_set_freqs(device, true, 0, 0, NULL)) {
		error("Failed to reset frequencies");
		return false;
	}

	return true;
}

/*
 * Reset the frequencies of each GPU in the step to the hardware default
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate
 */
static void _reset_freq(bitstr_t *gpus)
{
	int gpu_len = bit_size(gpus);
	int count = 0, count_set = 0;
	bool freq_reset = false;
	ze_device_handle_t all_devices[MAX_GPU_NUM];
	uint32_t gpu_num = MAX_GPU_NUM;

	/* Get all of device handles */
	_oneapi_get_device_handles(all_devices, &gpu_num, false);
	if (gpu_num == 0) {
		error("Failed to get devices!");
		return;
	}

	/*
	 * If the gpu length is greater than the total GPU number,
	 * use the total GPU number
	 */
	if (gpu_len > gpu_num)
		gpu_len = gpu_num;

	/* Reset the frequency of each device allocated to the step */
	for (int i = 0; i < gpu_len; i++) {
		if (!bit_test(gpus, i))
			continue;
		count++;

		/* Reset frequency to the default value */
		freq_reset = _oneapi_reset_freqs(all_devices[i]);

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
 *
 * gpus		(IN) A bitmap specifying the GPUs on which to operate.
 * gpu_freq	(IN) The frequencies to set each of the GPUs to. If a NULL or
 *		empty memory or graphics frequency is specified, then
		GpuFreqDef will be consulted, which defaults to
		"high,memory=high" if not set.
 */
static void _set_freq(bitstr_t *gpus, char *gpu_freq)
{
	bool verbose_flag = false;
	int gpu_len = 0;
	int count = 0, count_set = 0;
	unsigned int gpu_freq_num = 0, mem_freq_num = 0;
	bool freq_set = false, freq_logged = false;
	char *tmp = NULL;
	bool task_cgroup = false;
	bool constrained_devices = false;
	bool cgroups_active = false;
	ze_device_handle_t all_devices[MAX_GPU_NUM];
	uint32_t gpu_num = MAX_GPU_NUM;

	/*
	 * Parse frequency information
	 */
	debug2("_parse_gpu_freq(%s)", gpu_freq);
	gpu_common_parse_gpu_freq(gpu_freq, &gpu_freq_num, &mem_freq_num,
				  &verbose_flag);
	if (verbose_flag)
		debug2("verbose_flag ON");

	tmp = gpu_common_freq_value_to_string(mem_freq_num);
	debug2("Requested GPU memory frequency: %s", tmp);
	xfree(tmp);
	tmp = gpu_common_freq_value_to_string(gpu_freq_num);
	debug2("Requested GPU graphics frequency: %s", tmp);
	xfree(tmp);

	if (!mem_freq_num || !gpu_freq_num) {
		debug2("%s: No frequencies to set", __func__);
		return;
	}

	/* Check if GPUs are constrained by cgroups */
	cgroup_conf_init();
	if (slurm_cgroup_conf.constrain_devices)
		constrained_devices = true;

	/* Check if task/cgroup plugin is loaded */
	if (xstrstr(slurm_conf.task_plugin, "cgroup"))
		task_cgroup = true;

	/* If both of these are true, then GPUs will be constrained */
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

	/* Get all of device handles */
	_oneapi_get_device_handles(all_devices, &gpu_num, false);
	if (gpu_num == 0) {
		error("Failed to get devices!");
		return;
	}

	if (gpu_len > gpu_num)
		gpu_len = gpu_num;

	/* Set the frequency of each device allocated to the step */
	for (int i = 0; i < gpu_len; i++) {
		/* Only check the global GPU bitstring if not using cgroups */
		if (!cgroups_active && !bit_test(gpus, i)) {
			debug2("Passing over oneAPI device %u", i);
			continue;
		}
		count++;

		freq_set = _oneapi_set_freqs(all_devices[i], false,
					     gpu_freq_num, mem_freq_num,
					     &tmp);
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
 * Set the cpu affinity mask
 *
 * cpu		(IN) The index of the CPU
 * cpu_set:	[IN/out] An array reference in which to return a bitmask of
 *		CPUs. 64 CPUs per unsigned long on 64-bit machines, 32 on
 * 		32-bit machines. For example, on 32-bit machines,
 * 		if processors 0, 1, 32, and 33 are ideal for the device
 * 		and cpuSetSize == 2, result[0] = 0x3, result[1] = 0x3.
 * size		[IN] The size of the cpu set buffer
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_set_cpu_affinity_mask(int cpu,
					  unsigned long *cpu_set,
					  unsigned int size)
{
	unsigned int count;
	unsigned int model;

	if (cpu < 0)
		return false;

	count = cpu / ULONG_BITS;
	if ((count + 1) > size) {
		error("cpu set size is not enough: %u", size);
		return false;
	}

	model = cpu % ULONG_BITS;
	cpu_set[count] = cpu_set[count] | (((unsigned long)0x01) << model);
	return true;
}

/*
 * Read the cpu affinity mask
 *
 * file		(IN) The full path of cpu list file
 * 		For example, /sys/class/drm/card1/device/local_cpulist
 * cpu_set:	[IN/out] An array reference in which to return a bitmask of
 *		CPUs. 64 CPUs per unsigned long on 64-bit machines, 32 on
 * 		32-bit machines. For example, on 32-bit machines,
 * 		if processors 0, 1, 32, and 33 are ideal for the device
 * 		and cpuSetSize == 2, result[0] = 0x3, result[1] = 0x3.
 * size		[IN] The size of the cpu set buffer
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_read_cpu_affinity_list(const char *file,
					   unsigned long *cpu_set,
					   unsigned int size)
{
	char line[CPU_LINE_SIZE] = {'\0'};
	char buf[CPU_LINE_SIZE] = {'\0'};
	char *save_ptr = line, *tok = NULL;
	int min_cpu = -1, max_cpu = -1;
	FILE *fp = NULL;
	int pos = -1;

	debug2("Read file: %s", file);

	fp = fopen(file, "r");
	if (fp == NULL) {
		error("Failed to read the file: %s", file);
		return false;
	}

	/* Example format: "0-27,56-83" */
	if (fgets(line, sizeof(line), fp) != NULL) {
		debug2("line is: %s", line);
		while ((tok = strtok_r(save_ptr, ",", &save_ptr)) != NULL)  {
			/* Split CPU range from string like "0-27" */
			debug2("tok is :%s", tok);
			pos = strcspn(tok, "-");
			if (pos > 0 && pos < strlen(tok)) {
				strlcpy(buf, tok, pos);
				min_cpu = atoi(buf);
				strlcpy(buf, tok + pos + 1, sizeof(buf));
				max_cpu = atoi(buf);
			} else if (pos > 0 && pos == strlen(tok)) {
				max_cpu = min_cpu = atoi(tok);
			} else {
				continue;
			}

			debug2("cpu range is: %d~%d", min_cpu, max_cpu);

			/* Set CPU bit mask */
			for (int i = min_cpu; i <= max_cpu; i++)
				_oneapi_set_cpu_affinity_mask(i, cpu_set,
							      size);
		}
	}

	fclose(fp);
	return true;
}


/*
 * Get device card name under folder "/sys/class/drm"
 * There are no APIs to get minor number of Intel GPU at the moment, so we
 * have to read BDF information from PCI and map it according to the
 * device file symlinks under the folder "/sys/class/drm".
 *
 * domain	(IN) From PCI BDF
 * bus		(IN) From PCI BDF
 * device	(IN) From PCI BDF
 * function	(IN) From PCI BDF
 * name		(IN/OUT) The device name
 * len		(IN) The length of the device name buffer
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_get_device_name(uint32_t domain, uint32_t bus,
				    uint32_t device, uint32_t function,
				    char *name, uint32_t len)
{
	static const char *card_reg_string = "card[0-9]+$";
	const char *search_path = "/sys/class/drm";
	char device_pattern[PATH_MAX] = {'\0'};
	char path[PATH_MAX] = {'\0'};
	char real_path[PATH_MAX] = {'\0'};
	DIR *dir = NULL;
	struct dirent *dp = NULL;
	regex_t search_reg;
	regex_t card_reg;
	regmatch_t reg_match;
	char *matched = NULL;
	bool ret = false;
	int rc;

	/*
	 * Build search pattern to search strings like
	 * "../../devices/pci0000:89/0000:89:02.0/0000:8a:00.0
	 * /0000:8b:01.0/0000:8c:00.0/drm/card0"
	 */
	snprintf(device_pattern, sizeof(device_pattern),
		 "/%04x:%02x:%02x.%0x/drm/card[0-9]+$", domain, bus,
		 device, function);
	if ((rc = regcomp(&search_reg, device_pattern, REG_EXTENDED))) {
		dump_regex_error(rc, &search_reg,
				 "Device file regex \"%s\" compilation failed",
				 device_pattern);
		return false;
	}

	if ((rc = regcomp(&card_reg, card_reg_string, REG_EXTENDED))) {
		dump_regex_error(rc, &card_reg,
				 "Card regex \"%s\" compilation failed",
				 card_reg_string);
		regfree(&search_reg);
		return false;
	}

	/* Open the device folder */
	if ((dir = opendir(search_path)) == NULL) {
		error("Failed to open the folder: %s", search_path);
		regfree(&card_reg);
		regfree(&search_reg);
		return false;
	}

	/* Loop all of symlink files */
	while (((dp = readdir(dir))) != NULL) {
		/* If the file is folder, ignore it */
		if (!strncmp(dp->d_name, ".", 1) ||
		    !strncmp(dp->d_name, "..", 2))
			continue;

		/* Read the symlinks */
		snprintf(path, sizeof(path), "%s/%s", search_path, dp->d_name);
		memset(real_path, 0, PATH_MAX);
		if (readlink(path, real_path, PATH_MAX) < 0)
			continue;
		debug2("Read symblink file: %s with real path: %s",
		       path, real_path);

		/* Check file path match */
		if (regexec(&search_reg, real_path, 1, &reg_match, 0) ==
		    REG_NOMATCH)
			continue;

		/* Check card name match */
		if (regexec(&card_reg, real_path, 1, &reg_match, 0) ==
		    REG_NOMATCH)
			continue;

		/* BDF string matches, so it should be the devie file name */
		matched = xstrndup(real_path + reg_match.rm_so, (size_t)
				   (reg_match.rm_eo - reg_match.rm_so));
		snprintf(name, len, "%s", matched);
		xfree(matched);

		debug2("Device name is: %s", name);

		ret = true;
		break;
	}

	regfree(&card_reg);
	regfree(&search_reg);
	closedir(dir);

	return ret;
}

/*
 * Get device affinity
 *
 * device_name	(IN) The device name under folder "/sys/class/drm"
 * cpu_set:	[IN/out] An array reference in which to return a bitmask of
 *		CPUs. 64 CPUs per unsigned long on 64-bit machines, 32 on
 * 		32-bit machines. For example, on 32-bit machines,
 * 		if processors 0, 1, 32, and 33 are ideal for the device
 * 		and cpuSetSize == 2, result[0] = 0x3, result[1] = 0x3.
 * size		[IN] The size of the cpu set buffer
 *
 * Returns true if successful, false if not
 */
static bool _oneapi_get_device_affinity(const char *device_name,
					unsigned long *cpu_set,
					unsigned int size)
{
	const char *search_path = "/sys/class/drm";
	const char *cpu_list_sub_path = "device/local_cpulist";
	char path[PATH_MAX] = {'\0'};

	snprintf(path, sizeof(path), "%s/%s/%s", search_path, device_name,
		 cpu_list_sub_path);
	return _oneapi_read_cpu_affinity_list(path, cpu_set, size);
}

extern int init(void)
{
	debug("loading");

	/* Init oneAPI */
	setenv("ZES_ENABLE_SYSMAN", "1", 1);
	if (zeInit(0) != ZE_RESULT_SUCCESS)
		fatal("zeInit failed");

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	debug("unloading");

	return SLURM_SUCCESS;
}

extern int gpu_p_reconfig(void)
{
	return SLURM_SUCCESS;
}

/*
 * Creates and returns a gres conf list of detected Intel gpus on the node.
 * If an error occurs, return NULL
 * Caller is responsible for freeing the list.
 *
 * If the Intel oneAPI exists, then query GPU info,
 * so the user doesn't need to specify manually in gres.conf.
 *
 * node_config (IN/OUT) pointer of node_config_load_t passed down
 */
static List _get_system_gpu_list_oneapi(node_config_load_t *node_config)
{
	char device_file[PATH_MAX];
	char card_name[CARD_NAME_LEN];
	ze_device_handle_t all_devices[MAX_GPU_NUM];
	ze_device_properties_t device_props;
	zes_device_handle_t zes_handle;
	zes_pci_properties_t pci;
	ze_result_t oneapi_rc;
	uint32_t gpu_num = MAX_GPU_NUM;
	unsigned long cpu_set[CPU_SET_SIZE] = {0};
	char *cpu_aff_mac_range = NULL;
	char *cpu_aff_abs_range = NULL;
	int i;

	List gres_list_system = list_create(destroy_gres_slurmd_conf);

	/* Get all of device handles */
	_oneapi_get_device_handles(all_devices, &gpu_num, true);
	if (gpu_num == 0) {
		error("Failed to get devices!");
		return gres_list_system ;
	}

	/* Loop all of GPU device handles */
	for (i = 0; i < gpu_num; i++) {
		gres_slurmd_conf_t gres_slurmd_conf = {
			.config_flags = GRES_CONF_ENV_ONEAPI,
			.count = 1,
			.cpu_cnt = node_config->cpu_cnt,
			.name = "gpu",
		};

		/* Get PCI properties */
		zes_handle = (zes_device_handle_t)all_devices[i];
		oneapi_rc = zesDevicePciGetProperties(zes_handle, &pci);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			error("Failed to get pci info: 0x%x", oneapi_rc);
			continue;
		}

		/* Get device card name */
		if (!_oneapi_get_device_name(pci.address.domain,
					     pci.address.bus,
					     pci.address.device,
					     pci.address.function,
					     card_name, CARD_NAME_LEN)) {
			error("Failed to get device card name for GPU: %u", i);
			continue;
		}

		/* Get device file */
		snprintf(device_file, PATH_MAX, "/dev/dri/%s", card_name);

		/* Get device affinity */
		memset(cpu_set, 0, sizeof(unsigned long) * CPU_SET_SIZE);
		if (!_oneapi_get_device_affinity(card_name, cpu_set,
						 CPU_SET_SIZE)) {
			error("Failed to get device affinity for GPU: %u", i);
			continue;
		}

		/* Convert from cpu bitmask to slurm bitstr_t (machine fmt) */
		gres_slurmd_conf.cpus_bitmap = bit_alloc(MAX_CPUS);
		_set_cpu_set_bitstr(gres_slurmd_conf.cpus_bitmap,
				    cpu_set, CPU_SET_SIZE);

		/* Convert from bitstr_t to cpu range str */
		cpu_aff_mac_range = bit_fmt_full(gres_slurmd_conf.cpus_bitmap);

		/*
		 * Convert cpu range str from machine to abstract (slurm) format
		 */
		if (node_config->xcpuinfo_mac_to_abs(cpu_aff_mac_range,
						     &cpu_aff_abs_range)) {
			error("Conversion from machine to abstract failed");
			FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
			xfree(cpu_aff_mac_range);
			continue;
		}

		/* Use links to record PCI bus ID order */
		gres_slurmd_conf.links = gres_links_create_empty(i, gpu_num);

		/* Get device properties */
		oneapi_rc = zeDeviceGetProperties(all_devices[i],
						  &device_props);
		if (oneapi_rc != ZE_RESULT_SUCCESS) {
			info("Failed to get device property: 0x%x", oneapi_rc);
			FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
			xfree(cpu_aff_mac_range);
			xfree(cpu_aff_abs_range);
			xfree(gres_slurmd_conf.links);
			continue;
		}

		debug2("GPU index %u:", i);
		debug2("    Name: %s", device_props.name);
		debug2("    DeviceId: %u", device_props.deviceId);
		debug2("    PCI Domain/Bus/Device/Function: %u:%u:%u:%u",
			pci.address.domain, pci.address.bus,
			pci.address.device, pci.address.function);
		debug2("    Links: %s", gres_slurmd_conf.links);
		debug2("    Device File: %s", device_file);
		debug2("    CPU Affinity Range - Machine: %s",
			cpu_aff_mac_range);
		debug2("    Core Affinity Range - Abstract: %s",
			cpu_aff_abs_range);

		/* Print out possible frequencies for this device */
		_oneapi_print_freqs(all_devices[i], LOG_LEVEL_DEBUG2);

		gres_slurmd_conf.type_name = device_props.name;
		gres_slurmd_conf.file = device_file;

		/* Add the GPU to list */
		add_gres_to_list(gres_list_system, &gres_slurmd_conf);

		FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
		xfree(cpu_aff_mac_range);
		xfree(gres_slurmd_conf.cpus);
		xfree(gres_slurmd_conf.links);
	}

	return gres_list_system;
}

extern List gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	xassert(node_config);

	List gres_list_system = _get_system_gpu_list_oneapi(node_config);
	if (!gres_list_system)
		error("System GPU detection failed");

	return gres_list_system;
}

extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	debug2("enter gpu_p_step_hardware_init()");

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

	/*
	 * Save a copy of the GPUs affected, so we can reset things afterwards
	 */
	FREE_NULL_BITMAP(saved_gpus);
	saved_gpus = bit_copy(usable_gpus);

	/* Set the frequency of each GPU index specified in the bitstr */
	_set_freq(usable_gpus, freq);
	xfree(freq);

	debug2("exit gpu_p_step_hardware_init() normally");
}

extern void gpu_p_step_hardware_fini(void)
{
	debug2("enter gpu_p_step_hardware_fini()");

	if (!saved_gpus)
		return;

	/* Reset the frequencies back to the hardware default */
	_reset_freq(saved_gpus);
	FREE_NULL_BITMAP(saved_gpus);

	debug2("exit gpu_p_step_hardware_fini() normally");
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

extern void gpu_p_get_device_count(unsigned int *device_count)
{
	ze_device_handle_t all_devices[MAX_GPU_NUM];
	uint32_t gpu_num = MAX_GPU_NUM;

	_oneapi_get_device_handles(all_devices, &gpu_num, false);
	if (gpu_num == 0) {
		error("Failed to get device count!");
		*device_count = 0;
	} else {
		*device_count = gpu_num;
	}
}

extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	return SLURM_SUCCESS;
}

extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
	return SLURM_SUCCESS;
}
