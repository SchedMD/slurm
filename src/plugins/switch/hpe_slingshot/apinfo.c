/*****************************************************************************\
 *  apinfo.c - Write Slingshot information for Cray PMI
 *****************************************************************************
 *  Copyright 2022 Hewlett Packard Enterprise Development LP
 *  Written by David Gloe <david.gloe@hpe.com>
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

#include <sys/stat.h>
#include <sys/types.h>

#include "src/common/read_config.h"
#include "src/common/xstring.h"
#include "src/plugins/mpi/cray_shasta/apinfo.h"

#include "switch_hpe_slingshot.h"

/*
 * Create the HPE Slingshot directory under the slurmd spool directory
 */
static int _create_slingshot_dir(const char *spool)
{
	char *slingshotdir = NULL;

	slingshotdir = xstrdup_printf("%s/%s", spool, HPE_SLINGSHOT_DIR);
	if ((mkdir(slingshotdir, 0755) == -1) && (errno != EEXIST)) {
		error("%s: Couldn't create HPE Slingshot directory %s: %m",
		      plugin_type, slingshotdir);
		xfree(slingshotdir);
		return false;
	}
	xfree(slingshotdir);
	return true;
}

/*
 * Get the Slingshot apinfo file name
 */
static char *_get_apinfo_file(const stepd_step_rec_t *step, char *spool)
{
	return xstrdup_printf("%s/%s/apinfo.%u.%u", spool, HPE_SLINGSHOT_DIR,
			      step->step_id.job_id, step->step_id.step_id);
}

/*
 * Fill in the apinfo header
 */
static void _build_header(pals_header_t *hdr, slingshot_jobinfo_t *jobinfo)
{
	size_t offset = sizeof(pals_header_t);

	memset(hdr, 0, sizeof(pals_header_t));
	hdr->version = PALS_APINFO_VERSION;

	hdr->comm_profile_size = sizeof(pals_comm_profile_t);
	hdr->comm_profile_offset = offset;
	hdr->ncomm_profiles = jobinfo->num_profiles;
	offset += hdr->comm_profile_size * hdr->ncomm_profiles;

	hdr->nic_size = sizeof(pals_hsn_nic_t);
	hdr->nic_offset = offset;
	hdr->nnics = jobinfo->num_nics;
	offset += hdr->nic_size * hdr->nnics;

	// Don't support NIC distances yet
	hdr->dist_size = 0;
	hdr->dist_offset = 0;

	hdr->total_size = offset;
}

/*
 * Convert to the apinfo comm profile structure
 */
static void _comm_profile_convert(slingshot_comm_profile_t *ss_profile,
				  pals_comm_profile_t *profile)
{
	memset(profile, 0, sizeof(pals_comm_profile_t));
	profile->svc_id = ss_profile->svc_id;
	profile->traffic_classes = ss_profile->tcs;
	memcpy(profile->vnis, ss_profile->vnis, sizeof(profile->vnis));
	profile->nvnis = ss_profile->vnis_used;
	memcpy(profile->device_name, ss_profile->device_name,
	       sizeof(profile->device_name));
}

/*
 * Convert to the apinfo HSN NIC information structure (for Instant On)
 */
static void _hsn_nic_convert(slingshot_hsn_nic_t *ss_nic, pals_hsn_nic_t *nic)
{
	memset(nic, 0, sizeof(pals_hsn_nic_t));
	nic->nodeidx = ss_nic->nodeidx;
	if (ss_nic->address_type == SLINGSHOT_ADDR_MAC)
		nic->address_type = PALS_ADDR_MAC;
	else if (ss_nic->address_type == SLINGSHOT_ADDR_IPV4)
		nic->address_type = PALS_ADDR_IPV4;
	else
		nic->address_type = PALS_ADDR_IPV6;
	memcpy(nic->address, ss_nic->address, sizeof(nic->address));
	nic->numa_node = ss_nic->numa_node;
	memcpy(nic->device_name, ss_nic->device_name, sizeof(nic->device_name));
}

/*
 * Write the application information file
 */
extern bool create_slingshot_apinfo(const stepd_step_rec_t *step)
{
	int fd = -1;
	pals_header_t hdr;
	slingshot_jobinfo_t *jobinfo = step->switch_job->data;
	char *spool = NULL;
	char *apinfo = NULL;

	/* Get the filename */
	spool = slurm_conf_expand_slurmd_path(slurm_conf.slurmd_spooldir,
					      step->node_name, step->node_name);
	if (!_create_slingshot_dir(spool)) {
		xfree(spool);
		return false;
	}
	apinfo = _get_apinfo_file(step, spool);

	/* Create the file */
	fd = creat(apinfo, 0600);
	if (fd == -1) {
		error("%s: Couldn't create %s: %m", plugin_type, apinfo);
		xfree(apinfo);
		xfree(spool);
		return false;
	}

	/* Write header */
	_build_header(&hdr, jobinfo);
	safe_write(fd, &hdr, sizeof(pals_header_t));

	/* Write communication profiles */
	for (int i = 0; i < jobinfo->num_profiles; i++) {
		pals_comm_profile_t profile;
		_comm_profile_convert(&jobinfo->profiles[i], &profile);
		safe_write(fd, &profile, sizeof(pals_comm_profile_t));
	}

	/* Write Instant On data */
	for (int i = 0; i < jobinfo->num_nics; i++) {
		pals_hsn_nic_t nic;
		_hsn_nic_convert(&jobinfo->nics[i], &nic);
		safe_write(fd, &nic, sizeof(pals_hsn_nic_t));
	}

	debug("%s: Wrote %s", plugin_type, apinfo);

	close(fd);
	xfree(apinfo);
	xfree(spool);
	return true;

rwfail:
	close(fd);
	unlink(apinfo);
	xfree(apinfo);
	xfree(spool);
	return false;
}

/*
 * Remove the Slingshot apinfo file
 */
extern void remove_slingshot_apinfo(const stepd_step_rec_t *step)
{
	char *apinfo, *spool;

	spool = slurm_conf_expand_slurmd_path(slurm_conf.slurmd_spooldir,
					      step->node_name, step->node_name);
	apinfo = _get_apinfo_file(step, spool);

	if (unlink(apinfo) == -1) {
		error("%s: Couldn't unlink %s: %m", plugin_type, apinfo);
	} else {
		debug("%s: Removed %s", plugin_type, apinfo);
	}

	xfree(apinfo);
	xfree(spool);
}
