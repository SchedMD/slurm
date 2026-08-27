/*****************************************************************************\
 *  gpu_amd.c - Support for AMD GPUs through the kernel sysfs interface.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "../common/gpu_common.h"

/*
 * gpu/amd: reads GPU information from the amdgpu kernel driver via sysfs, with
 * no vendor library.
 */

#define GPU_AMD_DEV_DIR "/dev/dri"
#define GPU_AMD_DRM_DIR "/sys/class/drm"
#define GPU_AMD_PCI_NAME_MAX 16 /* at most %04x:%02x:%02x.%d */
#define GPU_AMD_PCI_SLOT_NAME_SCAN \
	"PCI_SLOT_NAME=%" XSTRINGIFY(GPU_AMD_PCI_NAME_MAX) "s"
#define GPU_AMD_SYSFS_BUF 256

/* The slurmd configuration, defined for real by slurmd and slurmstepd. */
#if defined(__APPLE__)
extern slurmd_conf_t *conf __attribute__((weak_import));
#else
slurmd_conf_t *conf = NULL;
#endif

/* A card kept by discovery. */
typedef struct {
	int card; /* DRM card number */
	char *name; /* lowercased GPU model, owned and freed per element */
	char *pci_address; /* PCI slot name, owned and freed per element */
	int render_minor; /* DRM render node minor */
} amd_card_t;

/* Required Slurm plugin symbols: */
const char plugin_name[] = "GPU AMD plugin";
const char plugin_type[] = "gpu/amd";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

/*
 * The cards discovery emits a GRES for, in GRES index order, cached for the
 * process lifetime. Freed in fini().
 */
static amd_card_t *amdgpu_cards = NULL;
static int amdgpu_ncards = 0;

/* Whether this load logged its discovery messages. */
static bool discovery_logged = false;

/*
 * Build the sysfs path GPU_AMD_DRM_DIR/card<num>/device/<attr>.
 *
 * card_num (IN) The DRM card number.
 * attr (IN) The attribute name under the card's device directory.
 *
 * Returns an xmalloc'd path (caller frees).
 */
static char *_card_device_path(int card_num, const char *attr)
{
	xassert(attr);

	return xstrdup_printf("%s/card%d/device/%s", GPU_AMD_DRM_DIR, card_num,
			      attr);
}

/*
 * Check whether a DRM card is bound to the amdgpu kernel driver.
 *
 * card_num (IN) The DRM card number.
 *
 * Returns true if the card is an amdgpu device, false otherwise.
 */
static bool _is_amdgpu_card(int card_num)
{
	char *path = NULL;
	char link[PATH_MAX] = { 0 };
	ssize_t len = 0;

	path = _card_device_path(card_num, "driver");
	len = readlink(path, link, sizeof(link) - 1);
	xfree(path);
	/*
	 * readlink() neither NUL-terminates nor signals truncation, so a full
	 * buffer may hold a truncated target. Fail rather than compare it.
	 */
	if ((len < 0) || (len >= (sizeof(link) - 1)))
		return false;
	link[len] = '\0';

	return !xstrcmp(xbasename(link), "amdgpu");
}

/*
 * Read the first line of a sysfs file, stripping the line terminator.
 *
 * path (IN) Full path of the sysfs file to read.
 *
 * Returns the xmalloc'd line (caller frees), NULL when the file cannot be read
 * or the line is empty.
 */
static char *_read_sysfs_line(const char *path)
{
	FILE *fp = NULL;
	char *line = NULL, *value = NULL;
	size_t size = 0;

	xassert(path);

	if (!(fp = fopen(path, "r")))
		return NULL;

	if (getline(&line, &size, fp) > 0) {
		line[strcspn(line, "\n")] = '\0';
		if (line[0])
			value = xstrdup(line);
	}
	fclose(fp);
	free(line);

	return value;
}

/*
 * Collect the amdgpu card numbers on the node, ordered ascending so the cache
 * fill walks them deterministically.
 *
 * cards (OUT) An xmalloc'd array of the card numbers (caller frees), NULL when
 *	none was found.
 *
 * Returns the number of cards found.
 */
static int _list_amdgpu_cards(int **cards)
{
	DIR *dr = NULL;
	struct dirent *de = NULL;
	int found = 0;
	int *found_cards = NULL;

	xassert(cards);

	*cards = NULL;
	if (!(dr = opendir(GPU_AMD_DRM_DIR)))
		return 0;

	while ((de = readdir(dr))) {
		int card_num = 0;
		char tail = '\0';

		/*
		 * Connector entries (card0-DP-1) share the prefix, so any
		 * character after the number rejects the entry.
		 */
		if (sscanf(de->d_name, "card%d%c", &card_num, &tail) != 1)
			continue;
		if (!_is_amdgpu_card(card_num))
			continue;
		xrecalloc(found_cards, (found + 1), sizeof(*found_cards));
		found_cards[found++] = card_num;
	}
	closedir(dr);

	if (found)
		qsort(found_cards, found, sizeof(*found_cards),
		      slurm_sort_int_list_asc);

	*cards = found_cards;
	return found;
}

/*
 * Get a card's DRM render minor, naming its /dev/dri/renderD<minor> file.
 *
 * card_num (IN) The DRM card number.
 *
 * Returns the renderD minor number, -1 when the card has no render node.
 */
static int _get_render_minor(int card_num)
{
	char *dir_path = NULL;
	DIR *dr = NULL;
	struct dirent *de = NULL;
	int minor = -1;

	dir_path = _card_device_path(card_num, "drm");
	dr = opendir(dir_path);
	xfree(dir_path);
	if (!dr)
		return -1;

	while ((de = readdir(dr))) {
		if (sscanf(de->d_name, "renderD%d", &minor) == 1)
			break;
	}
	closedir(dr);

	return minor;
}

/*
 * Read a card's PCI address from the PCI_SLOT_NAME line of its device uevent.
 *
 * card_num (IN) The DRM card number.
 *
 * Returns the xmalloc'd address (caller frees), NULL when none was read.
 */
static char *_read_pci_address(int card_num)
{
	char *path = NULL, *pci_address = NULL;
	FILE *fp = NULL;
	char line[GPU_AMD_SYSFS_BUF] = { 0 };
	char addr[GPU_AMD_PCI_NAME_MAX + 1] = { 0 };

	path = _card_device_path(card_num, "uevent");
	fp = fopen(path, "r");
	xfree(path);
	if (!fp)
		return NULL;

	while (!pci_address && fgets(line, sizeof(line), fp)) {
		if (sscanf(line, GPU_AMD_PCI_SLOT_NAME_SCAN, addr) == 1)
			pci_address = xstrdup(addr);
	}
	fclose(fp);

	return pci_address;
}

/*
 * Read one line of a card's device/<attr> sysfs attribute.
 *
 * card_num (IN) The DRM card number.
 * attr (IN) The attribute name under the card's device directory.
 *
 * Returns the xmalloc'd line (caller frees), NULL when the attribute cannot be
 * read or is empty.
 */
static char *_read_card_attr(int card_num, const char *attr)
{
	char *path = NULL, *line = NULL;

	xassert(attr);

	path = _card_device_path(card_num, attr);
	line = _read_sysfs_line(path);
	xfree(path);

	return line;
}

/*
 * Read a card's model name, lower-cased and underscorified to serve as a GRES
 * type name. product_name exists only where the driver read a board FRU
 * EEPROM, so fall back to the PCI device ID (device/device).
 *
 * card_num (IN) The DRM card number.
 *
 * Returns the xmalloc'd model name (caller frees), NULL when none is read.
 */
static char *_read_card_model(int card_num)
{
	char *name = NULL;

	if (!(name = _read_card_attr(card_num, "product_name")))
		name = _read_card_attr(card_num, "device");
	if (name)
		gpu_common_underscorify_tolower(name);

	return name;
}

/*
 * Read a card's CPU affinity from sysfs (device/local_cpulist) and convert it
 * from machine format to Slurm's abstract format.
 *
 * xcpuinfo_mac_to_abs (IN) Converts a machine CPU range to abstract format.
 * card_num (IN) The DRM card number.
 * enabled_cpus (IN) The CPUs Slurm schedules, masking the list, or NULL to
 *	keep every listed CPU.
 * cores (OUT) The abstract core range (caller frees), NULL when the affinity
 *	could not be resolved.
 *
 * Returns the machine CPU affinity bitmap (caller frees), NULL when the
 * affinity could not be resolved.
 */
static bitstr_t *_set_cpu_affinity(int (*xcpuinfo_mac_to_abs)(char *mac,
							      char **abs),
				   int card_num, bitstr_t *enabled_cpus,
				   char **cores)
{
	char *path = NULL, *line = NULL, *mac_range = NULL;
	bitstr_t *cpus_bitmap = NULL;

	xassert(xcpuinfo_mac_to_abs);
	xassert(cores);

	*cores = NULL;

	path = _card_device_path(card_num, "local_cpulist");
	if (!(line = _read_sysfs_line(path))) {
		error("%s: unable to read %s", __func__, path);
		xfree(path);
		return NULL;
	}

	cpus_bitmap = bit_alloc(MAX_CPUS);
	if (bit_unfmt(cpus_bitmap, line)) {
		error("%s: unable to parse CPU list \"%s\" from %s",
		      __func__, line, path);
		FREE_NULL_BITMAP(cpus_bitmap);
		xfree(line);
		xfree(path);
		return NULL;
	}
	xfree(line);
	xfree(path);

	if (enabled_cpus)
		bit_and(cpus_bitmap, enabled_cpus);

	mac_range = bit_fmt_full(cpus_bitmap);
	if (xcpuinfo_mac_to_abs(mac_range, cores)) {
		error("%s: machine to abstract conversion failed", __func__);
		FREE_NULL_BITMAP(cpus_bitmap);
	}
	xfree(mac_range);

	return cpus_bitmap;
}

/*
 * Emit one GRES from its already-resolved fields. The caller keeps ownership
 * of every pointer argument.
 *
 * cpu_cnt (IN) The node's CPU count, recorded on the GRES.
 * gres_list (IN/OUT) The system GRES list to append to.
 * file (IN) The GRES device file.
 * type_name (IN) The GRES type name, or NULL to emit an untyped GRES.
 * unique_id (IN) The ROCR GPU-<hex> token, or NULL for selection by index.
 * cores (IN) The abstract core affinity range, or NULL when unresolved.
 * cpus_bitmap (IN) The machine CPU affinity bitmap, or NULL when unresolved.
 * links (IN) The GRES Links vector.
 */
static void _emit_gpu_gres(uint32_t cpu_cnt, list_t *gres_list, char *file,
			   char *type_name, char *unique_id, char *cores,
			   bitstr_t *cpus_bitmap, char *links)
{
	gres_slurmd_conf_t gres_slurmd_conf = {
		.config_flags = (GRES_CONF_ENV_RSMI | GRES_CONF_AUTODETECT),
		.count = 1,
		.cpu_cnt = cpu_cnt,
		/* The GRES cpus field carries the abstract core range. */
		.cpus = cores,
		.cpus_bitmap = cpus_bitmap,
		.file = file,
		.links = links,
		.name = "gpu",
		.type_name = type_name,
		.unique_id = unique_id,
	};

	xassert(gres_list);
	xassert(file);
	xassert(links);

	add_gres_to_list(gres_list, &gres_slurmd_conf);
}

/*
 * Log the number of GPUs detected on this node, at info.
 */
static void _log_capabilities(void)
{
	info("%d GPU system device(s) detected", amdgpu_ncards);
}

/*
 * Order cached cards by PCI address, the device numbering gres.conf documents.
 * The kernel zero pads the address fields, so text order is numeric order.
 *
 * a (IN) First card to compare (amd_card_t).
 * b (IN) Second card to compare (amd_card_t).
 *
 * Returns negative, zero, or positive as a sorts before, at, or after b.
 */
static int _sort_by_pci_address(const void *a, const void *b)
{
	const amd_card_t *ca = a, *cb = b;

	xassert(a);
	xassert(b);

	return xstrcmp(ca->pci_address, cb->pci_address);
}

/*
 * Discover the GPUs on the first call, caching them ordered by PCI address,
 * packed densely so entry i is the card behind GRES index i. A failed listing
 * stays uncached for a later retry.
 *
 * Returns the number of cached cards, 0 when none could be cached.
 */
static int _discover_gpus(void)
{
	int ncards = 0;
	int *cards = NULL;

	if (amdgpu_cards)
		return amdgpu_ncards;

	if (!(ncards = _list_amdgpu_cards(&cards)))
		return 0;

	/*
	 * Keep only cards exposing a render node and a readable model, caching
	 * them so the emit loop needs no second read. The kept list is dense,
	 * so every Links vector discovery sizes to it is exact (an over-wide
	 * one drains the node).
	 */
	amdgpu_cards = xcalloc(ncards, sizeof(*amdgpu_cards));
	for (int i = 0; i < ncards; i++) {
		amd_card_t *kept = &amdgpu_cards[amdgpu_ncards];
		char *mode = NULL;

		/* Skip: a fileless GRES cannot be device-cgroup isolated. */
		if ((kept->render_minor = _get_render_minor(cards[i])) < 0) {
			error("%s: card%d: no render node, no GRES emitted",
			      __func__, cards[i]);
			continue;
		}
		if (!(kept->pci_address = _read_pci_address(cards[i]))) {
			error("%s: card%d: unreadable PCI address, no GRES emitted",
			      __func__, cards[i]);
			continue;
		}
		if (!(kept->name = _read_card_model(cards[i]))) {
			error("%s: card%d: unreadable model, no GRES emitted",
			      __func__, cards[i]);
			xfree(kept->pci_address);
			continue;
		}
		/*
		 * A compute partition mode other than SPX splits the GPU into
		 * several KFD devices this DRM scan does not see; the GRES
		 * stays the whole GPU.
		 */
		if ((mode = _read_card_attr(cards[i],
					    "current_compute_partition"))) {
			if (xstrcasecmp(mode, "SPX"))
				error("%s: card%d: compute partition mode %s is not supported, only SPX; the whole GPU is one GRES",
				      __func__, cards[i], mode);
			xfree(mode);
		}
		kept->card = cards[i];
		amdgpu_ncards++;
	}

	xfree(cards);

	/* The array, not the count, marks a fill, keeping failure retryable. */
	if (!amdgpu_ncards) {
		xfree(amdgpu_cards);
		return 0;
	}

	/*
	 * Order the cache by PCI address, so GRES index i is the GPU with the
	 * i-th smallest address, the device numbering gres.conf documents.
	 */
	qsort(amdgpu_cards, amdgpu_ncards, sizeof(*amdgpu_cards),
	      _sort_by_pci_address);

	return amdgpu_ncards;
}

/*
 * Build the GRES conf list of the AMD GPUs detected on the node.
 *
 * node_config (IN) The node configuration.
 *
 * Returns the GRES conf list (caller frees), or NULL if no GPUs are found or
 * on error.
 */
static list_t *_get_system_gpu_list_amd(const node_config_load_t *node_config)
{
	list_t *gres_list_system = NULL;
	bitstr_t *enabled_cpus = NULL;
	int nuuid = 0;

	xassert(node_config);

	if (!_discover_gpus())
		return NULL;

	gres_list_system = list_create(destroy_gres_slurmd_conf);

	/* Mask out E-cores, which local_cpulist lists but slurmd may not. */
	enabled_cpus = gpu_common_get_enabled_cpus();

	for (int i = 0; i < amdgpu_ncards; i++) {
		char *file = NULL, *uuid = NULL, *unique_id = NULL;
		char *cores = NULL, *links = NULL;
		bitstr_t *cpus_bitmap = NULL;

		debug2("GPU(amd) card%d:", amdgpu_cards[i].card);
		debug2("    Name: %s", amdgpu_cards[i].name);
		debug2("    PCI Address: %s", amdgpu_cards[i].pci_address);

		/*
		 * Both DRM nodes as one MultipleFiles group, so the device
		 * cgroup confines the card node too: a card node left
		 * accessible hands its first opener the DRM master role with
		 * the full API.
		 */
		file = xstrdup_printf("%s/renderD%d,%s/card%d", GPU_AMD_DEV_DIR,
				      amdgpu_cards[i].render_minor,
				      GPU_AMD_DEV_DIR, amdgpu_cards[i].card);
		debug2("    Device File: %s", file);

		/*
		 * amdgpu exports 16 hex ID digits only for GPUs carrying a
		 * hardware ID, leaving the attribute absent or empty on the
		 * rest. ROCR reports the same digits as "GPU-<hex>" and
		 * matches a ROCR_VISIBLE_DEVICES token regardless of case. Under
		 * Flags=env_uuid a job allocated a GPU with no ID gets numeric
		 * indices for all of its GPUs.
		 */
		if ((uuid = _read_card_attr(amdgpu_cards[i].card,
					    "unique_id"))) {
			unique_id = xstrdup_printf("GPU-%s", uuid);
			xfree(uuid);
			nuuid++;
		}
		debug2("    UUID: %s", unique_id);

		cpus_bitmap =
			_set_cpu_affinity(node_config->xcpuinfo_mac_to_abs,
					  amdgpu_cards[i].card, enabled_cpus,
					  &cores);
		if (cpus_bitmap) {
			char *mac_range = bit_fmt_full(cpus_bitmap);

			debug2("    CPU Affinity Range - Machine: %s", mac_range);
			xfree(mac_range);
		}
		debug2("    Core Affinity Range - Abstract: %s", cores);

		/* Links: emit-order placeholder */
		links = gres_links_create_empty(i, amdgpu_ncards);
		debug2("    Links: %s", links);

		_emit_gpu_gres(node_config->cpu_cnt, gres_list_system, file,
			       amdgpu_cards[i].name, unique_id, cores,
			       cpus_bitmap, links);

		xfree(unique_id);
		xfree(cores);
		FREE_NULL_BITMAP(cpus_bitmap);
		xfree(file);
		xfree(links);
	}

	FREE_NULL_BITMAP(enabled_cpus);

	/*
	 * Log once per process: with AutoDetect=full the plugin probe and the
	 * GRES load both run this discovery.
	 */
	if (!discovery_logged) {
		if (nuuid && (nuuid < amdgpu_ncards))
			warning("gpu/amd: node has GPUs both with and without a UUID. With Flags=env_uuid, a job allocated a GPU without a UUID gets numeric indices for all of its GPUs.");
		_log_capabilities();
		discovery_logged = true;
	}

	return gres_list_system;
}

/*
 * Initialize the plugin on load.
 *
 * Returns SLURM_SUCCESS.
 */
extern int init(void)
{
	debug("%s loaded", plugin_name);

	return SLURM_SUCCESS;
}

/*
 * Free plugin state on unload.
 */
extern void fini(void)
{
	for (int i = 0; i < amdgpu_ncards; i++) {
		xfree(amdgpu_cards[i].name);
		xfree(amdgpu_cards[i].pci_address);
	}
	xfree(amdgpu_cards);
	amdgpu_ncards = 0;
	discovery_logged = false;

	debug("unloading %s", plugin_name);
}

/*
 * Detect the node's AMD GPUs via sysfs and return them as a GRES conf list.
 *
 * node_config (IN) The node configuration.
 *
 * Returns the GRES conf list (the caller frees it), or NULL on error.
 */
extern list_t *gpu_p_get_system_gpu_list(node_config_load_t *node_config)
{
	list_t *gres_list_system = NULL;

	xassert(node_config);

	if (!(gres_list_system = _get_system_gpu_list_amd(node_config)))
		error("System GPU detection failed");

	return gres_list_system;
}

/*
 * At step start (in slurmstepd), report a requested GPU frequency as not
 * controlled, like gpu/generic does. This plugin does not drive GPU clocks.
 *
 * usable_gpus (IN) Bitmap of the GPUs allocated to the step.
 * tres_freq (IN) The step's TRES frequency specification.
 */
extern void gpu_p_step_hardware_init(bitstr_t *usable_gpus, char *tres_freq)
{
	xassert(tres_freq);
	xassert(usable_gpus);

	if (!usable_gpus || !tres_freq)
		return;

	/* No GPU frequency spec */
	if (!xstrstr(tres_freq, "gpu:"))
		return;

	fprintf(stderr, "GpuFreq=control_disabled\n");
}

/*
 * At step end (in slurmstepd), reset each card gpu_p_step_hardware_init()
 * drove back to automatic performance management.
 */
extern void gpu_p_step_hardware_fini(void)
{
	return;
}

/*
 * Testsuite CPU range conversion hook, not implemented by this plugin.
 *
 * cpu_range (IN) The CPU range string to convert.
 *
 * Returns NULL.
 */
extern char *gpu_p_test_cpu_conv(char *cpu_range)
{
	return NULL;
}

/*
 * Read the current power draw of a GPU for energy accounting.
 *
 * dv_ind (IN) The GPU index.
 * gpu (IN/OUT) The GPU status to update with the power reading.
 *
 * Returns SLURM_SUCCESS.
 */
extern int gpu_p_energy_read(uint32_t dv_ind, gpu_status_t *gpu)
{
	xassert(gpu);

	return SLURM_SUCCESS;
}

/*
 * Count the node's amdgpu GPUs, that is the GPUs discovery emits a GRES for.
 *
 * device_count (OUT) Set to the number of AMD GPUs found, or 0 on error.
 */
extern void gpu_p_get_device_count(uint32_t *device_count)
{
	xassert(device_count);

	*device_count = _discover_gpus();
}

/*
 * Read a process's GPU memory and compute-unit usage for job accounting.
 *
 * pid (IN) The process ID to read GPU usage for.
 * data (IN/OUT) The accounting array to fill with the process's GPU usage.
 *
 * Returns SLURM_SUCCESS.
 */
extern int gpu_p_usage_read(pid_t pid, acct_gather_data_t *data)
{
	xassert(data);

	return SLURM_SUCCESS;
}
