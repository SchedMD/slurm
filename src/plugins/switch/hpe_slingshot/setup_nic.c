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
#include <sys/stat.h>

#include "libcxi/libcxi.h"

/* Global variables */
static void *cxi_handle = NULL;
static bool cxi_avail = false;
static struct cxil_dev **cxi_devs = NULL;
static int cxi_ndevs = 0;

/* Define struct not defined in earlier versions of libcxi */
#ifndef HAVE_STRUCT_CXI_RSRC_USE
#define CXI_RSRC_TYPE_MAX 8
struct cxi_rsrc_use {
	uint16_t in_use[CXI_RSRC_TYPE_MAX];
};
#endif

/* Function pointers loaded from libcxi */
static int (*cxil_get_device_list_p)(struct cxil_device_list **);
static int (*cxil_get_svc_list_p)(struct cxil_dev *dev,
				  struct cxil_svc_list **svc_list);
static int (*cxil_open_device_p)(uint32_t, struct cxil_dev **);
static int (*cxil_alloc_svc_p)(struct cxil_dev *, struct cxi_svc_desc *,
	struct cxi_svc_fail_info *);
static int (*cxil_destroy_svc_p)(struct cxil_dev *, unsigned int);
#if HAVE_STRUCT_CXI_RSRC_USE
static int (*cxil_get_svc_rsrc_use_p)(struct cxil_dev *, unsigned int,
				      struct cxi_rsrc_use *);
#endif


#define LOOKUP_SYM(_lib, x) \
do { \
	x ## _p = dlsym(_lib, #x); \
	if (x ## _p == NULL) { \
		log_flag(SWITCH, "Error loading symbol: %s (skipped)", \
			 dlerror()); \
	} \
} while (0)

static bool _load_cxi_funcs(void *lib)
{
	LOOKUP_SYM(lib, cxil_get_device_list);
	LOOKUP_SYM(lib, cxil_get_svc_list);
	LOOKUP_SYM(lib, cxil_open_device);
	LOOKUP_SYM(lib, cxil_alloc_svc);
	LOOKUP_SYM(lib, cxil_destroy_svc);
#if HAVE_STRUCT_CXI_RSRC_USE
	LOOKUP_SYM(lib, cxil_get_svc_rsrc_use);
#endif
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
 * Subtract MAX(reserved limit, current in_use value) for this system service
 * from the max value for this limit specified in the device info;
 * this will be used as the max value for the device that users can request
 */
static void _adjust_limit(int rsrc, int dev, int svc, struct cxi_limits *lim,
			  uint16_t *in_use, uint16_t *devmax)
{
	uint16_t oldmax = *devmax;
	uint16_t adjust = MAX(lim[rsrc].res, in_use[rsrc]);
	*devmax -= adjust;
	log_flag(SWITCH, "CXI dev/svc[%d][%d]: limits.%s.res/in_use %hu %hu (old/new max %hu %hu)",
		 dev, svc, cxi_rsrc_type_strs[rsrc], lim[rsrc].res,
		 in_use[rsrc], oldmax, *devmax);
}

/*
 * For this device, total the reserved resources used by system services,
 * and remove that amount from the device's max limits (this will be used
 * as a ceiling for resource requests)
 */
static bool _adjust_dev_limits(int dev, struct cxil_devinfo *devinfo)
{
	int svc, rc;
	int num_svc = 0, num_system_svc = 0;
	struct cxil_svc_list *list = NULL;

	if (!cxi_devs[dev])
		return true;
	if ((rc = cxil_get_svc_list_p(cxi_devs[dev], &list))) {
		error("Could not get service list for CXI dev_id=%d (%s): %s",
		      devinfo->dev_id, devinfo->device_name, strerror(-rc));
		return false;
	}
	for (svc = 0; svc < list->count; svc++) {
		struct cxi_rsrc_use usage = { 0 };
		struct cxi_limits *lim;

		if (!list->descs[svc].is_system_svc) {
			num_svc++;
			continue;
		}
		num_system_svc++;
#if HAVE_STRUCT_CXI_RSRC_USE
		if ((rc = cxil_get_svc_rsrc_use_p(cxi_devs[dev],
						  list->descs[svc].svc_id,
						  &usage))) {
			error("Could not get resource usage for CXI dev_id=%d (%s) svc_id=%d: %s",
			      devinfo->dev_id, devinfo->device_name,
			      list->descs[svc].svc_id, strerror(-rc));
			// in_use value will be 0
		}
#endif
		lim = list->descs[svc].limits.type;
		_adjust_limit(CXI_RSRC_TYPE_PTE, dev, svc, lim,
			      usage.in_use, &devinfo->num_ptes);
		_adjust_limit(CXI_RSRC_TYPE_TXQ, dev, svc, lim,
			      usage.in_use, &devinfo->num_txqs);
		_adjust_limit(CXI_RSRC_TYPE_TGQ, dev, svc, lim,
			      usage.in_use, &devinfo->num_tgqs);
		_adjust_limit(CXI_RSRC_TYPE_EQ, dev, svc, lim,
			      usage.in_use, &devinfo->num_eqs);
		_adjust_limit(CXI_RSRC_TYPE_CT, dev, svc, lim,
			      usage.in_use, &devinfo->num_cts);
		_adjust_limit(CXI_RSRC_TYPE_LE, dev, svc, lim,
			      usage.in_use, &devinfo->num_les);
		_adjust_limit(CXI_RSRC_TYPE_TLE, dev, svc, lim,
			      usage.in_use, &devinfo->num_tles);
		_adjust_limit(CXI_RSRC_TYPE_AC, dev, svc, lim,
			      usage.in_use, &devinfo->num_acs);
	}
	log_flag(SWITCH, "CXI services=%d system=%d user=%d",
		 list->count, num_system_svc, num_svc);

	free(list);	/* can't use xfree() */
	return true;
}

/*
 * Set up basic access to the CXI devices in the daemon
 */
static bool _create_cxi_devs(slingshot_jobinfo_t *job)
{
	struct cxil_device_list *list;
	int dev, rc;

	if ((rc = cxil_get_device_list_p(&list))) {
		error("Could not get a list of the CXI devices: %s",
		      strerror(-rc));
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
	for (dev = 0; dev < cxi_ndevs; dev++) {
		struct cxil_devinfo *info = &list->info[dev];
		if ((rc = cxil_open_device_p(info->dev_id, &cxi_devs[dev]))) {
			error("Could not open CXI device dev_id=%d (%s): %s",
			      info->dev_id, info->device_name, strerror(-rc));
			cxi_devs[dev] = NULL;
			continue;
		}
		/* Only done in debug mode */
		if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH)
			_print_devinfo(dev, &cxi_devs[dev]->info);
		/*
		 * If configured, adjust max NIC resources available
		 * by subtracting system service reserved/used values
		 */
		if (job->flags & SLINGSHOT_FLAGS_ADJUST_LIMITS)
			_adjust_dev_limits(dev, &cxi_devs[dev]->info);
	}

	return true;
}

/*
 * Return a cxi_limits struct with res/max fields set according to
 * job max/res/def limits, device max limits, and number of CPUs on node
 */
static struct cxi_limits _set_desc_limits(int rsrc,
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
	/*
	 * SPECIAL CASE: limit TLE max value to reserved value
	 */
	if (rsrc == CXI_RSRC_TYPE_TLE)
		ret.max = ret.res;
	const char *name = cxi_rsrc_type_strs[rsrc];
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

	desc->restricted_members = true;
	desc->members[0].type = CXI_SVC_MEMBER_UID;
	desc->members[0].svc_member.uid = uid;
	desc->members[1].type = CXI_SVC_MEMBER_IGNORE;

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
	desc->limits.txqs = _set_desc_limits(CXI_RSRC_TYPE_TXQ,
				&job->limits.txqs, devinfo->num_txqs, cpus);
	desc->limits.tgqs = _set_desc_limits(CXI_RSRC_TYPE_TGQ,
				&job->limits.tgqs, devinfo->num_tgqs, cpus);
	desc->limits.eqs = _set_desc_limits(CXI_RSRC_TYPE_EQ,
				&job->limits.eqs, devinfo->num_eqs, cpus);
	desc->limits.cts = _set_desc_limits(CXI_RSRC_TYPE_CT,
				&job->limits.cts, devinfo->num_cts, cpus);
	desc->limits.tles = _set_desc_limits(CXI_RSRC_TYPE_TLE,
				&job->limits.tles, devinfo->num_tles, cpus);
	desc->limits.ptes = _set_desc_limits(CXI_RSRC_TYPE_PTE,
				&job->limits.ptes, devinfo->num_ptes, cpus);
	desc->limits.les = _set_desc_limits(CXI_RSRC_TYPE_LE,
				&job->limits.les, devinfo->num_les, cpus);
	desc->limits.acs = _set_desc_limits(CXI_RSRC_TYPE_AC,
				&job->limits.acs, devinfo->num_acs, cpus);

	/* Differentiates system and user services */
	desc->is_system_svc = false;
}

/*
 * Open the Slingshot CXI library; set up functions and set cxi_avail
 * if successful (default is 'false')
 */
extern bool slingshot_open_cxi_lib(slingshot_jobinfo_t *job)
{
	if (!(cxi_handle = dlopen(HPE_SLINGSHOT_LIB,
				  RTLD_LAZY | RTLD_GLOBAL))) {
		error("Couldn't find CXI library %s: %s",
		      HPE_SLINGSHOT_LIB, dlerror());
		goto out;
	}

	if (!_load_cxi_funcs(cxi_handle))
		goto out;

	if (!_create_cxi_devs(job))
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
 * Open, write-lock, and read 'fname'; convert the (hex string) contents
 * of the file to a bitstr_t and return in *retbitsp;
 * also fill in the file size in *fsizep.
 * Returns the file descriptor on success, or -1 on failure
 */
static int _read_bitstr_file(const char *fname, bitstr_t **retbitsp,
			     size_t *fsizep)
{
	int fd, rc;
	size_t fsize;
	struct flock lock;
	struct stat statbuf;

	fd = open(fname, (O_RDWR | O_CREAT), 0644);
	if (fd == -1) {
		error("open(%s) failed: %m", fname);
		return -1;
	}

	/*
	 * Exclusive lock on the first byte of the file
	 * Automatically released when the file descriptor is closed
	 */
	lock.l_type = F_WRLCK;
	lock.l_whence = SEEK_SET;
	lock.l_start = 0;
	lock.l_len = 1;
	rc = fcntl(fd, F_SETLKW, &lock);
	if (rc == -1) {
		error("fcntl(F_SETLKW, %s) failed: %m", fname);
		goto errout;
	}

	/* Read in the whole file, convert to bitstr_t */
	if (fstat(fd, &statbuf) == -1) {
		error("fstat(%s) failed: %m", fname);
		goto errout;
	}
	fsize = statbuf.st_size;
	if (fsize > SLINGSHOT_VNI_PIDS_BUFSIZ) {
		error("%s too large: %zu vs max %d",
			fname, fsize, SLINGSHOT_VNI_PIDS_BUFSIZ);
		goto errout;
	}
	*retbitsp = bit_alloc(SLINGSHOT_VNI_PIDS);
	/* Non-empty file; read it in, convert to bitstr_t */
	if (fsize > 0) {
		char buf[SLINGSHOT_VNI_PIDS_BUFSIZ];
		ssize_t sret = read(fd, buf, fsize);
		if (sret == -1) {
			error("read(%s, %zu) failed: %m", fname, fsize);
			goto errout;
		} else if (sret != fsize) {
			error("read(%s, %zu) ret %zu: %m", fname, fsize, sret);
			goto errout;
		}
		log_flag(SWITCH, "read(%s, %zu) contents='%s'",
			fname, fsize, buf);
		/* Convert from hex bitmask to bitstr_t */
		if (bit_unfmt_hexmask(*retbitsp, buf) == -1) {
			error("%s: invalid VNI pids bitmask '%s'", fname, buf);
			goto errout;
		}
	}
	*fsizep = fsize;
	return fd;
errout:
	close(fd);
	return -1;
}

/*
 * Convert the bitstr_t into a hex string and replace the contents of the file
 * with name 'fname', file descriptor 'fd', and size 'oldsize';
 * if the bitstring has no bits set, unlink the file
 */
static bool _write_bitstr_file(const char *fname, int fd, size_t oldsize,
			       bitstr_t *bitstr)
{
	char *mask = NULL;
	bool retval = false;
	/*
	 * If bitstr has any bits set, convert it to a hex string,
	 * then overwrite the file; if no bits left, remove file
	 */
	if (bit_ffs(bitstr) >= 0) {
		size_t fsize;
		ssize_t sret;
		mask = bit_fmt_hexmask_trim(bitstr);
		 fsize = strlen(mask) + 1;	/* write NULL on end */
		xassert(fsize <= SLINGSHOT_VNI_PIDS_BUFSIZ);
		if (fsize < oldsize) {
			if (ftruncate(fd, 0) == -1) {
				error("ftruncate(%s) failed: %m", fname);
				goto out;
			}
		}
		sret = pwrite(fd, mask, fsize, 0);
		log_flag(SWITCH, "pwrite(%s, '%s', %zu)", fname, mask, fsize);
		if (sret == -1) {
			error("pwrite(%d, %zu) failed: %m", fd, fsize);
			goto out;
		} else if (sret != fsize) {
			error("pwrite(%d, %zu) ret %zu: %m", fd, fsize, sret);
			goto out;
		}
	} else {
		log_flag(SWITCH, "unlink(%s)", fname);
		if (unlink(fname) == -1) {
			error("unlink(%s) failed: %m", fname);
			goto out;
		}
	}
	retval = true;
out:
	xfree(mask);
	return retval;
}

/*
 * If this job step has a job VNI, allocate a set of Slingshot "PIDs"
 * (a set of 'step_cpus' numbers 0-255 that are disjoint from the sets
 * used by any other job step in the job on this node).
 * Write a hex representation of this set into a job-specific file.
 * Returns true/false on success/failure.
 */
static bool _alloc_vni_pids(slingshot_jobinfo_t *step, int step_cpus,
			    uint32_t job_id)
{
	int bit, fd = -1, num_bits = 0;
	char *fname = NULL;
	size_t fsize;
	bitstr_t *job_vni_pids = NULL;
	bool retval = false;	/* assume failure */

	/* Return if VNI PIDs not configured, or no job VNI */
	if (!(step->flags & SLINGSHOT_FLAGS_VNI_PIDS) || step->num_vnis < 2)
		return true;

	/* Create filename for this job's allocated VNI PIDs */
	xstrfmtcat(fname, SLINGSHOT_VNI_PIDS_FMT,
		   slurm_conf.slurmd_spooldir, job_id);
	log_flag(SWITCH, "job_id=%u cpus=%d fname=%s",
		 job_id, step_cpus, fname);
	fd = _read_bitstr_file(fname, &job_vni_pids, &fsize);
	if (fd == -1)
		goto out;

	/* Allocate step_cpus bits from job_vni_pids, add to step->vni_pids */
	xassert(step->vni_pids == NULL);
	step->vni_pids = bit_alloc(SLINGSHOT_VNI_PIDS);
	if (fsize == 0) {
		/*
		 * File just created, must be the first step;
		 * allocate step_cpus bits from bitmask
		 */
		bit_nset(job_vni_pids, 0, step_cpus - 1);
		bit_nset(step->vni_pids, 0, step_cpus - 1);
		log_flag(SWITCH, "cpus=%d startbit=0", step_cpus);
	} else if ((bit = bit_nffc(job_vni_pids, step_cpus)) >= 0) {
		/*
		 * Subsequent job steps:
		 * allocate step_cpus free bits from per-job bitstring
		 * First try for configuous bits
		 */
		bit_nset(job_vni_pids, bit, bit + step_cpus - 1);
		bit_nset(step->vni_pids, bit, bit + step_cpus - 1);
		log_flag(SWITCH, "cpus=%d startbit=%d", step_cpus, bit);
	} else {
		/* Not enough contiguous bits: alloc a bit at a time */
		for (num_bits = 0; num_bits < step_cpus; num_bits++) {
			if ((bit = bit_ffc(job_vni_pids)) == -1)
				break;
			bit_set(job_vni_pids, bit);
			bit_set(step->vni_pids, bit);
		}
		log_flag(SWITCH, "got num_bits %d/%d", num_bits, step_cpus);
		if (num_bits < step_cpus)
			goto out;
	}

	/* Write the new bitstring out to the file */
	if (!_write_bitstr_file(fname, fd, fsize, job_vni_pids))
		goto out;

	retval = true;
out:
	if (job_vni_pids)
		bit_free(job_vni_pids);
	if (fd >= 0)
		close(fd);
	xfree(fname);
	return retval;
}

/*
 * If this job step has a set of VNI "PIDs", remove them from
 * the set of allocated "PIDs" for this job.
 * Returns true/false on success/failure.
 */
static bool _free_vni_pids(slingshot_jobinfo_t *step, uint32_t job_id)
{
	bool retval = false;	/* assume failure */
	bitstr_t *job_vni_pids = NULL;
	int fd = -1;
	char *fname = NULL;
	size_t fsize;

	/* Return if no VNI PIDs */
	if (!step->vni_pids)
		return true;

	/* Create filename for this job's allocated VNI PIDs */
	xstrfmtcat(fname, SLINGSHOT_VNI_PIDS_FMT,
		   slurm_conf.slurmd_spooldir, job_id);
	log_flag(SWITCH, "job_id=%u fname=%s", job_id, fname);
	fd = _read_bitstr_file(fname, &job_vni_pids, &fsize);
	if (fd == -1)
		goto out;

	/* Remove job step's VNI PIDs from job's VNI PIDS */
	xassert(bit_super_set(step->vni_pids, job_vni_pids) == 1);
	bit_and_not(job_vni_pids, step->vni_pids);
	if (slurm_conf.debug_flags & DEBUG_FLAG_SWITCH) {
		char *jpids = bit_fmt_full(job_vni_pids);
		char *spids = bit_fmt_full(step->vni_pids);
		log_flag(SWITCH, "free: step %s remaining %s",
			spids, jpids);
		xfree(spids);
		xfree(jpids);
	}

	/* Write the new bitstring out to the file */
	if (!_write_bitstr_file(fname, fd, fsize, job_vni_pids))
		goto out;

	retval = true;
out:
	if (job_vni_pids)
		bit_free(job_vni_pids);
	if (fd >= 0)
		close(fd);
	xfree(fname);
	return retval;
}

/*
 * Attempt to destroy a CXI service; retry a few times on EBUSY
 */
static bool _destroy_cxi_service(struct cxil_dev *dev, const char *devname,
				 int svc_id)
{
	int i, rc;

	for (i = 0; i < SLINGSHOT_CXI_DESTROY_RETRIES; i++) {
		debug("Destroying CXI SVC ID %d on NIC %s (retry %d)",
		      svc_id, devname, i);
		rc = cxil_destroy_svc_p(dev, svc_id);
		if (rc == 0)
			return true;
		error("Failed to destroy CXI Service ID %d (%s): %s",
		      svc_id, devname, strerror(-rc));
		if (rc != -EBUSY)
			break;
		sleep(1);
	}
	return false;
}

/*
 * In the daemon, when the shepherd for an App terminates, free any CXI
 * Services we have allocated for it
 */
extern bool slingshot_destroy_services(slingshot_jobinfo_t *job,
				       uint32_t job_id)
{
	bool retval = true;

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

		/* Try to destroy service (with retries) */
		if (!_destroy_cxi_service(dev, devname, svc_id))
			retval = false;
	}

	xfree(job->profiles);
	job->profiles = NULL;
	job->num_profiles = 0;

	/* De-allocate this step's VNI "PIDs" from job's set */
	if (job->vni_pids) {
		if (!_free_vni_pids(job, job_id))
			retval = false;
		bit_free(job->vni_pids);
		job->vni_pids = NULL;
	}

	return retval;
}

/*
 * Log any non-system CXI services, to help with diagnosing allocation failures
 */
static void _log_other_services(struct cxil_dev *dev)
{
	struct cxil_svc_list *list = NULL;
	int rc;

	if ((rc = cxil_get_svc_list(dev, &list))) {
		error("Could not get service list for CXI dev_id=%d (%s): %s",
		      dev->info.dev_id, dev->info.device_name, strerror(-rc));
		return;
	}
	for (int svc = 0; svc < list->count; svc++) {
		if (list->descs[svc].is_system_svc)
			continue;
		error("CXI allocation failed for %s: svc_id=%d UID=%d also on device",
		      dev->info.device_name, list->descs[svc].svc_id,
		      list->descs[svc].members[0].svc_member.uid);
	}
	free(list);	/* can't use xfree() */
}

/*
 * If cxil_alloc_svc failed, log information about the failure
 */
static void _alloc_fail_info(struct cxil_dev *dev,
			     struct cxi_svc_desc *desc,
			     struct cxi_svc_fail_info *fail_info)
{
	for (int rsrc = 0; rsrc < CXI_RSRC_TYPE_MAX; rsrc++) {
		const char *rsrc_str = cxi_rsrc_type_strs[rsrc];
		int rsrc_res = desc->limits.type[rsrc].res;
		int rsrc_max = desc->limits.type[rsrc].max;

		if (rsrc_res > fail_info->rsrc_avail[rsrc])
			error("%s: allocation failed: max/available/requested %d %d %d",
			      rsrc_str, rsrc_max, fail_info->rsrc_avail[rsrc],
			      rsrc_res);
	}

	if (fail_info->no_le_pools)
		error("No LE pools available on %s", dev->info.device_name);
	if (fail_info->no_tle_pools)
		error("No TLE pools available on %s", dev->info.device_name);
	if (fail_info->no_cntr_pools)
		error("No CNTR pools available on %s", dev->info.device_name);

	/* log any other non-system services on this node */
	_log_other_services(dev);
}

/*
 * Set up CXI services for each of the CXI NICs on this host
 */
extern bool slingshot_create_services(slingshot_jobinfo_t *job, uint32_t uid,
				      uint16_t step_cpus, uint32_t job_id)
{
	int prof, devn;
	struct cxi_svc_desc desc;
	struct cxil_dev *dev;
	struct cxi_svc_fail_info failinfo;
	slingshot_comm_profile_t *profile;

	xassert(job);

	/* dlopen() libcxi and query CXI devices */
	slingshot_open_cxi_lib(job);

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
			error("Slingshot service allocation failed on %s: %s",
			      dev->info.device_name, strerror(-svc_id));
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
			" SVC ID %u vnis=[%hu %hu %hu %hu] tcs=%#x",
			prof, devn, profile->device_name, profile->svc_id,
			profile->vnis[0], profile->vnis[1], profile->vnis[2],
			profile->vnis[3], profile->tcs);
		prof++;
	}

	/*
	 * If this job step has a job VNI, allocate a set of "PIDs" (0-255)
	 * so the MPI library can distinguish between job steps within the job
	 */
	if (!_alloc_vni_pids(job, step_cpus, job_id))
		goto error;

	return true;

error:
	slingshot_destroy_services(job, job_id);

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
