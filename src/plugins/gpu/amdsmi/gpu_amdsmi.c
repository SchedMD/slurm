/*****************************************************************************\
 *  gpu_amdsmi.c - Support amdsmi interface to an AMD GPU.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <dlfcn.h>
#include <amd_smi/amdsmi.h>

#include "../common/gpu_common.h"

#ifdef HAVE_NUMA
#  include <numa.h>
#endif

/*
 * #defines needed to test rsmi.
 */

static bitstr_t	*saved_gpus;

/*
 * Buffer size large enough for AMDSMI string
 */
#define AMDSMI_STRING_BUFFER_SIZE			256
/* ROCM release version >= 6.0.0 required for gathering usage */
#define AMDSMI_REQ_VERSION_USAGE 6

#ifndef SLURM_ARRAY_SIZE
# define SLURM_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
/*
 * PCI information about a GPU device.
 */
typedef struct amdsmiPciInfo_st {
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
} amdsmiPciInfo_t;

/* Required Slurm plugin symbols: */
const char plugin_name[] = "GPU AMDSMI plugin";
const char plugin_type[] = "gpu/amdsmi";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static int gpumem_pos = -1;
static int gpuutil_pos = -1;

static bool get_usage = true;

/* Processor handles cache for AMD-SMI API */
static amdsmi_processor_handle processor_handles[256] = {0};
static uint32_t processor_handle_count = 0;

static void _amdsmi_get_version(char *version, unsigned int len);
static void _amdsmi_get_driver(char *driver, unsigned int len);



/*
 * Initialize the AMD‑SMI library and cache processor handles.
 * Safe to call multiple times from the same process.
 */
static void _amdsmi_init(void)
{
    static pid_t init_pid = 0;
    static bool initialized = false;

    pid_t my_pid = getpid();

    if (conf && conf->pid)
        my_pid = conf->pid;
    amdsmi_status_t amdsmi_rc;
    const char *status_string = NULL;
    char version[AMDSMI_STRING_BUFFER_SIZE];
    char driver[AMDSMI_STRING_BUFFER_SIZE];

    uint32_t socket_count = 0;
    amdsmi_socket_handle socket_handles[32] = {0};

    /* If we've already initialized in this process, just return. */
    if (initialized && init_pid == my_pid)
        return;

    init_pid = my_pid;
    processor_handle_count = 0;

    DEF_TIMERS;
    START_TIMER;
    amdsmi_rc = amdsmi_init(AMDSMI_INIT_AMD_GPUS);
    END_TIMER;

    if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
        amdsmi_status_code_to_string(amdsmi_rc, &status_string);
        error("AMDSMI: amdsmi_init(AMDSMI_INIT_AMD_GPUS) failed: %s",
              status_string ? status_string : "unknown error");
        return;
    }

    debug3("AMDSMI: amdsmi_init() took %s", TIMER_STR());
    debug2("AMDSMI: Successfully initialized");

    /*
     * Discover sockets first (two‑stage pattern):
     *   1) get socket_count
     *   2) fill socket_handles[]
     */
    socket_count = 0;
    amdsmi_rc = amdsmi_get_socket_handles(&socket_count, NULL);
    if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
        amdsmi_status_code_to_string(amdsmi_rc, &status_string);
        error("AMDSMI: Failed to query socket count: %s",
              status_string ? status_string : "unknown error");
        goto fail_shutdown;
    }

    if (socket_count == 0) {
        error("AMDSMI: No sockets reported by amdsmi_get_socket_handles()");
        goto fail_shutdown;
    }

    if (socket_count > (uint32_t)SLURM_ARRAY_SIZE(socket_handles)) {
        error("AMDSMI: Detected %u sockets, but only have space for %zu",
              socket_count, SLURM_ARRAY_SIZE(socket_handles));
        socket_count = SLURM_ARRAY_SIZE(socket_handles);
    }

    amdsmi_rc = amdsmi_get_socket_handles(&socket_count, socket_handles);
    if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
        amdsmi_status_code_to_string(amdsmi_rc, &status_string);
        error("AMDSMI: Failed to get socket handles: %s",
              status_string ? status_string : "unknown error");
        goto fail_shutdown;
    }

    debug2("AMDSMI: Detected %u socket(s)", socket_count);

    /*
     * For each socket, query processor handles (devices) on that socket.
     * We only keep up to processor_handles[] capacity.
     */
    for (uint32_t s = 0; s < socket_count; s++) {
        uint32_t dev_count = 0;

        /* First call to get dev_count for this socket. */
        amdsmi_rc = amdsmi_get_processor_handles(socket_handles[s],
                             &dev_count,
                             NULL);
        if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
            amdsmi_status_code_to_string(amdsmi_rc, &status_string);
            error("AMDSMI: Failed to query device count on socket %u: %s",
                  s, status_string ? status_string : "unknown error");
            continue;
        }

        if (dev_count == 0)
            continue;

        if (processor_handle_count + dev_count >
            SLURM_ARRAY_SIZE(processor_handles)) {
            uint32_t allowed = SLURM_ARRAY_SIZE(processor_handles) -
                       processor_handle_count;
            debug("AMDSMI: Truncating devices on socket %u from %u to %u "
                  "to fit processor_handles[]",
                  s, dev_count, allowed);
            dev_count = allowed;
        }

        if (dev_count == 0)
            continue;

        amdsmi_rc = amdsmi_get_processor_handles(
            socket_handles[s],
            &dev_count,
            &processor_handles[processor_handle_count]);
        if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
            amdsmi_status_code_to_string(amdsmi_rc, &status_string);
            error("AMDSMI: Failed to get processor handles on socket %u: %s",
                  s, status_string ? status_string : "unknown error");
            continue;
        }

        processor_handle_count += dev_count;
    }

    if (processor_handle_count == 0) {
        error("AMDSMI: No GPU processors discovered on any socket");
        goto fail_shutdown;
    }

    debug("AMDSMI: Cached %u GPU processor handle(s)", processor_handle_count);

    /* Log driver + library version information (best‑effort, non‑fatal). */
    _amdsmi_get_driver(driver, AMDSMI_STRING_BUFFER_SIZE);
    _amdsmi_get_version(version, AMDSMI_STRING_BUFFER_SIZE);
    if (driver[0])
        debug("AMDSMI: AMD GPU driver version: %s", driver);
    if (version[0])
        debug("AMDSMI: AMD‑SMI library version: %s", version);

    initialized = true;
    return;

fail_shutdown:
    /*
     * We failed mid‑init; shut down cleanly so a later attempt can retry.
     * We deliberately do not set initialized=true in this path.
     */
    amdsmi_shut_down();
    processor_handle_count = 0;
}


extern int init(void)
{
	if (running_in_slurmstepd()) {
		gpu_get_tres_pos(&gpumem_pos, &gpuutil_pos);
	}

	debug("%s: %s loaded", __func__, plugin_name);

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);

	amdsmi_shut_down();
}

static void _amdsmi_get_driver(char *driver, unsigned int len)
{
    if (!driver || len == 0)
        return;

    driver[0] = '\0';

    /* Ensure AMD-SMI is initialized and handles are available */
    _amdsmi_init();

    if (processor_handle_count == 0) {
        debug("AMDSMI: No GPU processor handles available for driver query");
        return;
    }

    amdsmi_driver_info_t dinfo;
    memset(&dinfo, 0, sizeof(dinfo));

    const char *status_string = NULL;
    amdsmi_status_t rc =
        amdsmi_get_gpu_driver_info(processor_handles[0], &dinfo);

    if (rc != AMDSMI_STATUS_SUCCESS) {
        amdsmi_status_code_to_string(rc, &status_string);
        debug("AMDSMI: Failed to get driver info: %s",
              status_string ? status_string : "unknown");
        return;
    }

    snprintf(driver, len, "%s", dinfo.driver_version);
}

/*
 * Get all possible memory frequencies for the device
 *
 * dv_ind         (IN)      The device index
 * mem_freqs_size (IN/OUT)  IN:  size of mem_freqs[] buffer (capacity)
 *                           OUT: number of frequencies actually written
 * mem_freqs      (OUT)     The possible memory frequencies in MHz.
 *
 * Return true if successful, false if not.
 */
static bool _amdsmi_get_mem_freqs(uint32_t dv_ind, uint32_t *mem_freqs_size,
                                  uint32_t *mem_freqs)
{
    const char *status_string = NULL;
    amdsmi_status_t amdsmi_rc;
    amdsmi_frequencies_t amdsmi_freqs;
    uint32_t capacity, num_supported, to_copy;

    if (!mem_freqs_size || !mem_freqs) {
        error("AMDSMI: _amdsmi_get_mem_freqs called with NULL pointer");
        return false;
    }

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return false;
    }

    capacity = *mem_freqs_size;

    DEF_TIMERS;
    START_TIMER;
    amdsmi_rc = amdsmi_get_clk_freq(processor_handles[dv_ind],
                                    AMDSMI_CLK_TYPE_MEM,
                                    &amdsmi_freqs);
    END_TIMER;
    debug3("amdsmi_get_clk_freq(MEM) took %s", TIMER_STR());

    if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(amdsmi_rc, &status_string);
        error("AMDSMI: Failed to get memory frequencies: %s",
              status_string ? status_string : "unknown");
        return false;
    }

    num_supported = amdsmi_freqs.num_supported;

    /* Clamp to caller-provided capacity to avoid overflow */
    if (num_supported > capacity) {
        debug("AMDSMI: Truncating mem freq list: GPU supports %u freqs, "
              "buffer capacity is %u", num_supported, capacity);
        to_copy = capacity;
    } else {
        to_copy = num_supported;
    }

    for (uint32_t i = 0; i < to_copy; i++) {
        /* NOTE: amdsmi_freqs.frequency[] is assumed to be in Hz.
         * Convert to MHz for Slurm's internal representation.
         */
        mem_freqs[i] = amdsmi_freqs.frequency[i] / 1000000U;
    }

    /* Report how many entries we actually wrote */
    *mem_freqs_size = to_copy;

    return true;
}
/*
 * Get all possible graphics frequencies for the device
 *
 * dv_ind         (IN)      The device index
 * gfx_freqs_size (IN/OUT)  IN:  size of gfx_freqs[] buffer (capacity)
 *                           OUT: number of frequencies actually written
 * gfx_freqs      (OUT)     The possible graphics frequencies in MHz.
 *
 * Return true if successful, false if not.
 */
static bool _amdsmi_get_gfx_freqs(uint32_t dv_ind, uint32_t *gfx_freqs_size,
                                  uint32_t *gfx_freqs)
{
    const char *status_string = NULL;
    amdsmi_status_t amdsmi_rc;
    amdsmi_frequencies_t amdsmi_freqs;
    uint32_t capacity, num_supported, to_copy;

    if (!gfx_freqs_size || !gfx_freqs) {
        error("AMDSMI: _amdsmi_get_gfx_freqs called with NULL pointer");
        return false;
    }

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return false;
    }

    capacity = *gfx_freqs_size;

    DEF_TIMERS;
    START_TIMER;
    amdsmi_rc = amdsmi_get_clk_freq(processor_handles[dv_ind],
                                    AMDSMI_CLK_TYPE_SYS,
                                    &amdsmi_freqs);
    END_TIMER;
    debug3("amdsmi_get_clk_freq(GFX) took %s", TIMER_STR());

    if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(amdsmi_rc, &status_string);
        error("AMDSMI: Failed to get graphics frequencies: %s",
              status_string ? status_string : "unknown");
        return false;
    }

    num_supported = amdsmi_freqs.num_supported;

    /* Clamp to buffer capacity */
    if (num_supported > capacity) {
        debug("AMDSMI: Truncating gfx freq list: GPU supports %u freqs, "
              "buffer capacity is %u", num_supported, capacity);
        to_copy = capacity;
    } else {
        to_copy = num_supported;
    }

    for (uint32_t i = 0; i < to_copy; i++) {
        /* AMD-SMI's frequency[] is assumed to be in Hz -> convert to MHz */
        gfx_freqs[i] = amdsmi_freqs.frequency[i] / 1000000U;
    }

    /* Report back the actual number of entries written */
    *gfx_freqs_size = to_copy;

    return true;
}

/*
 * Print out all possible memory and graphics frequencies for the given device.
 * If there are more than FREQS_CONCISE frequencies, prints a summary instead
 *
 * dv_ind (IN) The device index
 * l      (IN) The log level at which to print
 */
static void _amdsmi_print_freqs(uint32_t dv_ind, log_level_t l)
{
    uint32_t mem_freqs[AMDSMI_MAX_NUM_FREQUENCIES] = {0};
    uint32_t gfx_freqs[AMDSMI_MAX_NUM_FREQUENCIES] = {0};

    /* --- MEMORY FREQUENCIES --- */
    uint32_t mem_size = AMDSMI_MAX_NUM_FREQUENCIES;

    if (!_amdsmi_get_mem_freqs(dv_ind, &mem_size, mem_freqs))
        return;

    qsort(mem_freqs, mem_size, sizeof(uint32_t),
          slurm_sort_uint32_list_desc);

    if (mem_size > 1 && mem_freqs[0] <= mem_freqs[mem_size - 1]) {
        error("%s: memory frequencies are not stored in descending order!",
              __func__);
        return;
    }

    gpu_common_print_freqs(mem_freqs, mem_size, l, "GPU Memory", 0);

    /* --- GRAPHICS FREQUENCIES --- */
    uint32_t gfx_size = AMDSMI_MAX_NUM_FREQUENCIES;

    if (!_amdsmi_get_gfx_freqs(dv_ind, &gfx_size, gfx_freqs))
        return;

    qsort(gfx_freqs, gfx_size, sizeof(uint32_t),
          slurm_sort_uint32_list_desc);

    if (gfx_size > 1 && gfx_freqs[0] <= gfx_freqs[gfx_size - 1]) {
        error("%s: graphics frequencies are not stored in descending order!",
              __func__);
        return;
    }

    gpu_common_print_freqs(gfx_freqs, gfx_size, l, "GPU Graphics", 0);
}

/*
 * Get the nearest valid memory and graphics frequencies.
 * Return bit masks indicating which indices correspond
 * to the selected frequencies.
 *
 * dv_ind      (IN)      device index
 * mem_freq    (IN/OUT)  requested/nearest valid memory frequency
 * mem_bitmask (OUT)     bit mask for nearest memory frequency
 * gfx_freq    (IN/OUT)  requested/nearest valid graphics frequency
 * gfx_bitmask (OUT)     bit mask for nearest graphics frequency
 */
static void _amdsmi_get_nearest_freqs(uint32_t dv_ind,
                                      uint32_t *mem_freq,
                                      uint64_t *mem_bitmask,
                                      uint32_t *gfx_freq,
                                      uint64_t *gfx_bitmask)
{
    if (!mem_freq || !mem_bitmask || !gfx_freq || !gfx_bitmask)
        return;

    /* --------------------------------------------------------------- *
     * MEMORY FREQUENCIES
     * --------------------------------------------------------------- */
    uint32_t mem_freqs[AMDSMI_MAX_NUM_FREQUENCIES] = {0};
    uint32_t mem_freqs_sorted[AMDSMI_MAX_NUM_FREQUENCIES] = {0};
    uint32_t mem_size = AMDSMI_MAX_NUM_FREQUENCIES;

    if (!_amdsmi_get_mem_freqs(dv_ind, &mem_size, mem_freqs))
        return;

    memcpy(mem_freqs_sorted, mem_freqs, mem_size * sizeof(uint32_t));

    qsort(mem_freqs_sorted, mem_size, sizeof(uint32_t),
          slurm_sort_uint32_list_desc);

    if (mem_size > 1 &&
        mem_freqs_sorted[0] <= mem_freqs_sorted[mem_size - 1]) {
        error("%s: memory frequencies not sorted descending!", __func__);
        return;
    }

    /* Determine nearest valid frequency */
    gpu_common_get_nearest_freq(mem_freq, mem_size, mem_freqs_sorted);

    /* Find bitmask index using ORIGINAL (unsorted) array */
    *mem_bitmask = 0;

    for (uint64_t i = 0; i < mem_size; i++) {
        if (*mem_freq == mem_freqs[i]) {
            *mem_bitmask = (1ULL << i);
            break;
        }
    }

    /* --------------------------------------------------------------- *
     * GRAPHICS FREQUENCIES
     * --------------------------------------------------------------- */
    uint32_t gfx_freqs[AMDSMI_MAX_NUM_FREQUENCIES] = {0};
    uint32_t gfx_freqs_sorted[AMDSMI_MAX_NUM_FREQUENCIES] = {0};
    uint32_t gfx_size = AMDSMI_MAX_NUM_FREQUENCIES;

    if (!_amdsmi_get_gfx_freqs(dv_ind, &gfx_size, gfx_freqs))
        return;

    memcpy(gfx_freqs_sorted, gfx_freqs, gfx_size * sizeof(uint32_t));

    qsort(gfx_freqs_sorted, gfx_size, sizeof(uint32_t),
          slurm_sort_uint32_list_desc);

    if (gfx_size > 1 &&
        gfx_freqs_sorted[0] <= gfx_freqs_sorted[gfx_size - 1]) {
        error("%s: graphics frequencies not sorted descending!", __func__);
        return;
    }

    /* Determine nearest valid frequency */
    gpu_common_get_nearest_freq(gfx_freq, gfx_size, gfx_freqs_sorted);

    /* Find bitmask index using ORIGINAL (unsorted) array */
    *gfx_bitmask = 0;

    for (uint64_t i = 0; i < gfx_size; i++) {
        if (*gfx_freq == gfx_freqs[i]) {
            *gfx_bitmask = (1ULL << i);
            break;
        }
    }
}
/*
 * Set the memory and graphics clock frequencies for the GPU.
 *
 * dv_ind      (IN) The device index
 * mem_bitmask (IN) bit mask for the memory frequency
 * gfx_bitmask (IN) bit mask for the graphics frequency
 *
 * Returns true if successful, false on error
 */
static bool _amdsmi_set_freqs(uint32_t dv_ind,
                              uint64_t mem_bitmask,
                              uint64_t gfx_bitmask)
{
    const char *status_string = NULL;
    amdsmi_status_t rc;

    /* Validate device index */
    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return false;
    }

    amdsmi_processor_handle h = processor_handles[dv_ind];

    /* ----------------------------------------------------------- */
    /* MEMORY CLOCK MASK                                           */
    /* ----------------------------------------------------------- */
    if (mem_bitmask != 0) {
        DEF_TIMERS;
        START_TIMER;

        /* freq_bitmask API (correct for bitmasks) */
        rc = amdsmi_set_clk_freq(h, AMDSMI_CLK_TYPE_MEM, mem_bitmask);

        END_TIMER;
        debug3("amdsmi_set_clk_freq(MEM, 0x%llx) took %s",
               (unsigned long long)mem_bitmask, TIMER_STR());

        if (rc != AMDSMI_STATUS_SUCCESS) {
            (void) amdsmi_status_code_to_string(rc, &status_string);
            error("AMDSMI: Failed to set memory frequency on GPU %u: %s",
                  dv_ind, status_string ? status_string : "unknown");
            return false;
        }
    } else {
        debug2("AMDSMI: mem_bitmask=0 -> skipping memory frequency update on GPU %u",
               dv_ind);
    }

    /* ----------------------------------------------------------- */
    /* GRAPHICS CLOCK MASK                                         */
    /* ----------------------------------------------------------- */
    if (gfx_bitmask != 0) {
        DEF_TIMERS;
        START_TIMER;

        /* freq_bitmask API (correct for bitmasks) */
        rc = amdsmi_set_clk_freq(h, AMDSMI_CLK_TYPE_SYS, gfx_bitmask);

        END_TIMER;
        debug3("amdsmi_set_clk_freq(GFX, 0x%llx) took %s",
               (unsigned long long)gfx_bitmask, TIMER_STR());

        if (rc != AMDSMI_STATUS_SUCCESS) {
            (void) amdsmi_status_code_to_string(rc, &status_string);
            error("AMDSMI: Failed to set graphics frequency on GPU %u: %s",
                  dv_ind, status_string ? status_string : "unknown");
            return false;
        }
    } else {
        debug2("AMDSMI: gfx_bitmask=0 -> skipping graphics frequency update on GPU %u",
               dv_ind);
    }

    return true;
}

/*
 * Reset the memory and graphics clock frequencies for the GPU to the same
 * default frequencies that are used after system reboot or driver reload. This
 * default cannot be changed.
 *
 * dv_ind  (IN) The device index
 *
 * Returns true if successful, false if not.
 */
static bool _amdsmi_reset_freqs(uint32_t dv_ind)
{
    const char *status_string = NULL;
    amdsmi_status_t rc;

    /* Validate device index */
    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return false;
    }

    amdsmi_processor_handle h = processor_handles[dv_ind];

    DEF_TIMERS;
    START_TIMER;

    /* Setting perf level to AUTO should restore default clock behaviour */
    rc = amdsmi_set_gpu_perf_level(h, AMDSMI_DEV_PERF_LEVEL_AUTO);

    END_TIMER;
    debug3("amdsmi_set_gpu_perf_level(GPU %u, AUTO) took %s",
           dv_ind, TIMER_STR());

    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to reset frequencies on GPU %u: %s",
              dv_ind, status_string ? status_string : "unknown");
        return false;
    }

    return true;
}

/*
 * Get the memory or graphics clock frequency that the GPU is currently running
 * at
 *
 * dv_ind	(IN) The device index
 * type		(IN) The clock type to query. Either AMDSMI_CLK_TYPE_SYS or
 *		AMDSMI_CLK_TYPE_MEM.
 *
 * Returns the clock frequency in MHz if successful, or 0 if not
 */
static uint32_t _amdsmi_get_freq(uint32_t dv_ind, amdsmi_clk_type_t type)
{
	const char *status_string;
	amdsmi_status_t amdsmi_rc;
	amdsmi_frequencies_t amdsmi_freqs;
	char *type_str = "unknown";

	if (dv_ind >= processor_handle_count) {
		error("AMDSMI: Invalid device index %u (max %u)", dv_ind, processor_handle_count);
		return 0;
	}

	DEF_TIMERS;

	switch (type) {
	case AMDSMI_CLK_TYPE_SYS:
		type_str = "graphics";
		break;
	case AMDSMI_CLK_TYPE_MEM:
		type_str = "memory";
		break;
	default:
		error("%s: Unsupported clock type", __func__);
		break;
	}

	START_TIMER;
	amdsmi_rc = amdsmi_get_clk_freq(processor_handles[dv_ind], type, &amdsmi_freqs);
	END_TIMER;
	debug3("amdsmi_get_clk_freq(%s) took %s", type_str, TIMER_STR());
	if (amdsmi_rc != AMDSMI_STATUS_SUCCESS) {
		amdsmi_rc = amdsmi_status_code_to_string(amdsmi_rc, &status_string);
		error("AMDSMI: Failed to get the GPU frequency type %s, error: %s",
		      type_str, status_string);
		return 0;
	}
	return amdsmi_freqs.frequency[amdsmi_freqs.current]/1000000;
}

static uint32_t _amdsmi_get_gfx_freq(uint32_t dv_ind)
{
	return _amdsmi_get_freq(dv_ind, AMDSMI_CLK_TYPE_SYS);
}

static uint32_t _amdsmi_get_mem_freq(uint32_t dv_ind)
{
	return _amdsmi_get_freq(dv_ind, AMDSMI_CLK_TYPE_MEM);
}

/*
 * Reset the frequencies of each GPU in the step to hardware defaults.
 * NOTE: AMDSMI must be initialized beforehand.
 *
 * gpus (IN) A bitmap specifying the GPUs on which to operate (local GPU IDs).
 */
static void _reset_freq(bitstr_t *gpus)
{
    if (!gpus) {
        error("AMDSMI: _reset_freq() called with NULL bitmask");
        return;
    }

    int gpu_len = bit_size(gpus);
    int count = 0;
    int count_set = 0;

    for (int i = 0; i < gpu_len; i++) {
        if (!bit_test(gpus, i))
            continue;  /* GPU not allocated */

        count++;

        /* Log BEFORE */
        uint32_t mem_before = _amdsmi_get_mem_freq(i);
        uint32_t gfx_before = _amdsmi_get_gfx_freq(i);

        debug2("AMDSMI: GPU[%d] memory frequency BEFORE reset: %u MHz",
               i, mem_before);
        debug2("AMDSMI: GPU[%d] graphics frequency BEFORE reset: %u MHz",
               i, gfx_before);

        bool ok = _amdsmi_reset_freqs(i);

        /* Log AFTER */
        uint32_t mem_after = _amdsmi_get_mem_freq(i);
        uint32_t gfx_after = _amdsmi_get_gfx_freq(i);

        debug2("AMDSMI: GPU[%d] memory frequency AFTER reset: %u MHz",
               i, mem_after);
        debug2("AMDSMI: GPU[%d] graphics frequency AFTER reset: %u MHz",
               i, gfx_after);

        if (ok) {
            log_flag(GRES, "AMDSMI: Successfully reset GPU[%d]", i);
            count_set++;
        } else {
            log_flag(GRES, "AMDSMI: FAILED to reset GPU[%d]", i);
        }
    }

    if (count_set != count) {
        log_flag(GRES,
                 "AMDSMI: reset failure — only %d/%d GPUs successfully reset",
                 count_set, count);

        fprintf(stderr,
                "AMDSMI: reset failure — only %d/%d GPUs successfully reset\n",
                count_set, count);
    }
}
/*
 * Set the frequencies of each GPU specified for the step.
 * AMDSMI must be initialized beforehand.
 *
 * gpus     (IN) A bitmap specifying GPUs on which to operate (LOCAL or GLOBAL)
 * gpu_freq (IN) Frequency request string e.g. "high,memory=low"
 */
static void _set_freq(bitstr_t *gpus, char *gpu_freq)
{
    if (!gpus || !gpu_freq) {
        error("AMDSMI: _set_freq() called with NULL argument");
        return;
    }

    bool verbose_flag = false;
    unsigned int gpu_freq_num = 0, mem_freq_num = 0;

    debug2("_parse_gpu_freq(%s)", gpu_freq);
    gpu_common_parse_gpu_freq(gpu_freq,
                              &gpu_freq_num,
                              &mem_freq_num,
                              &verbose_flag);

    if (verbose_flag)
        debug2("verbose_flag ON");

    /* Display parsed frequencies for debugging */
    char *s = gpu_common_freq_value_to_string(mem_freq_num);
    debug2("Requested GPU memory frequency: %s", s);
    xfree(s);

    s = gpu_common_freq_value_to_string(gpu_freq_num);
    debug2("Requested GPU graphics frequency: %s", s);
    xfree(s);

    if (!mem_freq_num && !gpu_freq_num) {
        debug2("%s: No frequencies to set", __func__);
        return;
    }

    /* --- Determine GPU index semantics --- */
    cgroup_conf_init();
    bool constrained_devices = slurm_cgroup_conf.constrain_devices;
    bool task_cgroup =
        (slurm_conf.task_plugin &&
         xstrstr(slurm_conf.task_plugin, "cgroup"));

    bool cgroups_active = (constrained_devices && task_cgroup);

    int gpu_len = 0;
    if (cgroups_active) {
        gpu_len = bit_set_count(gpus);
        debug2("%s: cgroups active -> using LOCAL GPU IDs", __func__);
    } else {
        gpu_len = bit_size(gpus);
        debug2("%s: cgroups NOT active -> using GLOBAL GPU IDs", __func__);
    }

    /* Iterate correctly: if cgroups active, iterate *set bits* */
    int count = 0, count_set = 0, iter = 0;

    for (int bit = 0; bit < bit_size(gpus); bit++) {
        if (!bit_test(gpus, bit))
            continue;

        /* When cgroups_active, only first gpu_len GPUs exist (LOCAL indexing)
           but bit positions may not match 0..gpu_len-1. We remap logically. */
        int dv_ind = cgroups_active ? iter : bit;
        iter++;
        count++;

        uint32_t mem_freq = mem_freq_num;
        uint32_t gfx_freq = gpu_freq_num;
        uint64_t mem_bitmask = 0, gfx_bitmask = 0;

        debug2("Setting frequency of AMDSMI device %u", dv_ind);

        _amdsmi_get_nearest_freqs(dv_ind,
                                  &mem_freq,  &mem_bitmask,
                                  &gfx_freq,  &gfx_bitmask);

        uint32_t mem_before = _amdsmi_get_mem_freq(dv_ind);
        uint32_t gfx_before = _amdsmi_get_gfx_freq(dv_ind);

        debug2("GPU[%d] Mem BEFORE: %u MHz", dv_ind, mem_before);
        debug2("GPU[%d] Gfx BEFORE: %u MHz", dv_ind, gfx_before);

        bool freq_set = _amdsmi_set_freqs(dv_ind,
                                          mem_bitmask,
                                          gfx_bitmask);

        uint32_t mem_after = _amdsmi_get_mem_freq(dv_ind);
        uint32_t gfx_after = _amdsmi_get_gfx_freq(dv_ind);

        debug2("GPU[%d] Mem AFTER: %u MHz", dv_ind, mem_after);
        debug2("GPU[%d] Gfx AFTER: %u MHz", dv_ind, gfx_after);

        /* Construct readable message */
        char *msg = NULL;
        const char *sep = "";

        if (mem_freq) {
            xstrfmtcat(msg, "%smemory_freq:%u", sep, mem_freq);
            sep = ",";
        }
        if (gfx_freq) {
            xstrfmtcat(msg, "%sgraphics_freq:%u", sep, gfx_freq);
        }

        if (freq_set) {
            log_flag(GRES, "AMDSMI: Successfully set GPU[%d] %s",
                     dv_ind, msg ? msg : "");
            count_set++;
        } else {
            log_flag(GRES, "AMDSMI: FAILED to set GPU[%d] %s",
                     dv_ind, msg ? msg : "");
        }

        if (verbose_flag && iter == 1) {
            fprintf(stderr, "GpuFreq=%s\n", msg ? msg : "");
        }

        xfree(msg);
        if (cgroups_active && iter >= gpu_len)
            break;
    }

    if (count_set != count) {
        log_flag(GRES,
                 "%s: Could not set frequencies for all GPUs (%d/%d succeeded)",
                 __func__, count_set, count);
        fprintf(stderr,
                 "Could not set frequencies for all GPUs %d/%d\n",
                 count_set, count);
    }
}


/*
 * Get the version of the AMD-SMI (AMDSMI) library.
 *
 * version (OUT) Buffer to store version string
 * len     (IN)  Size of the buffer
 */
static void _amdsmi_get_version(char *version, unsigned int len)
{
    if (!version || len == 0) {
        error("AMDSMI: _amdsmi_get_version() called with invalid buffer");
        return;
    }

    version[0] = '\0';   /* Always provide a safe default output */

    const char *status_string = NULL;
    amdsmi_version_t vinfo;
    amdsmi_status_t rc = amdsmi_get_lib_version(&vinfo);

    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get library version: %s",
              status_string ? status_string : "unknown");
        return;
    }

    /* Copy build string safely */
    snprintf(version, len, "%s", vinfo.build);

    /* Enforce minimum version for GPU usage accounting */
    if (vinfo.major < AMDSMI_REQ_VERSION_USAGE) {
        get_usage = false;
        error("%s: GPU usage accounting disabled. AMDSMI >= 6.0.0 required.",
              __func__);
    }
}
/*
 * Get the total number of AMD GPUs in the system.
 *
 * device_count (OUT) Number of available GPU devices.
 */
extern void gpu_p_get_device_count(uint32_t *device_count)
{
    if (!device_count) {
        error("AMDSMI: gpu_p_get_device_count() called with NULL pointer");
        return;
    }

    /* Ensure AMD-SMI is initialized and processor_handles[] is populated */
    _amdsmi_init();

    *device_count = processor_handle_count;

    debug2("AMDSMI: gpu_p_get_device_count -> %u device(s)",
           *device_count);
}

/*
 * Get the name (market name / user-facing) of the GPU.
 *
 * dv_ind       (IN)  Device index (local AMD-SMI index)
 * device_name  (OUT) Buffer to write name into
 * size         (IN)  Capacity of device_name buffer
 */
static void _amdsmi_get_device_name(uint32_t dv_ind,
                                    char *device_name,
                                    unsigned int size)
{
    if (!device_name || size == 0) {
        error("AMDSMI: _amdsmi_get_device_name() called with invalid buffer");
        return;
    }

    device_name[0] = '\0';

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return;
    }

    const char *status_string = NULL;
    amdsmi_asic_info_t info;
    memset(&info, 0, sizeof(info));

    amdsmi_processor_handle h = processor_handles[dv_ind];

    amdsmi_status_t rc = amdsmi_get_gpu_asic_info(h, &info);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get GPU[%u] ASIC info: %s",
              dv_ind, status_string ? status_string : "unknown");
        return;
    }

    /* market_name and vendor_name are fixed arrays, never NULL */
    if (info.market_name[0] != '\0') {
        snprintf(device_name, size, "%s", info.market_name);
    } else if (info.vendor_name[0] != '\0') {
        snprintf(device_name, size, "%s", info.vendor_name);
    } else {
        snprintf(device_name, size, "unknown");
    }
}
/*
 * Get the "brand" of the GPU — typically the market name or vendor.
 *
 * dv_ind       (IN)  Device index (local AMD-SMI index)
 * device_brand (OUT) Buffer to store the brand string
 * size         (IN)  Size of the provided buffer
 */
static void _amdsmi_get_device_brand(uint32_t dv_ind,
                                     char *device_brand,
                                     unsigned int size)
{
    if (!device_brand || size == 0) {
        error("AMDSMI: _amdsmi_get_device_brand() called with invalid buffer");
        return;
    }

    device_brand[0] = '\0';

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return;
    }

    const char *status_string = NULL;
    amdsmi_asic_info_t info;
    memset(&info, 0, sizeof(info));

    amdsmi_processor_handle h = processor_handles[dv_ind];

    amdsmi_status_t rc = amdsmi_get_gpu_asic_info(h, &info);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get GPU[%u] brand info: %s",
              dv_ind, status_string ? status_string : "unknown");
        return;
    }

    /* market_name and vendor_name are fixed arrays, never NULL */
    if (info.market_name[0] != '\0') {
        snprintf(device_brand, size, "%s", info.market_name);
    } else if (info.vendor_name[0] != '\0') {
        snprintf(device_brand, size, "%s", info.vendor_name);
    } else {
        snprintf(device_brand, size, "unknown");
    }
}
/*
 * Retrieves DRM renderD* minor number for the GPU.
 *
 * dv_ind (IN)   AMD-SMI device index
 * minor (OUT)   DRM render node minor number
 */
static void _amdsmi_get_device_minor_number(uint32_t dv_ind,
                                            unsigned int *minor)
{
    if (!minor) {
        error("AMDSMI: _amdsmi_get_device_minor_number() called with NULL pointer");
        return;
    }

    *minor = 0;

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return;
    }

    const char *status_string = NULL;
    amdsmi_bdf_t bdf;
    memset(&bdf, 0, sizeof(bdf));

    amdsmi_processor_handle h = processor_handles[dv_ind];

    amdsmi_status_t rc = amdsmi_get_gpu_device_bdf(h, &bdf);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get PCI BDF for GPU[%u]: %s",
              dv_ind, status_string ? status_string : "unknown");
        return;
    }

    /* Build sysfs path: /sys/bus/pci/devices/xxxx:bb:dd.f/drm/ */
    char path[256];
    snprintf(path, sizeof(path),
             "/sys/bus/pci/devices/%04x:%02x:%02x.%x/drm",
             (unsigned)bdf.domain_number,
             (unsigned)bdf.bus_number,
             (unsigned)bdf.device_number,
             (unsigned)bdf.function_number);

    DIR *d = opendir(path);
    if (!d) {
        error("AMDSMI: Cannot open DRM sysfs dir for GPU[%u] at %s",
              dv_ind, path);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        unsigned int m = 0;

        if (sscanf(de->d_name, "renderD%u", &m) == 1) {
            closedir(d);
            *minor = m;
            return;
        }
    }

    closedir(d);
    error("AMDSMI: No renderD* node found for GPU[%u] (%04x:%02x:%02x.%x)",
          dv_ind,
          (unsigned)bdf.domain_number,
          (unsigned)bdf.bus_number,
          (unsigned)bdf.device_number,
          (unsigned)bdf.function_number);
}

/*
 * Get the PCI BDF info for a GPU
 *
 * dv_ind (IN)   Device index (AMD-SMI index)
 * pci    (OUT)  PCI info in amdsmiPciInfo_t wrapper
 */
static void _amdsmi_get_device_pci_info(uint32_t dv_ind, amdsmiPciInfo_t *pci)
{
    if (!pci) {
        error("AMDSMI: _amdsmi_get_device_pci_info() called with NULL pci pointer");
        return;
    }

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return;
    }

    const char *status_string = NULL;
    amdsmi_bdf_t bdf;
    memset(&bdf, 0, sizeof(bdf));

    amdsmi_processor_handle h = processor_handles[dv_ind];

    amdsmi_status_t rc = amdsmi_get_gpu_device_bdf(h, &bdf);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get PCI BDF for GPU[%u]: %s",
              dv_ind, status_string ? status_string : "unknown");
        return;
    }

    pci->domain   = (uint32_t)bdf.domain_number;
    pci->bus      = (uint32_t)bdf.bus_number;
    pci->device   = (uint32_t)bdf.device_number;
    pci->function = (uint32_t)bdf.function_number;
}
/*
 * Get the Unique ID of the GPU
 *
 * dv_ind (IN)  The device index (AMD-SMI logical index)
 * id     (OUT) Pointer to a uint64_t receiving the unique id
 */
static void _amdsmi_get_device_unique_id(uint32_t dv_ind, uint64_t *id)
{
    if (!id) {
        error("AMDSMI: _amdsmi_get_device_unique_id() called with NULL pointer");
        return;
    }

    *id = 0;

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return;
    }

    const char *status_string = NULL;
    amdsmi_processor_handle h = processor_handles[dv_ind];

    /*
     * Use ASIC info's device_id as a numeric stable unique id.
     * amdsmi_asic_info_t contains device_id (uint64_t). [1](https://rocm.docs.amd.com/projects/amdsmi/en/latest/doxygen/docBin/html/structamdsmi__asic__info__t.html)
     */
    amdsmi_asic_info_t info;
    memset(&info, 0, sizeof(info));

    amdsmi_status_t rc = amdsmi_get_gpu_asic_info(h, &info);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get Unique ID (asic_info.device_id) of GPU[%u]: %s",
              dv_ind, status_string ? status_string : "unknown");
        return;
    }

    *id = info.device_id;
}

/*
 * Get the NUMA CPU mask associated with a GPU.
 *
 * dv_ind (IN) The device index (AMD-SMI index)
 *
 * Returns: bitstr_t* (Slurm CPU mask), or NULL if unavailable.
 */
static bitstr_t *_amdsmi_get_device_cpu_mask(uint32_t dv_ind)
{
    bitstr_t *cpu_aff_mac_bitstr = NULL;

#ifndef HAVE_NUMA
    return NULL;  /* No NUMA support at build time */
#else
    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        return NULL;
    }

    amdsmi_processor_handle h = processor_handles[dv_ind];
    const char *status_string = NULL;

    uint32_t nnid = 0;
    amdsmi_status_t rc =
        amdsmi_topo_get_numa_node_number(h, &nnid);  /* CORRECT API */

    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get NUMA node for GPU[%u]: %s",
              dv_ind, status_string ? status_string : "unknown");
        return NULL;
    }

    uint16_t maxcpus =
        conf->sockets * conf->cores * conf->threads;

    struct bitmask *collective = numa_allocate_cpumask();
    if (!collective) {
        error("AMDSMI: failed to allocate cpumask");
        return NULL;
    }

    if (collective->size < maxcpus) {
        error("AMDSMI: NUMA cpumask too small (%lu < %u)",
              collective->size, maxcpus);
        numa_free_cpumask(collective);
        return NULL;
    }

    /* NUMA v1 quirk: size in bytes, not bits */
    if (numa_node_to_cpus(nnid,
                          collective->maskp,
                          collective->size / 8)) {
        error("numa_node_to_cpus: %m");
        numa_free_cpumask(collective);
        return NULL;
    }

    cpu_aff_mac_bitstr = bit_alloc(maxcpus);
    if (!cpu_aff_mac_bitstr) {
        error("AMDSMI: failed to allocate Slurm CPU bitmask");
        numa_free_cpumask(collective);
        return NULL;
    }

    for (int cpu = 0; cpu < maxcpus; cpu++) {
        if (numa_bitmask_isbitset(collective, cpu))
            bit_set(cpu_aff_mac_bitstr, cpu);
    }

    numa_free_cpumask(collective);
    return cpu_aff_mac_bitstr;
#endif /* HAVE_NUMA */
}

/*
 * Creates and returns a GRES config list of detected AMD GPUs on the node.
 * If an error occurs, return NULL.
 *
 * node_config (IN/OUT) The node_config_load_t from Slurm.
 */
static list_t *_get_system_gpu_list_amdsmi(node_config_load_t *node_config)
{
    if (!node_config) {
        error("AMDSMI: node_config is NULL in _get_system_gpu_list_amdsmi()");
        return NULL;
    }

    _amdsmi_init(); /* Ensure handles are populated */

    uint32_t device_count = 0;
    gpu_p_get_device_count(&device_count);

    debug2("AMDSMI: Detected %u GPU(s)", device_count);

    list_t *gres_list_system = list_create(destroy_gres_slurmd_conf);
    if (!gres_list_system) {
        error("AMDSMI: Failed to allocate GRES list");
        return NULL;
    }

    for (uint32_t i = 0; i < device_count; i++) {

        char device_name[AMDSMI_STRING_BUFFER_SIZE]  = {0};
        char device_brand[AMDSMI_STRING_BUFFER_SIZE] = {0};

        uint64_t uuid = 0;
        unsigned int minor_number = 0;
        amdsmiPciInfo_t pci_info = {0};
        char *cpu_aff_mac_range = NULL;

        /* ---------------------------
         * Build gres_slurmd_conf_t
         * --------------------------- */
        gres_slurmd_conf_t gres = {
            .config_flags = GRES_CONF_ENV_AMDSMI | GRES_CONF_AUTODETECT,
            .count        = 1,
            .cpu_cnt      = node_config->cpu_cnt,
            .cpus_bitmap  = _amdsmi_get_device_cpu_mask(i),
            .name         = "gpu"
        };

        /* CPU mask (machine form) */
        //gres.cpus_bitmap = _amdsmi_get_device_cpu_mask(i);

        if (gres.cpus_bitmap) {
            cpu_aff_mac_range = bit_fmt_full(gres.cpus_bitmap);

            /* Convert machine cpuset → Slurm abstract cpuset */
            if (node_config->xcpuinfo_mac_to_abs(cpu_aff_mac_range,
                                                 &gres.cpus)) {
                error("AMDSMI: Failed CPU set machine→abstract conversion");
                FREE_NULL_BITMAP(gres.cpus_bitmap);
                xfree(cpu_aff_mac_range);
                continue;
            }
        }

        /* Device name, brand, PCI BDF, UUID, renderD* minor */
        _amdsmi_get_device_name(i, device_name, sizeof(device_name));
        _amdsmi_get_device_brand(i, device_brand, sizeof(device_brand));
        _amdsmi_get_device_minor_number(i, &minor_number);
        _amdsmi_get_device_pci_info(i, &pci_info);
        _amdsmi_get_device_unique_id(i, &uuid);

        /* Links entry: order by AMD-SMI index number */
        gres.links = gres_links_create_empty(i, device_count);

        /* Build GPU device file path */
        xstrfmtcat(gres.file, "/dev/dri/renderD%u", minor_number);

        /* Logging */
        debug2("AMDSMI: GPU[%u]:", i);
        debug2("    Name:         %s", device_name);
        debug2("    Brand/Type:   %s", device_brand);
        debug2("    UUID:         0x%lx", uuid);
        debug2("    PCI BDF:      %04x:%02x:%02x.%x",
               pci_info.domain, pci_info.bus,
               pci_info.device, pci_info.function);
        debug2("    Device file:  %s", gres.file);
        debug2("    Links:        %s", gres.links);
        debug2("    CPU Aff MAC:  %s",
               cpu_aff_mac_range ? cpu_aff_mac_range : "NULL");
        debug2("    CPU Aff ABS:  %s",
               gres.cpus ? gres.cpus : "NULL");

        /* Frequencies (debug only) */
        _amdsmi_print_freqs(i, LOG_LEVEL_DEBUG2);

        /* Prefer brand, fallback to device_name */
        gres.type_name = device_brand[0] ? device_brand : device_name;

        /* Add final GRES entry */
        add_gres_to_list(gres_list_system, &gres);

        /* Cleanup for next iteration */
        FREE_NULL_BITMAP(gres.cpus_bitmap);
        xfree(cpu_aff_mac_range);
        xfree(gres.cpus);
        xfree(gres.file);
        xfree(gres.links);
    }

    info("AMDSMI: %u GPU(s) added to node GRES list", device_count);
    return gres_list_system;
}

extern list_t *gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
    list_t *gres_list_system = _get_system_gpu_list_amdsmi(node_config);

    if (!gres_list_system) {
        error("AMDSMI: system GPU autodetection failed");
        return NULL;
    }

    return gres_list_system;
}

extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
    if (!usable_gpus || !tres_freq)
        return;

    /* Find gpu:... inside TRES string */
    char *gpu_part = strstr(tres_freq, "gpu:");
    if (!gpu_part)
        return;  /* No GPU frequency specification */

    /* Copy everything after "gpu:" up to an optional ';' */
    char *freq = xstrdup(gpu_part + 4);
    char *semi = strchr(freq, ';');
    if (semi)
        *semi = '\0';

    /* Save usable_gpus for reset in gpu_p_step_hardware_fini() */
    FREE_NULL_BITMAP(saved_gpus);
    saved_gpus = bit_copy(usable_gpus);

    /* Initialize AMD-SMI (idempotent) */
    _amdsmi_init();

    /* Apply frequency policy */
    _set_freq(usable_gpus, freq);

    xfree(freq);
}

extern void gpu_p_step_hardware_fini(void)
{
    if (!saved_gpus)
        return;

    /* Restore frequencies to hardware defaults */
    _reset_freq(saved_gpus);

    FREE_NULL_BITMAP(saved_gpus);
    /* DO NOT call amdsmi_shut_down() here.
     * Slurm calls plugin fini() which performs backend shutdown.
     */
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

/*
 * Read current average socket power for GPU dv_ind.
 * Update gpu_status_t accordingly.
 */
extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
    if (!gpu) {
        error("AMDSMI: gpu_p_energy_read() called with NULL gpu pointer");
        return SLURM_ERROR;
    }

    if (dv_ind >= processor_handle_count) {
        error("AMDSMI: Invalid device index %u (max %u)",
              dv_ind, processor_handle_count);
        gpu->energy.current_watts = NO_VAL;
        return SLURM_ERROR;
    }

    amdsmi_processor_handle h = processor_handles[dv_ind];
    const char *status_string = NULL;

    amdsmi_power_info_t power_info;
    memset(&power_info, 0, sizeof(power_info));

    amdsmi_status_t rc = amdsmi_get_power_info(h, &power_info);
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void)amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: Failed to get power for GPU[%u]: %s",
              dv_ind, status_string ? status_string : "unknown");

        gpu->energy.current_watts = NO_VAL;
        return SLURM_ERROR;
    }

    /* average_socket_power is reported in W, NOT mW */
    double watts = (double)power_info.average_socket_power;

    gpu->energy.current_watts = watts;
    gpu->previous_update_time = gpu->last_update_time;
    gpu->last_update_time     = time(NULL);
    gpu->last_update_watt     = watts;

    return SLURM_SUCCESS;
}
extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
    if (!data) {
        error("AMDSMI: gpu_p_usage_read() called with NULL data pointer");
        return SLURM_ERROR;
    }

    bool track_gpumem  = (gpumem_pos  != -1);
    bool track_gpuutil = (gpuutil_pos != -1);

    if (!track_gpumem && !track_gpuutil) {
        debug2("%s: TRES gpuutil/gpumem not tracked", __func__);
        return SLURM_SUCCESS;
    }

    if (!get_usage) {
        debug2("%s: AMDSMI < required version; usage disabled", __func__);
        return SLURM_SUCCESS;
    }

    _amdsmi_init();

    /*
     * NOTE: The current AMD-SMI API for process info is NOT per-GPU handle.
     * Signature: amdsmi_get_gpu_compute_process_info_by_pid(uint32_t pid, amdsmi_process_info_t *proc) [1](https://github.com/ROCm/amdsmi)
     */
    amdsmi_process_info_t pinfo;
    memset(&pinfo, 0, sizeof(pinfo));

    const char *status_string = NULL;
    amdsmi_status_t rc =
        amdsmi_get_gpu_compute_process_info_by_pid((uint32_t)pid, &pinfo); /* [1](https://github.com/ROCm/amdsmi) */

    if (rc == AMDSMI_STATUS_NOT_FOUND) {
        /* PID not using GPU; normal */
        return SLURM_SUCCESS;
    }
    if (rc != AMDSMI_STATUS_SUCCESS) {
        (void) amdsmi_status_code_to_string(rc, &status_string);
        error("AMDSMI: process usage read failed for pid %d: %s",
              pid, status_string ? status_string : "unknown");
        return SLURM_ERROR;
    }

    /* pinfo.cu_occupancy is percent (0-100) [2](https://rocm.docs.amd.com/projects/amdsmi/en/latest/doxygen/docBin/html/amdsmi_8h.html) */
    if (track_gpuutil) {
        data[gpuutil_pos].size_read = (uint64_t)pinfo.cu_occupancy;
    }

    /*
     * pinfo.vram_usage is in MB (per docs). Convert to bytes for Slurm gpumem TRES. [2](https://rocm.docs.amd.com/projects/amdsmi/en/latest/doxygen/docBin/html/amdsmi_8h.html)
     */
    if (track_gpumem) {
        data[gpumem_pos].size_read = (uint64_t)pinfo.vram_usage * 1024ULL * 1024ULL;
    }

    log_flag(JAG,
             "pid %d: GPUUtil=%lu%% MemMB=%lu",
             pid,
             track_gpuutil ? data[gpuutil_pos].size_read : 0UL,
             (unsigned long)pinfo.vram_usage);

    return SLURM_SUCCESS;
}