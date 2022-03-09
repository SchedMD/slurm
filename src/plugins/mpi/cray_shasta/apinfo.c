/*****************************************************************************\
 *  apinfo.c - Cray Shasta PMI apinfo file creation
 *****************************************************************************
 *  Copyright 2019 Hewlett Packard Enterprise Development LP
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
 * Parse an MPMD file to determine the number of MPMD commands and task->cmd
 * mapping. Adopted from multi_prog_parse in src/slurmd/slurmstepd/multi_prog.c.
 *
 * The file's contents are stored in job->argv[1], and follow this format:
 * <taskids> <command> <arguments>
 *
 * taskids is a range list of task IDs or * (for all remaining task IDs).
 * command and arguments give the argv to run for those tasks.
 * Empty lines and lines starting with # are ignored.
 * Newlines may be escaped with \.
 */
static void _multi_prog_parse(const stepd_step_rec_t *job, int *ncmds,
			      uint32_t **tid_offsets)
{
	int i = 0, line_num = 0, rank_id = 0, num_cmds = 0, nranks = 0;
	char *line = NULL, *local_data = NULL;
	char *end_ptr = NULL, *save_ptr = NULL, *tmp_str = NULL;
	char *rank_spec = NULL, *p = NULL, *one_rank = NULL;
	hostlist_t hl;
	uint32_t *offsets = NULL;

	offsets = xcalloc(job->ntasks, sizeof(uint32_t));
	for (i = 0; i < job->ntasks; i++) {
		offsets[i] = NO_VAL;
	}

	// Copy contents of MPMD file so we can tokenize it
	local_data = xstrdup(job->argv[1]);

	// Replace escaped newlines with spaces
	while ((p = xstrstr(local_data, "\\\n"))) {
		p[0] = ' ';
		p[1] = ' ';
	}

	while (1) {
		// Get the next line
		if (line_num)
			line = strtok_r(NULL, "\n", &save_ptr);
		else
			line = strtok_r(local_data, "\n", &save_ptr);
		if (!line)
			break;
		line_num++;

		// Get task IDs from the line
		p = line;
		while ((*p != '\0') && isspace(*p)) /* remove leading spaces */
			p++;
		if (*p == '#') /* only whole-line comments handled */
			continue;
		if (*p == '\0') /* blank line ignored */
			continue;

		rank_spec = p; /* Rank specification for this line */
		while ((*p != '\0') && !isspace(*p))
			p++;
		if (*p == '\0')
			goto fail;
		*p++ = '\0';

		while ((*p != '\0') && isspace(*p)) /* remove leading spaces */
			p++;
		if (*p == '\0') /* blank line ignored */
			continue;

		nranks = 0;
		// If rank_spec is '*', set all remaining ranks to this cmd
		if (!xstrcmp(rank_spec, "*")) {
			for (i = 0; i < job->ntasks; i++) {
				if (offsets[i] == NO_VAL) {
					offsets[i] = num_cmds;
					nranks++;
				}
			}
		} else {
			// Parse rank list into individual ranks
			tmp_str = xstrdup_printf("[%s]", rank_spec);
			hl = hostlist_create(tmp_str);
			xfree(tmp_str);
			if (!hl)
				goto fail;
			while ((one_rank = hostlist_pop(hl))) {
				rank_id = strtol(one_rank, &end_ptr, 10);
				if ((end_ptr[0] != '\0') || (rank_id < 0) ||
				    (rank_id >= job->ntasks)) {
					hostlist_destroy(hl);
					error("%s: invalid rank id %s",
					      plugin_type, one_rank);
					free(one_rank);
					goto fail;
				}
				free(one_rank);

				offsets[rank_id] = num_cmds;
				nranks++;
			}
			hostlist_destroy(hl);
		}
		// Only count this command if it had at least one rank
		if (nranks > 0) {
			num_cmds++;
		}
	}

	// Make sure we've initialized all ranks
	for (i = 0; i < job->ntasks; i++) {
		if (offsets[i] == NO_VAL) {
			error("%s: no command for task id %d", plugin_type, i);
			goto fail;
		}
	}

	xfree(local_data);
	*ncmds = num_cmds;
	*tid_offsets = offsets;
	return;

fail:
	xfree(offsets);
	xfree(local_data);
	*ncmds = 0;
	*tid_offsets = NULL;
	return;
}

/*
 * Return an array of pals_pe_t structures.
 */
static pals_pe_t *_setup_pals_pes(int ntasks, int nnodes, uint16_t *task_cnts,
				  uint32_t **tids, uint32_t *tid_offsets)
{
	pals_pe_t *pes = NULL;
	int nodeidx, localidx, taskid;

	pes = xcalloc(ntasks, sizeof(pals_pe_t));
	for (nodeidx = 0; nodeidx < nnodes; nodeidx++) {
		for (localidx = 0; localidx < task_cnts[nodeidx]; localidx++) {
			taskid = tids[nodeidx][localidx];
			if (taskid >= ntasks) {
				error("%s: task %d node %d >= ntasks %d; skipping",
				      plugin_type, taskid, nodeidx, ntasks);
				continue;
			}
			pes[taskid].nodeidx = nodeidx;
			pes[taskid].localidx = localidx;

			if (!tid_offsets) {
				pes[taskid].cmdidx = 0;
			} else {
				pes[taskid].cmdidx = tid_offsets[taskid];
			}
		}
	}
	return pes;
}

/*
 * Return an array of pals_cmd_t structures.
 */
static pals_cmd_t *_setup_pals_cmds(int ncmds, int ntasks, int nnodes,
				    int cpus_per_task, pals_pe_t *pes)
{
	pals_cmd_t *cmds;
	int peidx, cmdidx, nodeidx, max_ppn;
	int **cmd_ppn;

	// Allocate and initialize arrays
	cmds = xcalloc(ncmds, sizeof(pals_cmd_t));
	cmd_ppn = xcalloc(ncmds, sizeof(int *));
	for (cmdidx = 0; cmdidx < ncmds; cmdidx++) {
		cmd_ppn[cmdidx] = xcalloc(nnodes, sizeof(int));
	}

	// Count number of PEs for each command/node
	for (peidx = 0; peidx < ntasks; peidx++) {
		cmdidx = pes[peidx].cmdidx;
		nodeidx = pes[peidx].nodeidx;
		if (cmdidx >= 0 && cmdidx < ncmds && nodeidx >= 0 &&
		    nodeidx < nnodes) {
			cmd_ppn[cmdidx][nodeidx]++;
		}
	}

	// Fill in command information
	for (cmdidx = 0; cmdidx < ncmds; cmdidx++) {
		// NOTE: we don't know each job's depth for a heterogeneous job
		cmds[cmdidx].cpus_per_pe = cpus_per_task;

		// Find the total PEs and max PEs/node for this command
		max_ppn = 0;
		for (nodeidx = 0; nodeidx < nnodes; nodeidx++) {
			cmds[cmdidx].npes += cmd_ppn[cmdidx][nodeidx];
			if (cmd_ppn[cmdidx][nodeidx] > max_ppn) {
				max_ppn = cmd_ppn[cmdidx][nodeidx];
			}
		}
		xfree(cmd_ppn[cmdidx]);

		cmds[cmdidx].pes_per_node = max_ppn;
	}

	xfree(cmd_ppn);
	return cmds;
}

/*
 * Fill in the apinfo header
 */
static void _build_header(pals_header_t *hdr, int ncmds, int npes, int nnodes)
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
	hdr->ncmds = ncmds;
	offset += hdr->cmd_size * hdr->ncmds;

	hdr->pe_size = sizeof(pals_pe_t);
	hdr->pe_offset = offset;
	hdr->npes = npes;
	offset += hdr->pe_size * hdr->npes;

	hdr->node_size = sizeof(pals_node_t);
	hdr->node_offset = offset;
	hdr->nnodes = nnodes;
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

	xfree(apinfo);
	// Create apinfo name - put in per-application spool directory
	apinfo = xstrdup_printf("%s/apinfo", appdir);

	// Create file
	fd = creat(apinfo, 0600);
	if (fd == -1) {
		error("%s: Couldn't open apinfo file %s: %m",
		      plugin_type, apinfo);
		close(fd);
		return -1;
	}

	// Change ownership of file to application user
	if ((fchown(fd, job->uid, job->gid) == -1) && (getuid() == 0)) {
		error("%s: Couldn't chown %s to uid %u gid %u: %m",
		      plugin_type, apinfo, job->uid, job->gid);
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * Write the job's node list to the file
 */
static int _write_pals_nodes(int fd, char *nodelist)
{
	hostlist_t hl;
	char *host;
	pals_node_t node;

	memset(&node, 0, sizeof(pals_node_t));
	if (!(hl = hostlist_create(nodelist))) {
		error("%s: Couldn't create hostlist", plugin_type);
		return SLURM_ERROR;
	}
	while ((host = hostlist_shift(hl))) {
		snprintf(node.hostname, sizeof(node.hostname), "%s", host);
		node.nid = _get_nid(host);
		free(host);
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
	pals_cmd_t *cmds = NULL;
	pals_pe_t *pes = NULL;
	int ntasks, ncmds, nnodes;
	uint16_t *task_cnts;
	uint32_t **tids;
	uint32_t *tid_offsets;
	char *nodelist;
	bool free_tid_offsets = false;

	// Make sure the application spool directory has been created
	if (!appdir) {
		return SLURM_ERROR;
	}

	// Get relevant information from job
	if (job->het_job_offset != NO_VAL) {
		ntasks = job->het_job_ntasks;
		ncmds = job->het_job_step_cnt;
		nnodes = job->het_job_nnodes;
		task_cnts = job->het_job_task_cnts;
		tids = job->het_job_tids;
		tid_offsets = job->het_job_tid_offsets;
		nodelist = job->het_job_node_list;
	} else {
		ntasks = job->ntasks;
		nnodes = job->nnodes;
		task_cnts = job->msg->tasks_to_launch;
		tids = job->msg->global_task_ids;
		nodelist = job->msg->complete_nodelist;

		if (job->flags & LAUNCH_MULTI_PROG) {
			_multi_prog_parse(job, &ncmds, &tid_offsets);
			free_tid_offsets = true;
		} else {
			ncmds = 1;
			tid_offsets = NULL;
		}
	}

	// Make sure we've got everything
	if (ntasks <= 0) {
		error("%s: no tasks found", plugin_type);
		goto rwfail;
	}
	if (ncmds <= 0) {
		error("%s: no cmds found", plugin_type);
		goto rwfail;
	}
	if (nnodes <= 0) {
		error("%s: no nodes found", plugin_type);
		goto rwfail;
	}
	if (task_cnts == NULL) {
		error("%s: no per-node task counts", plugin_type);
		goto rwfail;
	}
	if (tids == NULL) {
		error("%s: no task IDs found", plugin_type);
		goto rwfail;
	}
	if (nodelist == NULL) {
		error("%s: no nodelist found", plugin_type);
		goto rwfail;
	}

	// Get information to write
	_build_header(&hdr, ncmds, ntasks, nnodes);
	pes = _setup_pals_pes(ntasks, nnodes, task_cnts, tids, tid_offsets);
	cmds = _setup_pals_cmds(ncmds, ntasks, nnodes, job->cpus_per_task, pes);

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
		error("%s: Couldn't sync %s to disk: %m", plugin_type, apinfo);
		goto rwfail;
	}

	debug("%s: Wrote apinfo file %s", plugin_type, apinfo);

	// Clean up and return
	if (free_tid_offsets)
		xfree(tid_offsets);

	xfree(pes);
	xfree(cmds);
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	if (free_tid_offsets)
		xfree(tid_offsets);

	xfree(pes);
	xfree(cmds);
	close(fd);
	return SLURM_ERROR;
}
