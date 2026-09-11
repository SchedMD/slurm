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

/* A card kept by discovery. */
typedef struct {
	int card; /* DRM card number */
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
 * Emit one GRES from its already-resolved fields. The caller keeps ownership
 * of every pointer argument.
 *
 * cpu_cnt (IN) The node's CPU count, recorded on the GRES.
 * gres_list (IN/OUT) The system GRES list to append to.
 * file (IN) The GRES device file.
 * links (IN) The GRES Links vector.
 */
static void _emit_gpu_gres(uint32_t cpu_cnt, list_t *gres_list, char *file,
			   char *links)
{
	gres_slurmd_conf_t gres_slurmd_conf = {
		.config_flags = (GRES_CONF_ENV_RSMI | GRES_CONF_AUTODETECT),
		.count = 1,
		.cpu_cnt = cpu_cnt,
		.file = file,
		.links = links,
		.name = "gpu",
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
	 * Keep only cards exposing a render node, caching them so the emit loop
	 * needs no second read. The kept list is dense, so every Links vector
	 * discovery sizes to it is exact (an over-wide one drains the node).
	 */
	amdgpu_cards = xcalloc(ncards, sizeof(*amdgpu_cards));
	for (int i = 0; i < ncards; i++) {
		amd_card_t *kept = &amdgpu_cards[amdgpu_ncards];

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

	xassert(node_config);

	if (!_discover_gpus())
		return NULL;

	gres_list_system = list_create(destroy_gres_slurmd_conf);

	for (int i = 0; i < amdgpu_ncards; i++) {
		char *file = NULL, *links = NULL;

		debug2("GPU(amd) card%d:", amdgpu_cards[i].card);
		debug2("    PCI Address: %s", amdgpu_cards[i].pci_address);

		file = xstrdup_printf("%s/renderD%d", GPU_AMD_DEV_DIR,
				      amdgpu_cards[i].render_minor);
		debug2("    Device File: %s", file);

		/* Links: emit-order placeholder */
		links = gres_links_create_empty(i, amdgpu_ncards);
		debug2("    Links: %s", links);

		_emit_gpu_gres(node_config->cpu_cnt, gres_list_system, file,
			       links);

		xfree(file);
		xfree(links);
	}

	/*
	 * Log once per process: with AutoDetect=full the plugin probe and the
	 * GRES load both run this discovery.
	 */
	if (!discovery_logged) {
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
	for (int i = 0; i < amdgpu_ncards; i++)
		xfree(amdgpu_cards[i].pci_address);
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
