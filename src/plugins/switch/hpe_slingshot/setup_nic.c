/*****************************************************************************\
 *  setup_nic.c - Library for managing HPE Slingshot networks
 *****************************************************************************
 *  Copyright 2021 Hewlett Packard Enterprise Development LP
 *  Written by Jim Nordby <james.nordby@hpe.com>
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

#include "config.h"

#define _GNU_SOURCE

#include "src/common/slurm_xlator.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "switch_hpe_slingshot.h"

#include <dlfcn.h>

#include "libcxi/libcxi.h"

/* Global variables */
static void *cxi_handle = NULL;
static bool cxi_avail = false;
static struct cxil_dev **cxi_devs;
static int cxi_ndevs = 0;

/* Function pointers loaded from libcxi */
static int (*cxil_get_device_list_p)(struct cxil_device_list **);
static int (*cxil_open_device_p)(uint32_t, struct cxil_dev **);
static int (*cxil_alloc_svc_p)(struct cxil_dev *, struct cxi_svc_desc *,
	struct cxi_svc_fail_info *);
static int (*cxil_destroy_svc_p)(struct cxil_dev *, unsigned int);


#define LOOKUP_SYM(_lib, _version, x) \
do { \
	x ## _p = (_version && _version[0]) ? \
		dlvsym(_lib, #x, _version) : dlsym(_lib, #x); \
	if (x ## _p == NULL) { \
		error("Error loading symbol %s, version '%s': %s", \
			#x, _version, dlerror()); \
		return false; \
	} \
} while (0)

static bool _load_cxi_funcs(void *lib, char *version)
{
	LOOKUP_SYM(lib, version, cxil_get_device_list);
	LOOKUP_SYM(lib, version, cxil_open_device);
	LOOKUP_SYM(lib, version, cxil_alloc_svc);
	LOOKUP_SYM(lib, version, cxil_destroy_svc);

	return true;
}

static void _print_devinfo(int dev, struct cxil_devinfo *info)
{
#define PDEVINFO(FMT, ...) \
	log_flag(SWITCH, "devinfo[%d]: " FMT, dev, ##__VA_ARGS__)

	PDEVINFO("device_name='%s' driver_name='%s'",
		info->device_name, info->driver_name);
	PDEVINFO("dev_id=%u nic_addr=%u pid_bits=%u pid_count=%u",
		info->dev_id, info->nic_addr, info->pid_bits, info->pid_count);
	PDEVINFO("pid_granule=%u min_free_shift=%u rdzv_get_idx=%u",
		info->pid_granule, info->min_free_shift, info->rdzv_get_idx);
	PDEVINFO("vendor_id=%u device_id=%u device_rev=%u device_proto=%u"
		 " device_platform=%u",
		info->vendor_id, info->device_id, info->device_rev,
		info->device_proto, info->device_platform);
	PDEVINFO("num_ptes=%hu num_txqs=%hu num_tgqs=%hu num_eqs=%hu",
		info->num_ptes, info->num_txqs, info->num_tgqs, info->num_eqs);
	PDEVINFO("num_cts=%hu num_acs=%hu num_tles=%hu num_les=%hu",
		info->num_cts, info->num_acs, info->num_tles, info->num_les);
	PDEVINFO("pci_domain=%hu pci_bus=%hu pci_device=%hu pci_function=%hu",
		info->pci_domain, info->pci_bus, info->pci_device,
		info->pci_function);
	PDEVINFO("link_mtu=%zu link_speed=%zu link_state=%hu uc_nic=%d",
		info->link_mtu, info->link_speed, info->link_state,
		info->uc_nic);
	PDEVINFO("pct_eq=%u fru_description='%s' is_vf=%u",
		info->pct_eq, info->fru_description, info->is_vf);
#undef PDEVINFO
}

/*
 * Return array of limits already reserved by system services
 */
static bool _get_reserved_limits(int dev, slingshot_limits_set_t *limits)
{
	int svc, rc;
	struct cxil_svc_list *list = NULL;

	if (!cxi_devs[dev])
		return true;
	if ((rc = cxil_get_svc_list(cxi_devs[dev], &list))) {
		error("Could not get service list for CXI device[%d] dev_id=%d (%s): %d",
			dev, cxi_devs[dev]->info.dev_id,
			cxi_devs[dev]->info.device_name, rc);
		return false;
	}
	for (svc = 0; svc < list->count; svc++) {
#define PLIMIT(DEV, SVC, LIM) { \
	limits->LIM.res += list->descs[SVC].limits.LIM.res; \
	log_flag(SWITCH, "CXI dev/svc/system[%d][%d][%d]: limits.%s.res %hu (tot/max %hu %hu)", \
		 DEV, SVC, list->descs[SVC].is_system_svc, #LIM, \
		 list->descs[SVC].limits.LIM.res, limits->LIM.res, \
		 list->descs[SVC].limits.LIM.max); \
}
		PLIMIT(dev, svc, ptes);
		PLIMIT(dev, svc, txqs);
		PLIMIT(dev, svc, tgqs);
		PLIMIT(dev, svc, eqs);
		PLIMIT(dev, svc, cts);
		PLIMIT(dev, svc, acs);
		PLIMIT(dev, svc, tles);
		PLIMIT(dev, svc, les);
#undef PLIMIT
	}
	free(list);	/* can't use xfree() */
	return true;
}

/*
 * Set up basic access to the CXI devices in the daemon
 */
static bool _create_cxi_devs(void)
{
	struct cxil_device_list *list;
	int dev, rc;

	if ((rc = cxil_get_device_list_p(&list))) {
		error("Could not get a list of the CXI devices: %d %d",
		      rc, errno);
		return false;
	}

	/* If there are no CXI NICs, just say it's unsupported */
	if (!list->count) {
		error("No CXI devices available");
		return false;
	}

	cxi_devs = xcalloc(list->count, sizeof(struct cxil_dev *));
	cxi_ndevs = list->count;

	/* We're OK with only getting access to a subset */
	slingshot_limits_set_t reslimits = { 0 };
	for (dev = 0; dev < cxi_ndevs; dev++) {
		struct cxil_devinfo *info = &list->info[dev];
		if ((rc = cxil_open_device_p(info->dev_id, &cxi_devs[dev]))) {
			error("Could not open CXI device[%d] dev_id=%d (%s): %d",
			      dev, info->dev_id, info->device_name, rc);
			cxi_devs[dev] = NULL;
			continue;
		}
		/* Only done in debug mode */
		if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)
			_print_devinfo(dev, &cxi_devs[dev]->info);
		if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)
			_get_reserved_limits(dev, &reslimits);
	}

	return true;
}

/*
 * Return a cxi_limits struct with res/max fields set according to
 * job max/res/def limits, device max limits, and number of CPUs on node
 */
static struct cxi_limits set_desc_limits(const char *name,
					 const slingshot_limits_t *joblimits,
					 uint16_t dev_max, int ncpus)
{
	struct cxi_limits ret;

	/* Restrict job max to device max */
	ret.max = MIN(joblimits->max, dev_max);
	/* If job reserved is set, use that, otherwise job default * ncpus */
	ret.res = joblimits->res ? joblimits->res : (joblimits->def * ncpus);
	/* Reserved can't be higher than max */
	ret.res = MIN(ret.res, ret.max);
	log_flag(SWITCH, "job %s.max/res/def/cpus %hu %hu %hu %d"
		 " CXI desc %s.max/res %hu %hu",
		 name, joblimits->max, joblimits->res, joblimits->def, ncpus,
		 name, ret.max, ret.res);
	return ret;
}

/*
 * Initialize a cxi_svc_desc with our CXI settings
 */
static void _create_cxi_descriptor(struct cxi_svc_desc *desc,
				   const struct cxil_devinfo *devinfo,
				   const slingshot_jobinfo_t *job,
				   uint32_t uid, uint16_t step_cpus)
{
	int cpus;

	memset(desc, 0, sizeof(*desc));

#ifdef CXI_SVC_MEMBER_UID
	desc->restricted_members = true;
	desc->members[0].type = CXI_SVC_MEMBER_UID;
	desc->members[0].svc_member.uid = uid;
	desc->members[1].type = CXI_SVC_MEMBER_IGNORE;
#else
	desc->restricted_members = false;
#endif

	/* Set up VNI */
	if (job->num_vnis > 0) {
		desc->restricted_vnis = true;
		desc->num_vld_vnis = job->num_vnis;
		for (int v = 0; v < job->num_vnis; v++)
			desc->vnis[v] = job->vnis[v];
	} else {
		desc->num_vld_vnis = 0;
		desc->restricted_vnis = false;
	}


	/* Set up traffic classes; best effort if none given */
	desc->restricted_tcs = true;
	if (job->tcs) {
		if (job->tcs & SLINGSHOT_TC_DEDICATED_ACCESS)
			desc->tcs[CXI_TC_DEDICATED_ACCESS] = true;
		if (job->tcs & SLINGSHOT_TC_LOW_LATENCY)
			desc->tcs[CXI_TC_LOW_LATENCY] = true;
		if (job->tcs & SLINGSHOT_TC_BULK_DATA)
			desc->tcs[CXI_TC_BULK_DATA] = true;
		if (job->tcs & SLINGSHOT_TC_BEST_EFFORT)
			desc->tcs[CXI_TC_BEST_EFFORT] = true;
	} else
		desc->tcs[CXI_TC_BEST_EFFORT] = true;

	/* Set up other fields */
	desc->is_system_svc = false;

	/* Set up resource limits */
	desc->resource_limits = true;
	/*
	 * If --network=depth=<X> (job->depth) is used, use that as
	 * the multiplier for the per-thread limit reservation setting;
	 * otherwise use the number of CPUs for this step
	 */
	cpus = job->depth ? job->depth : step_cpus;
	desc->limits.txqs = set_desc_limits("txqs", &job->limits.txqs,
					    devinfo->num_txqs, cpus);
	desc->limits.tgqs = set_desc_limits("tgqs", &job->limits.tgqs,
					    devinfo->num_tgqs, cpus);
	desc->limits.eqs = set_desc_limits("eqs", &job->limits.eqs,
					    devinfo->num_eqs, cpus);
	desc->limits.cts = set_desc_limits("cts", &job->limits.cts,
					    devinfo->num_cts, cpus);
	desc->limits.tles = set_desc_limits("tles", &job->limits.tles,
					    devinfo->num_tles, cpus);
	desc->limits.ptes = set_desc_limits("ptes", &job->limits.ptes,
					    devinfo->num_ptes, cpus);
	desc->limits.les = set_desc_limits("les", &job->limits.les,
					    devinfo->num_les, cpus);
	desc->limits.acs = set_desc_limits("acs", &job->limits.acs,
					    devinfo->num_acs, cpus);

	/* Differentiates system and user services */
	desc->is_system_svc = false;
}

/*
 * Open the Slingshot CXI library; set up functions and set cxi_avail
 * if successful (default is 'false')
 */
extern bool slingshot_open_cxi_lib(void)
{
	char *libfile;
	char *version;

	if (!(libfile = getenv(SLINGSHOT_CXI_LIB_ENV)))
		libfile = SLINGSHOT_CXI_LIB;

	if (!libfile || (libfile[0] == '\0')) {
		error("Bad library file specified by %s variable",
		      SLINGSHOT_CXI_LIB_ENV);
		goto out;
	}

	if (!(cxi_handle = dlopen(libfile, RTLD_LAZY | RTLD_GLOBAL))) {
		error("Couldn't find CXI library %s: %s", libfile, dlerror());
		goto out;
	}

	if (!(version = getenv(SLINGSHOT_CXI_LIB_VERSION_ENV)))
		version = SLINGSHOT_CXI_LIB_VERSION;

	debug("CXI library %s, version '%s'", libfile, version);
	if (!_load_cxi_funcs(cxi_handle, version))
		goto out;

	if (!_create_cxi_devs())
		goto out;

	cxi_avail = true;
out:
	log_flag(SWITCH, "cxi_avail=%d", cxi_avail);
	return cxi_avail;
}

/*
 * Return a pointer to the cxi_devs[] slot with the requested device name;
 * return NULL if not found
 */
static struct cxil_dev *_device_name_to_dev(const char *devname)
{
	for (int dev = 0; dev < cxi_ndevs; dev++) {
		if (!cxi_devs[dev])
			continue;
		if (!xstrcmp(devname, cxi_devs[dev]->info.device_name))
			return cxi_devs[dev];
	}
	return NULL;
}

/*
 * In the daemon, when the shepherd for an App terminates, free any CXI
 * Services we have allocated for it
 */
extern bool slingshot_destroy_services(slingshot_jobinfo_t *job)
{
	xassert(job);

	if (!cxi_avail)
		return true;

	for (int prof = 0; prof < job->num_profiles; prof++) {
		int svc_id = job->profiles[prof].svc_id;
		const char *devname = job->profiles[prof].device_name;

		/* Service ID 0 means not a Service */
		if (svc_id <= 0)
			continue;

		/* Find device associated with profile */
		struct cxil_dev *dev = _device_name_to_dev(devname);
		if (!dev) {
			error("Cannot find device for CXI Service ID %d (%s)",
				svc_id, devname);
			continue;
		}

		debug("Destroying CXI SVC ID %d on NIC %s",
			svc_id, devname);

		int rc = cxil_destroy_svc_p(dev, svc_id);
		if (rc) {
			error("Failed to destroy CXI Service ID %d (%s): %d",
			      svc_id, devname, rc);
			return false;
		}
	}

	xfree(job->profiles);
	job->profiles = NULL;
	job->num_profiles = 0;
	return true;
}

/*
 * If cxil_alloc_svc failed, log information about the failure
 */
static void _alloc_fail_info(const struct cxil_dev *dev,
			     const struct cxi_svc_desc *desc,
			     const struct cxi_svc_fail_info *fail_info)
{
	error("Slingshot service allocation failed on %s",
	      dev->info.device_name);

	for (int rsrc = 0; rsrc < CXI_RSRC_TYPE_MAX; rsrc++) {
		char *rsrc_str = NULL;
		int rsrc_req = 0;

		switch (rsrc) {
		case CXI_RSRC_TYPE_PTE:
			rsrc_str = "portal table entries";
			rsrc_req = desc->limits.ptes.res;
			break;
		case CXI_RSRC_TYPE_TXQ:
			rsrc_str = "transmit command queues";
			rsrc_req = desc->limits.txqs.res;
			break;
		case CXI_RSRC_TYPE_TGQ:
			rsrc_str = "target command queues";
			rsrc_req = desc->limits.tgqs.res;
			break;
		case CXI_RSRC_TYPE_EQ:
			rsrc_str = "event queues";
			rsrc_req = desc->limits.eqs.res;
			break;
		case CXI_RSRC_TYPE_CT:
			rsrc_str = "counters";
			rsrc_req = desc->limits.cts.res;
			break;
		case CXI_RSRC_TYPE_LE:
			rsrc_str = "list entries";
			rsrc_req = desc->limits.les.res;
			break;
		case CXI_RSRC_TYPE_TLE:
			rsrc_str = "trigger list entries";
			rsrc_req = desc->limits.tles.res;
			break;
		case CXI_RSRC_TYPE_AC:
			rsrc_str = "addressing contexts";
			rsrc_req = desc->limits.acs.res;
			break;
		case CXI_RSRC_TYPE_MAX:
		default:
			rsrc_str = "invalid resource";
			break;
		}

		if (rsrc_req > fail_info->rsrc_avail[rsrc])
			error("Only %d %s available on %s (requested %d)",
			      fail_info->rsrc_avail[rsrc], rsrc_str,
			      dev->info.device_name, rsrc_req);
	}

	if (fail_info->no_le_pools)
		error("No list entry pools available on %s",
		      dev->info.device_name);
	if (fail_info->no_tle_pools)
		error("No trigger list entry pools available on %s",
		      dev->info.device_name);
	if (fail_info->no_cntr_pools)
		error("No counter pools available on %s",
		      dev->info.device_name);
}

/*
 * Set up CXI services for each of the CXI NICs on this host
 */
extern bool slingshot_create_services(slingshot_jobinfo_t *job, uint32_t uid,
				      uint16_t step_cpus)
{
	int prof, devn;
	struct cxi_svc_desc desc;
	struct cxil_dev *dev;
	struct cxi_svc_fail_info failinfo;
	pals_comm_profile_t *profile;

	xassert(job);

	/* dlopen() libcxi and query CXI devices */
	slingshot_open_cxi_lib();

	/* Just return true if CXI not available or no VNIs to set up */
	if (!cxi_avail || !job->num_vnis) {
		log_flag(SWITCH, "cxi_avail=%d num_vnis=%d, ret true",
			 cxi_avail, job->num_vnis);
		return true;
	}

	/* Figure out number of working NICs = services to create */
	job->num_profiles = 0;
	for (int i = 0; i < cxi_ndevs; i++) {
		if (cxi_devs[i])
			job->num_profiles++;
	}
	job->profiles = xcalloc(job->num_profiles, sizeof(*job->profiles));

	/* Create a Service for each NIC */
	prof = 0;
	for (devn = 0; devn < cxi_ndevs; devn++) {
		dev = cxi_devs[devn];
		if (!dev)
			continue;

		/* Set what we'll need in the CXI Service */
		_create_cxi_descriptor(&desc, &dev->info, job, uid, step_cpus);

		int svc_id = cxil_alloc_svc_p(dev, &desc, &failinfo);
		if (svc_id < 0) {
			_alloc_fail_info(dev, &desc, &failinfo);
			goto error;
		}

		profile = &job->profiles[prof];
		profile->svc_id = svc_id;
		for (int v = 0; v < job->num_vnis; v++)
			profile->vnis[v] = job->vnis[v];
		profile->vnis_used = job->num_vnis;
		profile->tcs = job->tcs;
		snprintf(profile->device_name, sizeof(profile->device_name),
			"%s", dev->info.device_name);

		debug("Creating CXI profile[%d] on NIC %d (%s):"
			" SVC ID %u vnis=[%hu %hu %hu %hu] tcs=%u",
			prof, devn, profile->device_name, profile->svc_id,
			profile->vnis[0], profile->vnis[1], profile->vnis[2],
			profile->vnis[3], profile->tcs);
		prof++;
	}
	return true;

error:
	slingshot_destroy_services(job);
	return false;
}

/*
 * Free any allocated space before unloading the plugin
 */
extern void slingshot_free_services(void)
{
	if (cxi_handle)
		dlclose(cxi_handle);

	if (cxi_devs) {
		for (int i = 0; i < cxi_ndevs; i++)
			free(cxi_devs[i]);
		xfree(cxi_devs);
	}
}
