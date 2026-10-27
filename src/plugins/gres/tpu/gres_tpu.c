/*****************************************************************************\
 *  gres_tpu.c - Support TPUs as a generic resource.
 *****************************************************************************
 *  Copyright (C) 2026 Google LLC.
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

#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/bitstring.h"
#include "src/common/env.h"
#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/tpu_conf.h"
#include "src/common/xstring.h"
#include "src/interfaces/gres.h"

#include "../common/gres_common.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Gres TPU plugin";
const char plugin_type[] = "gres/tpu";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define DEFAULT_SYSFS_PATH "/sys/bus/pci/devices"

/*
 * Maximum PCI functions per device slot (PCI standard allows up to 8;
 * multi-core TPU chips expose 2 functions per device).
 */
#define TPU_MAX_CORES 8

static list_t *gres_devices = NULL;
static tpu_conf_t *tpu_conf = NULL;

/*
 * One physical accelerator chip, assembled from the PCI functions that share
 * its base address. A chip is NOT a PCI device: tpu7x presents two functions
 * per chip, each with its own IOMMU group and its own VFIO node.
 */
typedef struct {
	char *base_addr; /* "0000:00:05", i.e. domain:bus:device */
	char *cpus; /* local_cpulist of the lowest-numbered core */
	const tpu_type_entry_t *entry; /* family this chip belongs to */
	long sort_key; /* IOMMU group of the lowest-numbered core */
	char *vfio[TPU_MAX_CORES]; /* "/dev/vfio/<group>" per core */
} tpu_chip_t;

typedef struct {
	uint32_t chip_cnt;
	const tpu_type_entry_t *entry;
	list_t *gres_list;
	node_config_load_t *node_config;
} build_args_t;

static void _destroy_tpu_chip(void *x)
{
	tpu_chip_t *chip = x;

	if (!chip)
		return;

	xfree(chip->base_addr);
	xfree(chip->cpus);
	for (int i = 0; i < TPU_MAX_CORES; i++)
		xfree(chip->vfio[i]);
	xfree(chip);
}

/*
 * Read the first line of a sysfs attribute.
 * RET xmalloc()'d contents with the trailing newline removed, or NULL.
 */
static char *_read_attr(const char *dir, const char *name, const char *attr)
{
	char *path = NULL, *line = NULL, *value = NULL;
	size_t size = 0;
	FILE *fp;

	xstrfmtcat(path, "%s/%s/%s", dir, name, attr);
	fp = fopen(path, "r");
	xfree(path);

	if (!fp)
		return NULL;

	/* local_cpulist of a large node can be longer than any fixed buffer */
	if (getline(&line, &size, fp) > 0) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0])
			value = xstrdup(line);
	}
	fclose(fp);
	free(line);

	return value;
}

/*
 * Resolve the IOMMU group of a PCI device.
 * RET group number, or -1 if the device is not bound to one. A TPU with no
 *	IOMMU group has no /dev/vfio node and cannot be handed to a job, so it
 *	is not a usable resource.
 */
static long _iommu_group(const char *dir, const char *name)
{
	char *path = NULL, *base, *end = NULL;
	char target[PATH_MAX];
	ssize_t len;
	long group;

	xstrfmtcat(path, "%s/%s/iommu_group", dir, name);
	len = readlink(path, target, sizeof(target) - 1);
	xfree(path);

	if (len <= 0)
		return -1;
	target[len] = '\0';

	if (!(base = xstrrchr(target, '/')) || (base[1] < '0') ||
	    (base[1] > '9'))
		return -1;

	group = strtol(base + 1, &end, 10);
	if ((group < 0) || !end || *end)
		return -1;

	return group;
}

static int _find_chip(void *x, void *key)
{
	tpu_chip_t *chip = x;

	return !xstrcmp(chip->base_addr, (char *) key);
}

/*
 * Order chips by the IOMMU group of their first core, numerically.
 *
 * The GRES index a task sees is this list's order, so it has to be stable and
 * it has to match what every other tool on the node reports. Comparing the
 * device paths as strings would put /dev/vfio/10 before /dev/vfio/9 and
 * silently bind tasks to the wrong chips on any node with ten or more groups.
 */
static int _sort_chips(void *x, void *y)
{
	tpu_chip_t *a = *(tpu_chip_t **) x;
	tpu_chip_t *b = *(tpu_chip_t **) y;

	if (a->sort_key < b->sort_key)
		return -1;
	if (a->sort_key > b->sort_key)
		return 1;

	return 0;
}

/*
 * Walk sysfs and assemble the chips physically present on this node.
 *
 * RET list of tpu_chip_t*, sorted. Never NULL, may be empty.
 */
static list_t *_scan_sysfs(const char *sysfs_path)
{
	list_t *chips = list_create(_destroy_tpu_chip);
	DIR *dir;
	struct dirent *ent;

	if (!(dir = opendir(sysfs_path))) {
		debug("%s: cannot open %s, no TPUs will be detected",
		      plugin_type, sysfs_path);
		return chips;
	}

	while ((ent = readdir(dir))) {
		char *vendor, *device, *base_addr, *dot, *end = NULL;
		const tpu_type_entry_t *entry;
		tpu_chip_t *chip;
		long group, core;

		if (ent->d_name[0] == '.')
			continue;

		/*
		 * Vendor 0x1ae0 also covers non-TPU virtual devices (e.g.
		 * gVNIC), so match both PCI vendor and device ID against
		 * families configured in tpu.yaml.
		 */
		vendor = _read_attr(sysfs_path, ent->d_name, "vendor");
		device = _read_attr(sysfs_path, ent->d_name, "device");
		entry = tpu_conf_find_pci_id(tpu_conf, vendor, device);
		if (!entry) {
			debug2("%s: ignoring %s, PCI %s:%s is not configured in %s",
			       plugin_type, ent->d_name,
			       vendor ? vendor : "(none)",
			       device ? device : "(none)", TPU_CONF_FILE);
			xfree(vendor);
			xfree(device);
			continue;
		}
		xfree(vendor);
		xfree(device);

		if ((group = _iommu_group(sysfs_path, ent->d_name)) < 0) {
			error("%s: %s matches a configured TPU but has no IOMMU group; it cannot be isolated and will be ignored. Check that the host booted with an IOMMU enabled.",
			      plugin_type, ent->d_name);
			continue;
		}

		/* "0000:00:05.1" -> base "0000:00:05", core 1 */
		base_addr = xstrdup(ent->d_name);
		if (!(dot = xstrrchr(base_addr, '.')) || !dot[1]) {
			error("%s: PCI address %s has no function number",
			      plugin_type, ent->d_name);
			xfree(base_addr);
			continue;
		}
		*dot = '\0';
		core = strtol(dot + 1, &end, 16);

		if ((core < 0) || (core >= TPU_MAX_CORES) || !end || *end) {
			error("%s: PCI address %s has invalid function number",
			      plugin_type, ent->d_name);
			xfree(base_addr);
			continue;
		}

		if (!(chip = list_find_first(chips, _find_chip, base_addr))) {
			chip = xmalloc(sizeof(*chip));
			chip->base_addr = xstrdup(base_addr);
			chip->entry = entry;
			chip->sort_key = group;
			list_append(chips, chip);
		} else if (chip->entry != entry) {
			error("%s: PCI device %s reports a different TPU family than its sibling functions; ignoring",
			      plugin_type, ent->d_name);
			xfree(base_addr);
			continue;
		}

		if (chip->vfio[core]) {
			error("%s: duplicate PCI function %s", plugin_type,
			      ent->d_name);
			xfree(base_addr);
			continue;
		}

		xstrfmtcat(chip->vfio[core], "/dev/vfio/%ld", group);

		/*
		 * Cores of one chip hang off the same physical device, so any
		 * of them gives the same CPU affinity. Keep the lowest one so
		 * the result does not depend on readdir() order.
		 */
		if ((group < chip->sort_key) || !chip->cpus) {
			if (group < chip->sort_key)
				chip->sort_key = group;
			xfree(chip->cpus);
			chip->cpus = _read_attr(sysfs_path, ent->d_name,
						"local_cpulist");
		}

		xfree(base_addr);
	}
	closedir(dir);

	list_sort(chips, _sort_chips);

	return chips;
}

/* Join a chip's VFIO nodes in core order, e.g. "/dev/vfio/8,/dev/vfio/9". */
static char *_chip_file_str(tpu_chip_t *chip, int *core_cnt)
{
	char *files = NULL;

	*core_cnt = 0;
	for (int i = 0; i < TPU_MAX_CORES; i++) {
		if (!chip->vfio[i])
			continue;
		xstrfmtcat(files, "%s%s", (files ? "," : ""), chip->vfio[i]);
		(*core_cnt)++;
	}

	return files;
}

static int _build_gres_record(void *x, void *arg)
{
	tpu_chip_t *chip = x;
	build_args_t *args = arg;
	gres_slurmd_conf_t gres_slurmd_conf = { 0 };
	char *abs_cpus = NULL;
	int core_cnt = 0;

	gres_slurmd_conf.file = _chip_file_str(chip, &core_cnt);
	if (!gres_slurmd_conf.file) {
		error("%s: chip at %s has no usable cores", plugin_type,
		      chip->base_addr);
		return SLURM_SUCCESS;
	}

	/*
	 * sysfs reports machine CPU numbering; Slurm records store both the
	 * abstract range string (cpus) and the machine-formatted bitmap
	 * (cpus_bitmap). Affinity is an optimization, so a failed conversion
	 * degrades to no affinity rather than dropping the chip.
	 */
	if (chip->cpus && args->node_config->xcpuinfo_mac_to_abs) {
		if (args->node_config->xcpuinfo_mac_to_abs(chip->cpus,
							   &abs_cpus)) {
			abs_cpus = NULL;
		} else {
			gres_slurmd_conf.cpus_bitmap =
				bit_alloc(args->node_config->cpu_cnt);
			if (!bit_size(gres_slurmd_conf.cpus_bitmap) ||
			    bit_unfmt(gres_slurmd_conf.cpus_bitmap,
				      chip->cpus)) {
				FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
				xfree(abs_cpus);
			}
		}
	}

	/*
	 * One record per chip, whatever its core count. add_gres_to_list()
	 * derives GRES_CONF_HAS_FILE and GRES_CONF_HAS_MULT from the file
	 * list, so a multi-core chip becomes a MultipleFiles GRES on its own.
	 */
	gres_slurmd_conf.config_flags = GRES_CONF_AUTODETECT;
	gres_slurmd_conf.count = 1;
	gres_slurmd_conf.cpu_cnt = args->node_config->cpu_cnt;
	gres_slurmd_conf.cpus = abs_cpus;
	gres_slurmd_conf.name = "tpu";
	gres_slurmd_conf.type_name = chip->entry->type;

	add_gres_to_list(args->gres_list, &gres_slurmd_conf);

	debug("%s: chip %s (%s) with %d core%s: %s", plugin_type,
	      chip->base_addr, chip->entry->type, core_cnt,
	      (core_cnt == 1 ? "" : "s"), gres_slurmd_conf.file);

	args->chip_cnt++;
	args->entry = chip->entry;

	FREE_NULL_BITMAP(gres_slurmd_conf.cpus_bitmap);
	xfree(abs_cpus);
	xfree(gres_slurmd_conf.file);

	return SLURM_SUCCESS;
}

/*
 * Build the GRES records for the TPUs attached to this node.
 * RET list of gres_slurmd_conf_t*, or NULL if no TPUs were found.
 */
static list_t *_get_system_tpu_list(node_config_load_t *node_config)
{
	const char *sysfs_path;
	list_t *chips, *gres_list;
	build_args_t args = { 0 };

	/*
	 * An operator who says AutoDetect=off has taken responsibility for
	 * gres.conf, so leave it alone. Every other setting, including no
	 * setting at all, discovers: a node with no TPUs finds nothing and
	 * loses nothing, so there is no reason to make the common case opt in.
	 * AutoDetect=tpu is accepted and means the same thing, spelled out.
	 */
	if (gres_get_autodetect_flags() & GRES_AUTODETECT_GPU_OFF) {
		debug("%s: AutoDetect=off, not scanning for TPUs", plugin_type);
		return NULL;
	}

	/*
	 * Overridable via SLURM_TPU_SYSFS_PATH for testing against mock sysfs
	 * trees.
	 */
	if (!(sysfs_path = getenv("SLURM_TPU_SYSFS_PATH")))
		sysfs_path = DEFAULT_SYSFS_PATH;

	chips = _scan_sysfs(sysfs_path);
	if (!list_count(chips)) {
		FREE_NULL_LIST(chips);
		return NULL;
	}

	gres_list = list_create(destroy_gres_slurmd_conf);

	args.gres_list = gres_list;
	args.node_config = node_config;
	(void) list_for_each(chips, _build_gres_record, &args);

	FREE_NULL_LIST(chips);

	if (!list_count(gres_list)) {
		FREE_NULL_LIST(gres_list);
		return NULL;
	}

	/*
	 * A node that reports a different chip count than its family's
	 * shape_per_node implies is misconfigured. Sizing decisions upstream
	 * are made from the shape, so surface it loudly rather than
	 * registering a count that will not add up.
	 */
	if (args.entry && (args.chip_cnt != args.entry->chips_per_node))
		error("%s: found %u %s chips but %s declares shape_per_node %s, which is %u chips per node. Jobs sized from --layout will not fit this node.",
		      plugin_type, args.chip_cnt, args.entry->type,
		      TPU_CONF_FILE, args.entry->shape_per_node,
		      args.entry->chips_per_node);

	return gres_list;
}

static int _is_tpu_gres(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;

	return !xstrcasecmp(gres_slurmd_conf->name, "tpu");
}

extern int init(void)
{
	int rc;

	debug("%s: %s loaded", __func__, plugin_name);

	/*
	 * A missing tpu.yaml is not fatal: without it no device id maps to a
	 * family, so no TPUs are detected and the node simply has none. That
	 * keeps a cluster with the plugin enabled but no accelerators working.
	 */
	if ((rc = tpu_conf_load(&tpu_conf)) != SLURM_SUCCESS)
		debug("%s: no usable %s (%s); TPU autodetection is disabled",
		      plugin_type, TPU_CONF_FILE, slurm_strerror(rc));

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("%s: unloading %s", __func__, plugin_name);
	FREE_NULL_LIST(gres_devices);
	tpu_conf_destroy(tpu_conf);
	tpu_conf = NULL;
}

extern int gres_p_node_config_load(list_t *gres_conf_list,
				   node_config_load_t *node_config)
{
	int rc;
	list_t *gres_list_system;

	/* Assume this state is caused by an scontrol reconfigure */
	if (gres_devices) {
		debug("%s: Resetting gres_devices", plugin_name);
		FREE_NULL_LIST(gres_devices);
	}

	if ((gres_list_system = _get_system_tpu_list(node_config))) {
		debug("%s: replacing configured TPU GRES with detected TPUs",
		      plugin_name);
		/*
		 * Autodetection is authoritative for TPUs. gres.conf cannot
		 * name a VFIO node usefully, because IOMMU group numbers are
		 * assigned at boot and are not stable across reboots, so any
		 * hand-written File= would be wrong sooner or later. Drop
		 * any configured TPU records while preserving other GRES types
		 * on the node, and transfer the detected TPU chips in.
		 */
		if (gres_conf_list)
			(void) list_delete_all(gres_conf_list, _is_tpu_gres,
					       NULL);
		list_transfer(gres_conf_list, gres_list_system);
		FREE_NULL_LIST(gres_list_system);

		if (slurm_conf.debug_flags & DEBUG_FLAG_GRES)
			print_gres_list(gres_conf_list, LOG_LEVEL_VERBOSE);
	}

	rc = gres_node_config_load(gres_conf_list, node_config, &gres_devices);

	if (rc != SLURM_SUCCESS)
		fatal("%s failed to load configuration", plugin_name);

	return rc;
}

static void _set_env(common_gres_env_t *gres_env)
{
	char *slurm_env_var;

	if (gres_env->is_job)
		slurm_env_var = "SLURM_JOB_TPUS";
	else
		slurm_env_var = "SLURM_STEP_TPUS";

	/*
	 * Unlike gres/nic, which names devices after the file (mlx4_0), a TPU
	 * chip has no meaningful name of its own. Its VFIO file is named for
	 * an IOMMU group number assigned at boot, so dev_num would export an
	 * unstable value that means nothing to the task. Leave use_dev_num
	 * false so the env carries the chip's relative index on the node,
	 * matching both gres/gpu and the order libtpu enumerates chips in.
	 */
	gres_env->prefix = "";

	common_gres_set_env(gres_env);

	/*
	 * Unset rather than leave stale when nothing is allocated, so a step
	 * requesting --gres=none inside a TPU allocation does not inherit the
	 * job's device list.
	 */
	if (gres_env->global_list) {
		env_array_overwrite(gres_env->env_ptr, slurm_env_var,
				    gres_env->global_list);
		xfree(gres_env->global_list);
	} else {
		unsetenvp(*gres_env->env_ptr, slurm_env_var);
	}

	/* No TPU environment variable is built from the per-device lists */
	xfree(gres_env->local_list);
	xfree(gres_env->local_list_uuid);
}

extern void gres_p_job_set_env(char ***job_env_ptr, bitstr_t *gres_bit_alloc,
			       uint64_t gres_cnt, uint64_t *gres_per_bit,
			       gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = job_env_ptr,
		.flags = flags,
		.gres_cnt = gres_cnt,
		.gres_devices = gres_devices,
		.gres_per_bit = gres_per_bit,
		.is_job = true,
	};

	_set_env(&gres_env);
}

extern void gres_p_step_set_env(char ***step_env_ptr, bitstr_t *gres_bit_alloc,
				uint64_t gres_cnt, uint64_t *gres_per_bit,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = step_env_ptr,
		.flags = flags,
		.gres_cnt = gres_cnt,
		.gres_devices = gres_devices,
		.gres_per_bit = gres_per_bit,
	};

	_set_env(&gres_env);
}

extern void gres_p_task_set_env(char ***task_env_ptr, bitstr_t *gres_bit_alloc,
				uint64_t gres_cnt, uint64_t *gres_per_bit,
				bitstr_t *usable_gres,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = task_env_ptr,
		.flags = flags,
		.gres_cnt = gres_cnt,
		.gres_devices = gres_devices,
		.gres_per_bit = gres_per_bit,
		.is_task = true,
		.usable_gres = usable_gres,
	};

	_set_env(&gres_env);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_p_send_stepd(buf_t *buffer)
{
	gres_send_stepd(buffer, gres_devices);
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_p_recv_stepd(buf_t *buffer)
{
	gres_recv_stepd(buffer, &gres_devices);
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern list_t *gres_p_get_devices(void)
{
	return gres_devices;
}

extern list_t *gres_p_get_dmem_devices(void)
{
	return NULL;
}

extern void gres_p_step_hardware_init(bitstr_t *usable_gres, char *settings)
{
	return;
}

extern void gres_p_step_hardware_fini(void)
{
	return;
}

extern gres_prep_t *gres_p_prep_build_env(gres_job_state_t *gres_js)
{
	return NULL;
}

extern void gres_p_prep_set_env(char ***prep_env_ptr, gres_prep_t *gres_prep,
				int node_inx)
{
	return;
}
