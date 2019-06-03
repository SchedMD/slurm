/*****************************************************************************\
 *  apinfo.h - Cray Shasta PMI apinfo file creation
 *****************************************************************************
 *  Copyright 2019 Cray Inc. All Rights Reserved.
 *  Written by David Gloe <dgloe@cray.com>
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

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>

#include "src/common/xstring.h"

#include "apinfo.h"

/* Application file format version */
#define PALS_APINFO_VERSION 1

/* File header structure */
typedef struct {
	int version;
	size_t total_size;
	size_t comm_profile_size;
	size_t comm_profile_offset;
	int ncomm_profiles;
	size_t cmd_size;
	size_t cmd_offset;
	int ncmds;
	size_t pe_size;
	size_t pe_offset;
	int npes;
	size_t node_size;
	size_t node_offset;
	int nnodes;
	size_t nic_size;
	size_t nic_offset;
	int nnics;
} pals_header_t;

/* Network communication profile structure */
typedef struct {
	char tokenid[40];    /* Token UUID */
	int vni;             /* VNI associated with this token */
	int vlan;            /* VLAN associated with this token */
	int traffic_classes; /* Bitmap of allowed traffic classes */
} pals_comm_profile_t;

/* MPMD command information structure */
typedef struct {
	int npes;         /* Number of PEs in this command */
	int pes_per_node; /* Number of PEs per node */
	int cpus_per_pe;  /* Number of CPUs per PE */
} pals_cmd_t;

/* PE (i.e. task) information structure */
typedef struct {
	int localidx; /* Node-local PE index */
	int cmdidx;   /* Command index for this PE */
	int nodeidx;  /* Node index this PE is running on */
} pals_pe_t;

/* Node information structure */
typedef struct {
	int nid;           /* Node ID */
	char hostname[64]; /* Node hostname */
} pals_node_t;

/* NIC address type */
typedef enum {
	PALS_ADDR_IPV4,
	PALS_ADDR_IPV6,
	PALS_ADDR_MAC
} pals_address_type_t;

/* NIC information structure */
typedef struct {
	int nodeidx;                      /* Node index this NIC belongs to */
	pals_address_type_t address_type; /* Address type for this NIC */
	char address[40];                 /* Address of this NIC */
} pals_nic_t;

/*
 * Get a NID from a hostname, in format nidXXXXXX.
 * Trailing characters are ignored.
 * Returns -1 if the hostname is not in the expected format.
 */
static int _get_nid(const char *hostname)
{
	int nid = -1;
	if (sscanf(hostname, "nid%d", &nid) < 1 || nid < 0) {
		return -1;
	}
	return nid;
}

/*
 * Return an array of pals_pe_t structures. Returns NULL on error.
 */
static pals_pe_t *_setup_pals_pes(const stepd_step_rec_t *job)
{
	pals_pe_t *pes = NULL;
	int nodeidx, localidx, taskid;

	assert(job->ntasks > 0);

	pes = xmalloc(job->ntasks * sizeof(pals_pe_t));
	for (nodeidx = 0; nodeidx < job->nnodes; nodeidx++) {
		for (localidx = 0;
		     localidx < job->msg->tasks_to_launch[nodeidx];
		     localidx++) {
			taskid = job->msg->global_task_ids[nodeidx][localidx];
			assert(taskid < job->ntasks);
			pes[taskid].nodeidx = nodeidx;
			pes[taskid].localidx = localidx;

			// TODO: MPMD support
			pes[taskid].cmdidx = 0;
		}
	}
	return pes;
}

/*
 * Fill in the apinfo header
 */
static void _build_header(pals_header_t *hdr, const stepd_step_rec_t *job)
{
	size_t offset = sizeof(pals_header_t);

	memset(hdr, 0, sizeof(pals_header_t));
	hdr->version = PALS_APINFO_VERSION;

	hdr->comm_profile_size = sizeof(pals_comm_profile_t);
	hdr->comm_profile_offset = offset;
	hdr->ncomm_profiles = 0;
	offset += hdr->comm_profile_size * hdr->ncomm_profiles;

	hdr->cmd_size = sizeof(pals_cmd_t);
	hdr->cmd_offset = offset;
	hdr->ncmds = 1;
	offset += hdr->cmd_size * hdr->ncmds;

	hdr->pe_size = sizeof(pals_pe_t);
	hdr->pe_offset = offset;
	hdr->npes = job->ntasks;
	offset += hdr->pe_size * hdr->npes;

	hdr->node_size = sizeof(pals_node_t);
	hdr->node_offset = offset;
	hdr->nnodes = job->nnodes;
	offset += hdr->node_size * hdr->nnodes;

	hdr->nic_size = sizeof(pals_nic_t);
	hdr->nic_offset = offset;
	hdr->nnics = 0;
	offset += hdr->nic_size * hdr->nnics;

	hdr->total_size = offset;
}

/*
 * Open the apinfo file and return a writeable fd, or -1 on failure
 */
static int _open_apinfo(const stepd_step_rec_t *job)
{
	int fd = -1;

	// Create apinfo name - put in per-application spool directory
	xstrfmtcat(apinfo, "%s/apinfo", appdir);

	// Create file
	fd = creat(apinfo, 0600);
	if (fd == -1) {
		error("mpi/cray_shasta: Couldn't open apinfo file %s: %m",
		      apinfo);
		close(fd);
		return -1;
	}

	// Change ownership of file to application user
	if (fchown(fd, job->uid, job->gid) == -1 && getuid() == 0) {
		error("mpi/cray_shasta: Couldn't chown %s to uid %d gid %d: %m",
		      apinfo, job->uid, job->gid);
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * Write the job's node list to the file
 */
static int _write_pals_nodes(const stepd_step_rec_t *job, int fd)
{
	hostlist_t hl;
	char *host;
	pals_node_t node;

	memset(&node, 0, sizeof(pals_node_t));

	hl = hostlist_create(job->msg->complete_nodelist);
	if (hl == NULL) {
		error("mpi/cray: Couldn't create hostlist");
		return SLURM_ERROR;
	}
	while ((host = hostlist_shift(hl)) != NULL) {
		snprintf(node.hostname, sizeof(node.hostname), "%s", host);
		node.nid = _get_nid(host);
		safe_write(fd, &node, sizeof(pals_node_t));
	}
rwfail:
	hostlist_destroy(hl);
	return SLURM_SUCCESS;
}

/*
 * Write the application information file
 */
extern int create_apinfo(const stepd_step_rec_t *job)
{
	int fd = -1;
	pals_header_t hdr;
	pals_cmd_t cmd;
	pals_pe_t *pes = NULL;

	// Make sure the application spool directory has been created
	if (appdir == NULL) {
		return SLURM_ERROR;
	}

	// Get information to write
	_build_header(&hdr, job);

	// Make sure we've got everything
	if (ntasks <= 0) {
		error("mpi/cray_shasta: no tasks found");
		goto rwfail;
	}
	if (ncmds <= 0) {
		error("mpi/cray_shasta: no cmds found");
		goto rwfail;
	}
	if (nnodes <= 0) {
		error("mpi/cray_shasta: no nodes found");
		goto rwfail;
	}
	if (task_cnts == NULL) {
		error("mpi/cray_shasta: no per-node task counts");
		goto rwfail;
	}
	if (tids == NULL) {
		error("mpi/cray_shasta: no task IDs found");
		goto rwfail;
	}
	if (nodelist == NULL) {
		error("mpi/cray_shasta: no nodelist found");
		goto rwfail;
	}

	pes = _setup_pals_pes(job);
	if (pes == NULL) {
		return SLURM_ERROR;
	}

	// Create the file
	fd = _open_apinfo(job);
	if (fd == -1) {
		goto rwfail;
	}

	// Write info
	safe_write(fd, &hdr, sizeof(pals_header_t));
	safe_write(fd, cmds, (hdr.ncmds * sizeof(pals_cmd_t)));
	safe_write(fd, pes, (hdr.npes * sizeof(pals_pe_t)));

	if (_write_pals_nodes(fd, nodelist) == SLURM_ERROR)
		goto rwfail;

	// TODO: Write communication profiles
	// TODO write nics

	// Flush changes to disk
	if (fsync(fd) == -1) {
		error("mpi/cray_shasta: Couldn't sync %s to disk: %m", apinfo);
		goto rwfail;
	}

	debug("mpi/cray_shasta: Wrote apinfo file %s", apinfo);

	// Clean up and return
	xfree(pes);
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	if (job->flags & LAUNCH_MULTI_PROG) {
		xfree(tid_offsets);
	}
	xfree(pes);
	xfree(cmds);
	close(fd);
	return SLURM_ERROR;
}
