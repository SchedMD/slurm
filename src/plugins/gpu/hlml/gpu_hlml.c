/*****************************************************************************\
 *  gpu_hlml.c - Support HLML interface to a Gaudi AI accelerators.
 *****************************************************************************
 *  Copyright (C) 2019 SchedMD LLC
 *  Copyright (C) 2024 HabanaLabs Ltd.
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

#include <ctype.h>
#include <stdio.h>
#include <hlml.h>

#include "../common/gpu_common.h"

#define HL_FIELD_MAX_SIZE	256
#define MAX_CPUS			0x8000
#define ULONG_BYTES			(sizeof(unsigned long))
#define ULONG_BITS			(ULONG_BYTES * 8)
#define CPU_SET_SIZE		((MAX_CPUS + (ULONG_BITS-1)) / ULONG_BITS)


#define CHECK_HLML(x) do { \
  int retval = (x); \
  if (retval != HLML_SUCCESS) { \
    error("HLML error: %s returned %d at %s:%d", #x, retval, __FILE__, __LINE__); \
  } \
} while (0)

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
const char		plugin_name[]	= "Gaudi HLML plugin";
const char		plugin_type[]	= "gpu/hlml";
const uint32_t	plugin_version	= SLURM_VERSION_NUMBER;

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

extern int gpu_p_reconfig(void)
{
	return SLURM_SUCCESS;
}

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
 * Creates and returns a gres conf list of detected Habana accelerators on the node.
 * If an error occurs, return NULL
 * Caller is responsible for freeing the list.
 *
 * If the HLML API exists, then query Gaudi info,
 * so the user doesn't need to specify manually in gres.conf.
 *
 * node_config (IN/OUT) pointer of node_config_load_t passed down
 */
static List _get_system_hpu_list_hlml(node_config_load_t *node_config)
{
	unsigned int i;
	unsigned int device_count = 0;
	List gres_list_system = list_create(destroy_gres_slurmd_conf);

	xassert(node_config->xcpuinfo_mac_to_abs);

	CHECK_HLML(hlml_init());
	CHECK_HLML(hlml_device_get_count(&device_count));

	debug2("Device count: %d", device_count);

	// Loop through all the Gaudi accelerators on the system and add to gres_list_system
	for (i = 0; i < device_count; ++i) {
		unsigned int minor_number = 0;
		char device_name[HL_FIELD_MAX_SIZE] = {0};
		char uuid[HL_FIELD_MAX_SIZE] = {0};
		unsigned long cpu_affinity[CPU_SET_SIZE] = {0};
		char *cpu_affinity_mac_range = NULL;
		hlml_pci_info_t pci_info;
		hlml_device_t device;
		gres_slurmd_conf_t gres_slurmd_conf = {
			.config_flags = GRES_CONF_ENV_HLML,
			.count = 1,
			.cpu_cnt = node_config->cpu_cnt,
			.name = "gpu"
		};

		memset(cpu_affinity, 0, sizeof(unsigned long) * CPU_SET_SIZE);

		CHECK_HLML(hlml_device_get_handle_by_index(i, &device));
		CHECK_HLML(hlml_device_get_name(device, device_name, HL_FIELD_MAX_SIZE));
		CHECK_HLML(hlml_device_get_minor_number(device, &minor_number));
		CHECK_HLML(hlml_device_get_pci_info(device, &pci_info));
		CHECK_HLML(hlml_device_get_uuid(device, uuid, HL_FIELD_MAX_SIZE));
		CHECK_HLML(hlml_device_get_cpu_affinity(device, CPU_SET_SIZE, cpu_affinity));

		gres_slurmd_conf.type_name = device_name;
		gres_slurmd_conf.unique_id = uuid;

		xstrfmtcat(gres_slurmd_conf.file, "/dev/accel/accel%u", minor_number);

		gres_slurmd_conf.cpus_bitmap = bit_alloc(MAX_CPUS);
		_set_cpu_set_bitstr(gres_slurmd_conf.cpus_bitmap, cpu_affinity, CPU_SET_SIZE);
		cpu_affinity_mac_range = bit_fmt_full(gres_slurmd_conf.cpus_bitmap);

		/*
		 * Convert cpu range str from machine to abstract (slurm) format
		 */
		if (node_config->xcpuinfo_mac_to_abs(cpu_affinity_mac_range,
						     &gres_slurmd_conf.cpus)) {
			error("Conversion from machine to abstract failed");
			FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
			xfree(cpu_affinity_mac_range);
			continue;
		}

		gres_slurmd_conf.links = gres_links_create_empty(i, device_count);

		debug2("Gaudi index %u:", i);
		debug2("    Name: %s", gres_slurmd_conf.type_name);
		debug2("    UUID: %s", gres_slurmd_conf.unique_id);
		debug2("    PCI Domain/Bus/Device: %u:%u:%u", pci_info.domain,
		       pci_info.bus, pci_info.device);
		debug2("    Device File (minor number): %s", gres_slurmd_conf.file);
		if (minor_number != i)
			debug("Note: Gaudi index %u is different from minor # %u",
			      i, minor_number);
		debug2("    CPU Affinity Range: %s", cpu_affinity_mac_range);
		debug2("    CPU Affinity Range Abstract: %s", gres_slurmd_conf.cpus);

		// Temporary solution until the runtime will know to run according
		// to actual UUIDs.
		sprintf(gres_slurmd_conf.unique_id, "%d", i);

		add_gres_to_list(gres_list_system, &gres_slurmd_conf);

		FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
		xfree(cpu_affinity_mac_range);
		xfree(gres_slurmd_conf.cpus);
		xfree(gres_slurmd_conf.links);
	}

	CHECK_HLML(hlml_shutdown());

	info("%u Gaudi system device(s) detected", device_count);
	return gres_list_system;
}

extern List gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	List gres_list_system = _get_system_hpu_list_hlml(node_config);

	if (!gres_list_system)
		error("System Gaudi accelerators detection failed");

	return gres_list_system;
}

extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	xassert(tres_freq);
	xassert(usable_gpus);

	if (!usable_gpus)
		return;		/* Job allocated no Gaudi's */
	if (!tres_freq)
		return;		/* No TRES frequency spec */

	if (!strstr(tres_freq, "gpu:"))
		return;		/* No Gaudi frequency spec */

	fprintf(stderr, "GpuFreq=control_disabled\n");
}

extern void gpu_p_step_hardware_fini(void)
{
	return;
}

extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

extern void gpu_p_get_device_count(unsigned int *device_count)
{
	CHECK_HLML(hlml_init());
	CHECK_HLML(hlml_device_get_count(device_count));
	CHECK_HLML(hlml_shutdown());
}

extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	return SLURM_SUCCESS;
}

extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
	return SLURM_SUCCESS;
}