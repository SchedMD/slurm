/*****************************************************************************\
 *  apinfo.c - Cray Shasta PMI apinfo file creation
 *****************************************************************************
 *  Copyright 2019,2022 Hewlett Packard Enterprise Development LP
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
 * The file's contents are stored in step->argv[1], and follow this format:
 * <taskids> <command> <arguments>
 *
 * taskids is a range list of task IDs or * (for all remaining task IDs).
 * command and arguments give the argv to run for those tasks.
 * Empty lines and lines starting with # are ignored.
 * Newlines may be escaped with \.
 */
static void _multi_prog_parse(const stepd_step_rec_t *step, int *ncmds,
			      uint32_t **tid_offsets)
{
	int i = 0, line_num = 0, rank_id = 0, num_cmds = 0, nranks = 0;
	char *line = NULL, *local_data = NULL;
	char *end_ptr = NULL, *save_ptr = NULL, *tmp_str = NULL;
	char *rank_spec = NULL, *p = NULL, *one_rank = NULL;
	hostlist_t hl;
	uint32_t *offsets = NULL;

	offsets = xcalloc(step->ntasks, sizeof(uint32_t));
	for (i = 0; i < step->ntasks; i++) {
		offsets[i] = NO_VAL;
	}

	// Copy contents of MPMD file so we can tokenize it
	local_data = xstrdup(step->argv[1]);

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
			for (i = 0; i < step->ntasks; i++) {
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
				    (rank_id >= step->ntasks)) {
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
	for (i = 0; i < step->ntasks; i++) {
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
 * Open the per-job-step file created by the Slingshot plugin (if available);
 * fill in the pals_header_t structure at the beginning of the file;
 * return the file descriptor (and the file name in *ss_apinfop)
 */
static int _open_ss_info(const stepd_step_rec_t *job, const char *spool,
			 pals_header_t *hdr, char **ss_apinfop)
{
	int fd = -1;

	/* Open info file written by the Slingshot plugin */
	*ss_apinfop = xstrdup_printf("%s/%s/apinfo.%u.%u",
				     spool, HPE_SLINGSHOT_DIR,
				     job->step_id.job_id, job->step_id.step_id);
	fd = open(*ss_apinfop, O_RDONLY);
	if (fd == -1) {
		/* This is expected if Slingshot plugin isn't in use */
		debug("%s: Couldn't open %s: %m", plugin_type, *ss_apinfop);
		goto rwfail;
	}

	/* Read header */
	safe_read(fd, hdr, sizeof(*hdr));

	/* Check header fields */
	if (hdr->version != PALS_APINFO_VERSION) {
		error("%s: %s version %d doesn't match expected version %d",
		      plugin_type, *ss_apinfop,
		      hdr->version, PALS_APINFO_VERSION);
		goto rwfail;
	}
	return fd;

rwfail:
	if (fd != -1)
		close(fd);
	xfree(*ss_apinfop);
	return -1;
}

/*
 * Given the file descriptor and pals_header_t header from the information
 * file left by the Slingshot plugin, return a list of communication profiles
 */
static pals_comm_profile_t *_setup_pals_profiles(int fd, pals_header_t *hdr,
						 char *ss_apinfo,
						 int *nprofiles)
{
	pals_comm_profile_t *profiles = NULL;
	size_t profiles_size = 0;

	*nprofiles = 0;
	if (fd < 0)
		return NULL;

	/* Check header fields */
	if (hdr->ncomm_profiles < 0) {
		error("%s: %s invalid ncomm_profiles %d",
		      plugin_type, ss_apinfo, hdr->ncomm_profiles);
		goto rwfail;
	}
	if (hdr->comm_profile_size != sizeof(pals_comm_profile_t)) {
		error("%s: %s invalid comm_profile_size %zu != %zu",
		      plugin_type, ss_apinfo, hdr->comm_profile_size,
		      sizeof(pals_comm_profile_t));
		goto rwfail;
	}

	debug("%s: Found %d comm profiles in %s",
	      plugin_type, hdr->ncomm_profiles, ss_apinfo);

	if (hdr->ncomm_profiles == 0)
		return NULL;

	/* Allocate space for the profiles */
	profiles_size = hdr->ncomm_profiles * hdr->comm_profile_size;
	profiles = xmalloc(profiles_size);

	/* Read the profiles from the correct position */
	if (lseek(fd, hdr->comm_profile_offset, SEEK_SET) == -1) {
		error("%s: Couldn't seek to %zu in %s: %m",
		      plugin_type, hdr->comm_profile_offset, ss_apinfo);
		goto rwfail;
	}
	safe_read(fd, profiles, profiles_size);

	*nprofiles = hdr->ncomm_profiles;
	return profiles;

rwfail:
	xfree(profiles);
	return NULL;
}

/*
 * Given the file descriptor and pals_header_t header from the information
 * file left by the Slingshot plugin, return a list of HSN NIC info
 */
static pals_hsn_nic_t *_setup_pals_nics(int fd, pals_header_t *hdr,
				        char *ss_apinfo, int *nnics)
{
	pals_hsn_nic_t *nics = NULL;
	size_t nics_size = 0;

	*nnics = 0;
	if (fd < 0)
		return NULL;

	/* Check header fields */
	if (hdr->nnics < 0) {
		error("%s: %s invalid nnics %d", plugin_type,
		      ss_apinfo, hdr->nnics);
		goto rwfail;
	}
	if (hdr->nic_size != sizeof(pals_hsn_nic_t)) {
		error("%s: %s invalid nic_size %zu != %zu",
		      plugin_type, ss_apinfo, hdr->nic_size,
		      sizeof(pals_hsn_nic_t));
		goto rwfail;
	}

	debug("%s: Found %d hsn nics in %s", plugin_type,
	      hdr->nnics, ss_apinfo);

	if (hdr->nnics == 0)
		return NULL;

	// Allocate space for the nics
	nics_size = hdr->nnics * hdr->nic_size;
	nics = xmalloc(nics_size);

	// Read the profiles from the correct position
	if (lseek(fd, hdr->nic_offset, SEEK_SET) == -1) {
		error("%s: Couldn't seek to %zu in %s: %m", plugin_type,
		      hdr->nic_offset, ss_apinfo);
		goto rwfail;
	}
	safe_read(fd, nics, nics_size);

	*nnics = hdr->nnics;
	return nics;

rwfail:
	xfree(nics);
	return NULL;
}

/*
 * Fill in the apinfo header
 */
static void _build_header(pals_header_t *hdr, int ncmds, int npes, int nnodes,
			  int nprofiles, int nnics)
{
	size_t offset = sizeof(pals_header_t);

	memset(hdr, 0, sizeof(pals_header_t));
	hdr->version = PALS_APINFO_VERSION;

	hdr->comm_profile_size = sizeof(pals_comm_profile_t);
	hdr->comm_profile_offset = offset;
	hdr->ncomm_profiles = nprofiles;
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

	hdr->nic_size = sizeof(pals_hsn_nic_t);
	hdr->nic_offset = offset;
	hdr->nnics = nnics;
	offset += hdr->nic_size * hdr->nnics;

	/* Don't support status reporting or NIC distances yet */
	hdr->status_offset = 0;
	hdr->dist_size = 0;
	hdr->dist_offset = 0;

	hdr->total_size = offset;
}

/*
 * Open the apinfo file and return a writeable fd, or -1 on failure
 */
static int _open_apinfo(const stepd_step_rec_t *step)
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
	if ((fchown(fd, step->uid, step->gid) == -1) && (getuid() == 0)) {
		error("%s: Couldn't chown %s to uid %u gid %u: %m",
		      plugin_type, apinfo, step->uid, step->gid);
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
extern int create_apinfo(const stepd_step_rec_t *step, const char *spool)
{
	int fd = -1;
	pals_header_t hdr;
	char *ss_apinfo = NULL;
	pals_comm_profile_t *profiles = NULL;
	pals_hsn_nic_t *nics = NULL;
	pals_cmd_t *cmds = NULL;
	pals_pe_t *pes = NULL;
	int ntasks, ncmds, nnodes, nprofiles, nnics;
	uint16_t *task_cnts;
	uint32_t **tids;
	uint32_t *tid_offsets;
	char *nodelist;
	bool free_tid_offsets = false;

	// Make sure the application spool directory has been created
	if (!appdir)
		return SLURM_ERROR;

	/* Get relevant information from job */
	if (step->het_job_offset != NO_VAL) {
		ntasks = step->het_job_ntasks;
		ncmds = step->het_job_step_cnt;
		nnodes = step->het_job_nnodes;
		task_cnts = step->het_job_task_cnts;
		tids = step->het_job_tids;
		tid_offsets = step->het_job_tid_offsets;
		nodelist = step->het_job_node_list;
	} else {
		ntasks = step->ntasks;
		nnodes = step->nnodes;
		task_cnts = step->msg->tasks_to_launch;
		tids = step->msg->global_task_ids;
		nodelist = step->msg->complete_nodelist;

		if (step->flags & LAUNCH_MULTI_PROG) {
			_multi_prog_parse(step, &ncmds, &tid_offsets);
			free_tid_offsets = true;
		} else {
			ncmds = 1;
			tid_offsets = NULL;
		}
	}

	/* Make sure we've got everything */
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

	/* Get comm profile and NIC arrays from Slingshot plugin file */
	fd = _open_ss_info(step, spool, &hdr, &ss_apinfo);
	profiles = _setup_pals_profiles(fd, &hdr, ss_apinfo, &nprofiles);
	nics = _setup_pals_nics(fd, &hdr, ss_apinfo, &nnics);
	if (fd)
		close(fd);
	xfree(ss_apinfo);

	_build_header(&hdr, ncmds, ntasks, nnodes, nprofiles, nnics);
	pes = _setup_pals_pes(ntasks, nnodes, task_cnts, tids, tid_offsets);
	cmds = _setup_pals_cmds(ncmds, ntasks, nnodes,
				step->cpus_per_task, pes);

	/* Create the file */
	fd = _open_apinfo(step);
	if (fd == -1)
		goto rwfail;

	/* Write info */
	safe_write(fd, &hdr, sizeof(pals_header_t));
	safe_write(fd, profiles,
		   (hdr.ncomm_profiles * sizeof(pals_comm_profile_t)));
	safe_write(fd, cmds, (hdr.ncmds * sizeof(pals_cmd_t)));
	safe_write(fd, pes, (hdr.npes * sizeof(pals_pe_t)));

	if (_write_pals_nodes(fd, nodelist) == SLURM_ERROR)
		goto rwfail;

	safe_write(fd, nics, (hdr.nnics * sizeof(pals_hsn_nic_t)));

	/* Flush changes to disk */
	if (fsync(fd) == -1) {
		error("%s: Couldn't sync %s to disk: %m", plugin_type, apinfo);
		goto rwfail;
	}

	debug("%s: Wrote apinfo file %s", plugin_type, apinfo);

	/* Clean up and return */
	if (free_tid_offsets)
		xfree(tid_offsets);

	xfree(nics);
	xfree(pes);
	xfree(profiles);
	xfree(cmds);
	close(fd);
	return SLURM_SUCCESS;

rwfail:
	if (free_tid_offsets)
		xfree(tid_offsets);

	xfree(nics);
	xfree(pes);
	xfree(profiles);
	xfree(cmds);
	close(fd);
	return SLURM_ERROR;
}
