/*****************************************************************************\
 *  gres_shard.c - Support SHARD as a generic resources.
 *  Sharding is a mechanism to share GPUs generically.
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Danny Auble
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
#include <glob.h>
#include <inttypes.h>
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
#include "src/interfaces/cgroup.h"
#include "src/interfaces/gpu.h"
#include "src/interfaces/gres.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "../common/gres_common.h"
#include "../common/gres_c_s.h"

/* Required Slurm plugin symbols: */
const char plugin_name[] = "Gres SHARD plugin";
const char plugin_type[] = "gres/shard";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static list_t *gres_devices = NULL;
static uint32_t	node_flags		= 0;

typedef struct shard_dev_info {
	uint64_t count;
	int id;
} shard_dev_info_t;

#define DMEM_REGION_OFF "off"

/* Defines used to parse a PCI string */
#define BDF_DOMAIN_DIGITS_MIN 4
#define BDF_DOMAIN_DIGITS_MAX 8
#define BDF_BUS_DIGITS 2
#define BDF_DEV_DIGITS 2
#define BDF_FUNC_DIGITS 1
#define BDF_HEX_CHARS "0123456789abcdefABCDEF"

typedef struct {
	uint16_t bus;
	uint16_t device;
	uint16_t domain;
	uint16_t function;
} pci_bdf_t;

typedef struct {
	char *file; /* device file to look for */
	char *sharing_name; /* name of the sharing gres ("gpu") */
} find_conf_gpu_t;

typedef struct {
	list_t *gres_conf_list; /* gres.conf records */
	list_t *region_list; /* regions parsed from dmem.capacity */
} dmem_discover_t;

typedef struct {
	cgroup_limits_t *found; /* last matching region */
	int matches;
	pci_bdf_t *pci;
} match_region_t;

/*
 * Parse a PCI address string of the form domain:bus:device[.function].
 * e.g. sysfs "0000:01:00.0" or "00000000:01:00.2".
 * IN str - string to parse
 * OUT bdf - parsed address, untouched on error
 * RET true if str is a PCI address
 */
static bool _pci_bdf_parse(const char *str, pci_bdf_t *bdf)
{
	const char *pos = str;
	uint16_t bus = 0, device = 0, domain = 0, function = 0;
	size_t span = 0;

	/* Parse the domain */
	span = strspn(pos, BDF_HEX_CHARS);
	if ((span < BDF_DOMAIN_DIGITS_MIN) || (span > BDF_DOMAIN_DIGITS_MAX) ||
	    (pos[span] != ':'))
		return false;
	domain = strtoul(pos, NULL, 16);
	pos += (span + 1);

	/* Parse the bus address */
	span = strspn(pos, BDF_HEX_CHARS);
	if ((span != BDF_BUS_DIGITS) || (pos[BDF_BUS_DIGITS] != ':'))
		return false;
	bus = strtoul(pos, NULL, 16);
	pos += (span + 1);

	/* Parse the device */
	span = strspn(pos, BDF_HEX_CHARS);
	if (span != BDF_DEV_DIGITS)
		return false;
	device = strtoul(pos, NULL, 16);
	pos += span;

	/* Parse the function, only if a '.' has been found */
	if (*pos == '.') {
		pos++;
		if (strspn(pos, BDF_HEX_CHARS) != BDF_FUNC_DIGITS)
			return false;
		function = strtoul(pos, NULL, 16);
		pos++;
	}

	/* String continues and should not, bail */
	if (*pos)
		return false;

	*bdf = (pci_bdf_t) {
		.bus = bus,
		.device = device,
		.domain = domain,
		.function = function,
	};

	return true;
}

/*
 * Test whether any '/'-separated component of path is a PCI address for
 * the same device function as bdf (e.g. "nvidia/0000:01:00/vidmem" or
 * "drm/0000:79:00.0/vram").
 * IN path - string to scan
 * IN bdf - address to look for
 * RET true if some component parses to an address equal to bdf
 */
static bool _pci_bdf_match_path(const char *path, const pci_bdf_t *bdf)
{
	const char *start = path;

	if (!path || !bdf)
		return false;

	while (start) {
		const char *end = xstrchr(start, '/');
		size_t len = end ? (size_t) (end - start) : strlen(start);
		char token[32] = { 0 };
		pci_bdf_t parsed = { 0 };

		if (len && (len < sizeof(token))) {
			memcpy(token, start, len);
			if (_pci_bdf_parse(token, &parsed) &&
			    (parsed.bus == bdf->bus) &&
			    (parsed.device == bdf->device) &&
			    (parsed.domain == bdf->domain) &&
			    (parsed.function == bdf->function))
				return true;
		}
		start = end ? (end + 1) : NULL;
	}

	return false;
}

/*
 * Extract the first '/'-separated component of path that parses as a PCI
 * address (e.g. "0000:01:00" out of "nvidia/0000:01:00/vidmem").
 * IN path - string to scan
 * OUT bdf - parsed address, untouched when none is found
 * RET true if some component parses to a PCI address
 */
static bool _pci_bdf_from_path(const char *path, pci_bdf_t *bdf)
{
	const char *start = path;

	if (!path || !bdf)
		return false;

	while (start) {
		const char *end = xstrchr(start, '/');
		size_t len = end ? (size_t) (end - start) : strlen(start);
		char token[32] = { 0 };

		if (len && (len < sizeof(token))) {
			memcpy(token, start, len);
			if (_pci_bdf_parse(token, bdf))
				return true;
		}
		start = end ? (end + 1) : NULL;
	}

	return false;
}

/*
 * The proprietary NVIDIA driver does not register its native device nodes
 * in /sys/dev, but exposes the PCI address and the device minor of every GPU in
 * /proc/driver/nvidia/gpus/<pci_addr>/information.
 *
 * IN gres_device - device to resolve
 * OUT bdf - parsed PCI address
 * RET true if a GPU with the device minor was found
 */
static bool _pci_bdf_from_nvidia_procfs(gres_device_t *gres_device,
					pci_bdf_t *bdf)
{
	glob_t globbuf = { 0 };
	bool found = false;

	if (glob("/proc/driver/nvidia/gpus/*/information", 0, NULL, &globbuf))
		return false;

	for (size_t i = 0; ((i < globbuf.gl_pathc) && !found); i++) {
		char line[256];
		FILE *fp = fopen(globbuf.gl_pathv[i], "r");

		if (!fp)
			continue;
		while (fgets(line, sizeof(line), fp)) {
			long minor = -1;
			/*
			 * Just use the minor to identify the NV gpu. According
			 * to NVML docs	the minor is the number in
			 * /dev/nvidia[0-9] (see nvmlDeviceGetMinorNumber()).
			 */
			if (xstrncmp(line, "Device Minor:", 13))
				continue;
			minor = strtol((line + 13), NULL, 10);
			if (minor == (long) gres_device->dev_desc.minor) {
				char *dir = xdirname(globbuf.gl_pathv[i]);

				/* Parse the <pci_addr> path component */
				found = _pci_bdf_parse(xbasename(dir), bdf);
				xfree(dir);
			}
			break;
		}
		fclose(fp);
	}
	globfree(&globbuf);

	return found;
}

/*
 * Resolve the PCI address of a device file through sysfs.
 *
 * IN gres_device - device to resolve
 * OUT bdf - parsed PCI address
 * RET true if the device maps to a PCI device
 */
static bool _pci_bdf_from_sysfs(gres_device_t *gres_device, pci_bdf_t *bdf)
{
	char *link = NULL, *resolved = NULL;
	bool found = false;

	if (gres_device->dev_desc.type == DEV_TYPE_NONE)
		return false;

	/*
	 * A driver should register_chrdev() and then create a device with
	 * device_create(), this will leave a device in /sys/dev.
	 */
	link = xstrdup_printf("/sys/dev/%s/%u:%u/device",
			      (gres_device->dev_desc.type == DEV_TYPE_BLOCK) ?
				      "block" :
				      "char",
			      gres_device->dev_desc.major,
			      gres_device->dev_desc.minor);
	resolved = realpath(link, NULL);
	if (resolved)
		found = _pci_bdf_parse(xbasename(resolved), bdf);

	free(resolved);
	xfree(link);

	return found;
}

static int _find_conf_gpu(void *x, void *arg)
{
	gres_slurmd_conf_t *gres_slurmd_conf = x;
	find_conf_gpu_t *find = arg;

	if (!xstrcmp(gres_slurmd_conf->name, find->sharing_name) &&
	    !xstrcmp(gres_slurmd_conf->file, find->file))
		return 1;
	return 0;
}

/*
 * Find the PCI address of a shard sharing device and store its canonical
 * form in gres_device->pci_addr, left NULL when it cannot be resolved.
 * IN/OUT gres_device - device being discovered
 * IN gres_slurmd_conf - matching sharing (gpu) conf record, or NULL
 */
static void _find_dev_pci(gres_device_t *gres_device,
			  gres_slurmd_conf_t *gres_slurmd_conf)
{
	pci_bdf_t pci = { 0 };
	bool pci_valid = false;

	/* An explicit DmemRegion= carries the address and is trusted first */
	if (gres_slurmd_conf && gres_slurmd_conf->dmem_region)
		pci_valid =
			_pci_bdf_from_path(gres_slurmd_conf->dmem_region, &pci);
	/* Then the PCI address that AutoDetect stored */
	if (!pci_valid && gres_slurmd_conf && gres_slurmd_conf->pci_addr)
		pci_valid = _pci_bdf_parse(gres_slurmd_conf->pci_addr, &pci);
	if (!pci_valid)
		pci_valid = _pci_bdf_from_sysfs(gres_device, &pci);

	/*
	 * The nvidia driver seems to not call device_create(),
	 * so try to resolve it separately.
	 */
	if (!pci_valid)
		pci_valid = _pci_bdf_from_nvidia_procfs(gres_device, &pci);
	if (pci_valid)
		gres_device->pci_addr =
			xstrdup_printf("%04x:%02x:%02x.%x", pci.domain, pci.bus,
				       pci.device, pci.function);
}

static int _find_shared_id(void *x, void *arg)
{
	shared_dev_info_t *shared = x;
	int *dev_num = arg;

	return (shared->id == *dev_num);
}

static int _find_region_name(void *x, void *arg)
{
	cgroup_limits_t *region = x;
	char *name = arg;

	return !xstrcmp(region->dmem_region, name);
}

/*
 * Bind a dmem region to a device: record its name and capacity and make
 * the device usable.
 */
static void _bind_region(gres_dmem_dev_t *dmem, cgroup_limits_t *region)
{
	dmem->capacity = region->limit_in_bytes;
	dmem->region = xstrdup(region->dmem_region);
	dmem->state = GRES_DMEM_USABLE;
}

static int _foreach_match_region(void *x, void *arg)
{
	cgroup_limits_t *region = x;
	match_region_t *match = arg;

	if (_pci_bdf_match_path(region->dmem_region, match->pci)) {
		match->matches++;
		match->found = region;
	}

	return 0;
}

/*
 * Find the dmem region belonging to a device by matching the device's PCI
 * address against every region name.
 * IN region_list - regions parsed from dmem.capacity
 * IN/OUT gres_device - device to match; on a unique match its region,
 *	capacity and state are set, several matches set
 *	GRES_DMEM_AMBIGUOUS. A device without a resolved PCI address is
 *	left untouched.
 */
static void _match_region_by_pci(list_t *region_list,
				 gres_device_t *gres_device)
{
	pci_bdf_t pci = { 0 };
	match_region_t match = { .pci = &pci };

	if (!gres_device->pci_addr || !region_list)
		return;
	if (!_pci_bdf_parse(gres_device->pci_addr, &pci))
		return;

	(void) list_for_each(region_list, _foreach_match_region, &match);

	if (match.matches == 1) {
		_bind_region(gres_device->dmem, match.found);
	} else if (match.matches > 1) {
		/*
		 * Enforcing several regions of one device (e.g. multi-tile
		 * GPUs) is a possible future enhancement.
		 */
		gres_device->dmem->state = GRES_DMEM_AMBIGUOUS;
	}
}

/*
 * Check for regions that appear twice and classify them.
 *
 * A device with DmemRegion=off or with no DmemRegion, is ignored.
 *
 * - Two automatically matched devices point to the same PCI address
 *   (e.g. /dev/nvidia0 + /dev/dri/renderD129), both are marked
 *   GRES_DMEM_SHARED.
 * - One device is explicitly configured with DmemRegion= and another
 *   one automatically matched the same region. The explicit one takes
 *   precedence, the other is marked GRES_DMEM_SHARED.
 * - Two devices explicitly configured with the same DmemRegion= are a
 *   fatal configuration error.
 */
/*
 * Classify one device against every other device of the node.
 * IN x - device to compare against
 * IN arg - device being classified
 */
static int _foreach_dup_region(void *x, void *arg)
{
	gres_device_t *dev2 = x;
	gres_device_t *dev = arg;

	if ((dev2 == dev) ||
	    (dev2->dmem->state == GRES_DMEM_EXCLUDED) ||
	    !dev2->dmem->region)
		return 0;
	if (xstrcmp(dev->dmem->region, dev2->dmem->region))
		return 0;
	if (dev->dmem->from_conf && dev2->dmem->from_conf)
		fatal("gres.conf: dmem region %s bound with DmemRegion= to both %s and %s",
		      dev->dmem->region, dev->path, dev2->path);
	if (!dev->dmem->from_conf)
		dev->dmem->state = GRES_DMEM_SHARED;

	return 0;
}

static int _foreach_check_dup_regions(void *x, void *arg)
{
	gres_device_t *dev = x;

	if (dev->dmem->state == GRES_DMEM_USABLE)
		(void) list_for_each(gres_devices, _foreach_dup_region, dev);

	return 0;
}

static void _dmem_check_dup_regions(void)
{
	(void) list_for_each(gres_devices, _foreach_check_dup_regions, NULL);
}

/*
 * Divide the dmem region capacity across the device configured shards.
 *
 * For logging:
 * - "region=" is printed only if a region exists for the device.
 * - "dmem=" is printed only if a region exists: "enforced" or "disabled"
 *   when usable (per ConstrainDeviceMemory=yes), or "off" when excluded
 *   with DmemRegion=off.
 * - A device excluded with DmemRegion=off shows "dmem=off" when it has a
 *   region, and neither field otherwise, without any warning.
 * - A device with no usable region shows neither field, plus one warning
 *   only under ConstrainDeviceMemory=yes naming the cause.
 */
static int _dmem_set_dev_slices(void *x, void *arg)
{
	gres_device_t *dev = x;
	gres_dmem_dev_t *dmem = dev->dmem;

	/*
	 * A device of no shards has no slice to enforce, so it is reported the
	 * way the other unenforceable devices are: without a dmem= field, so
	 * that field never claims a limit that no cgroup ever holds.
	 */
	if ((dmem->state == GRES_DMEM_USABLE) && !dmem->shards) {
		info("shard: %s shards=0 region=%s capacity=%"PRIu64"MiB",
		     dev->path, dmem->region,
		     (dmem->capacity / (1024 * 1024)));
		if (slurm_cgroup_conf.constrain_device_memory)
			warning("shard: %s has no shards, its device memory will not be enforced.",
				dev->path);
		return 0;
	}

	if (dmem->state == GRES_DMEM_USABLE) {
		dmem->slice = (dmem->capacity / dmem->shards);
		info("shard: %s shards=%"PRIu64" region=%s capacity=%"PRIu64"MiB slice=%"PRIu64"MiB dmem=%s",
		     dev->path, dmem->shards, dmem->region,
		     (dmem->capacity / (1024 * 1024)),
		     (dmem->slice / (1024 * 1024)),
		     slurm_cgroup_conf.constrain_device_memory ? "enforced" :
								 "disabled");
		return 0;
	}

	if ((dmem->state == GRES_DMEM_EXCLUDED) && dmem->region) {
		info("shard: %s shards=%"PRIu64" region=%s dmem=off",
		     dev->path, dmem->shards, dmem->region);
		return 0;
	}

	info("shard: %s shards=%"PRIu64, dev->path, dmem->shards);

	if (!slurm_cgroup_conf.constrain_device_memory ||
	    (dmem->state == GRES_DMEM_EXCLUDED))
		return 0;

	if (dmem->state == GRES_DMEM_AMBIGUOUS) {
		warning("shard: %s matches several dmem regions, its device memory will not be enforced. Set DmemRegion= on its gres.conf gpu record to pick one.", dev->path);
	} else if (dmem->state == GRES_DMEM_SHARED) {
		warning("shard: %s shares dmem region %s with another device (e.g. MIG instances of one GPU), its device memory will not be enforced.", dev->path, dmem->region);
	} else {
		warning("shard: %s has no dmem region, its device memory will not be enforced (driver without dmem cgroup support, or kernel older than 6.14).", dev->path);
	}

	return 0;
}

static int _foreach_dmem_discover(void *x, void *arg)
{
	gres_device_t *gres_device = x;
	dmem_discover_t *discover = arg;
	gres_dmem_dev_t *dmem = xmalloc(sizeof(*dmem));
	find_conf_gpu_t find = {
		.file = gres_device->path,
		.sharing_name = "gpu",
	};
	gres_slurmd_conf_t *gres_slurmd_conf = NULL;
	shared_dev_info_t *shared = NULL;

	if (gres_device->dmem) {
		xfree(gres_device->dmem->region);
		xfree(gres_device->dmem);
	}
	xfree(gres_device->pci_addr);
	gres_device->dmem = dmem;

	if (discover->gres_conf_list)
		gres_slurmd_conf = list_find_first(discover->gres_conf_list,
						   _find_conf_gpu, &find);

	_find_dev_pci(gres_device, gres_slurmd_conf);

	if (shared_info &&
	    (shared = list_find_first(shared_info, _find_shared_id,
				      &gres_device->dev_num)))
		dmem->shards = shared->count;

	if (gres_slurmd_conf && gres_slurmd_conf->dmem_region &&
	    !xstrcasecmp(gres_slurmd_conf->dmem_region, DMEM_REGION_OFF)) {
		/*
		 * Deliberate exclusion. Still detect the region so
		 * the startup log can show what is being excluded.
		 */
		_match_region_by_pci(discover->region_list, gres_device);
		dmem->state = GRES_DMEM_EXCLUDED;
	} else if (gres_slurmd_conf && gres_slurmd_conf->dmem_region) {
		cgroup_limits_t *region = NULL;

		if (!discover->region_list ||
		    !(region = list_find_first(discover->region_list,
					       _find_region_name,
					       gres_slurmd_conf->dmem_region))) {
			fatal("gres.conf: DmemRegion=%s for %s does not exist.",
			      gres_slurmd_conf->dmem_region,
			      gres_device->path);
		}
		_bind_region(dmem, region);
		dmem->from_conf = true;
	} else {
		_match_region_by_pci(discover->region_list, gres_device);
	}

	return 0;
}

/*
 * Discover the dmem cgroup state of every shard sharing device and compute
 * its per-shard device memory slice.
 *
 * IN gres_conf_list - merged gres.conf records, consulted for the PCI
 *	address that AutoDetect stored and for DmemRegion= overrides on
 *	the sharing (gpu) records
 */
static void _dmem_discover(list_t *gres_conf_list)
{
	dmem_discover_t discover = { .gres_conf_list = gres_conf_list };

	if (!gres_devices)
		return;

	if (!(discover.region_list = cgroup_g_get_dmem_regions()) ||
	    !list_count(discover.region_list))
		log_flag(GRES, "shard: no dmem regions available");

	(void) list_for_each(gres_devices, _foreach_dmem_discover, &discover);

	_dmem_check_dup_regions();

	(void) list_for_each(gres_devices, _dmem_set_dev_slices, NULL);

	FREE_NULL_LIST(discover.region_list);
}

extern int init(void)
{
	debug("loaded");

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloading");
	FREE_NULL_LIST(gres_devices);
	gres_c_s_fini();
}

/*
 * We could load gres state or validate it using various mechanisms here.
 * This only validates that the configuration was specified in gres.conf.
 * In the general case, no code would need to be changed.
 */
extern int gres_p_node_config_load(list_t *gres_conf_list,
				   node_config_load_t *config)
{
	int rc = gres_c_s_init_share_devices(
		gres_conf_list, &gres_devices, config, "gpu");

	if (rc != SLURM_SUCCESS)
		return rc;

	/*
	 * See what envs the gres_slurmd_conf records want to set (if one
	 * record wants an env, assume every record on this node wants that
	 * env). Check node_flags when setting envs later in stepd.
	 */
	node_flags = 0;
	(void) list_for_each(gres_conf_list,
			     gres_common_set_env_types_on_node_flags,
			     &node_flags);

	/* We don't do any discover in slurmctld */
	if (config->in_slurmd)
		_dmem_discover(gres_conf_list);

	return rc;
}

static void _set_shard_env(common_gres_env_t *gres_env)
{
	if (gres_env->gres_cnt) {
		char *gpus_on_node = xstrdup_printf("%"PRIu64,
						    gres_env->gres_cnt);
		env_array_overwrite(gres_env->env_ptr, "SLURM_SHARDS_ON_NODE",
				    gpus_on_node);
		xfree(gpus_on_node);
	} else if (!(gres_env->flags & GRES_INTERNAL_FLAG_PROTECT_ENV)) {
		unsetenvp(*(gres_env->env_ptr), "SLURM_SHARDS_ON_NODE");
	}
}

/*
 * Set environment variables as appropriate for a job (i.e. all tasks) based
 * upon the job's GRES state.
 */
extern void gres_p_job_set_env(char ***job_env_ptr,
			       bitstr_t *gres_bit_alloc,
			       uint64_t gres_per_node,
			       gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = job_env_ptr,
		.flags = flags,
		.gres_cnt = gres_per_node,
		.gres_conf_flags = node_flags,
		.gres_devices = gres_devices,
		.is_job = true,
	};

	gres_common_gpu_set_env(&gres_env);
	_set_shard_env(&gres_env);
}

/*
 * Set environment variables as appropriate for a step (i.e. all tasks) based
 * upon the job step's GRES state.
 */
extern void gres_p_step_set_env(char ***step_env_ptr,
				bitstr_t *gres_bit_alloc,
				uint64_t gres_per_node,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = step_env_ptr,
		.flags = flags,
		.gres_cnt = gres_per_node,
		.gres_conf_flags = node_flags,
		.gres_devices = gres_devices,
	};

	gres_common_gpu_set_env(&gres_env);
	_set_shard_env(&gres_env);
}

/*
 * Reset environment variables as appropriate for a job (i.e. this one task)
 * based upon the job step's GRES state and assigned CPUs.
 */
extern void gres_p_task_set_env(char ***task_env_ptr,
				bitstr_t *gres_bit_alloc,
				uint64_t gres_cnt,
				bitstr_t *usable_gres,
				gres_internal_flags_t flags)
{
	common_gres_env_t gres_env = {
		.bit_alloc = gres_bit_alloc,
		.env_ptr = task_env_ptr,
		.flags = flags,
		.gres_cnt = gres_cnt,
		.gres_conf_flags = node_flags,
		.gres_devices = gres_devices,
		.is_task = true,
		.usable_gres = usable_gres,
	};

	gres_common_gpu_set_env(&gres_env);
	_set_shard_env(&gres_env);
}

/* Send GRES information to slurmstepd on the specified file descriptor */
extern void gres_p_send_stepd(buf_t *buffer)
{
	gres_send_stepd(buffer, gres_devices);

	pack32(node_flags, buffer);

	gres_c_s_send_stepd(buffer);

	return;
}

/* Receive GRES information from slurmd on the specified file descriptor */
extern void gres_p_recv_stepd(buf_t *buffer)
{
	gres_recv_stepd(buffer, &gres_devices);

	safe_unpack32(&node_flags, buffer);

	gres_c_s_recv_stepd(buffer);

	return;

unpack_error:
	error("%s: failed", __func__);
}

/*
 * Return a list of devices of this type. The list elements are of type
 * "gres_device_t" and the list should be freed using FREE_NULL_LIST().
 */
extern list_t *gres_p_get_devices(void)
{
	return gres_devices;
}

/*
 * Return the list of sharing devices, each carrying its dmem cgroup state
 * in the dmem member. The list elements are of type "gres_device_t" and
 * remain owned by this plugin.
 */
extern list_t *gres_p_get_dmem_devices(void)
{
	return gres_devices;
}

extern void gres_p_step_hardware_init(bitstr_t *usable_gres, char *settings)
{
	gpu_g_step_hardware_init(usable_gres, settings);
}

extern void gres_p_step_hardware_fini(void)
{
	gpu_g_step_hardware_fini();
}

/*
 * Build record used to set environment variables as appropriate for a job's
 * prolog or epilog based GRES allocated to the job.
 */
extern gres_prep_t *gres_p_prep_build_env(gres_job_state_t *gres_js)
{
	int i;
	gres_prep_t *gres_prep;

	gres_prep = xmalloc(sizeof(gres_prep_t));
	gres_prep->node_cnt = gres_js->node_cnt;
	gres_prep->gres_bit_alloc = xcalloc(gres_prep->node_cnt,
					    sizeof(bitstr_t *));
	gres_prep->gres_cnt_node_alloc = xcalloc(gres_prep->node_cnt,
						 sizeof(uint64_t));
	for (i = 0; i < gres_prep->node_cnt; i++) {
		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[i]) {
			gres_prep->gres_bit_alloc[i] =
				bit_copy(gres_js->gres_bit_alloc[i]);
		}
		if (gres_js->gres_bit_alloc &&
		    gres_js->gres_bit_alloc[i]) {
			gres_prep->gres_cnt_node_alloc[i] =
				gres_js->gres_cnt_node_alloc[i];
		}
	}

	return gres_prep;
}

/*
 * Set environment variables as appropriate for a job's prolog or epilog based
 * GRES allocated to the job.
 */
extern void gres_p_prep_set_env(char ***prep_env_ptr,
				gres_prep_t *gres_prep, int node_inx)
{
	(void) gres_common_prep_set_env(prep_env_ptr, gres_prep,
					node_inx, node_flags, gres_devices);
}
