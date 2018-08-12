/*****************************************************************************\
 *  port_mgr.c - manage the reservation of I/O ports on the nodes.
 *	Design for use with OpenMPI.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include <stdlib.h>
#include <string.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmctld/slurmctld.h"

#define  _DEBUG 0

bitstr_t **port_resv_table = (bitstr_t **) NULL;
int        port_resv_cnt   = 0;
int        port_resv_min   = 0;
int        port_resv_max   = 0;

static void _dump_resv_port_info(void);
static void _make_all_resv(void);
static void _make_step_resv(struct step_record *step_ptr);
static void _rebuild_port_array(struct step_record *step_ptr);

static void _dump_resv_port_info(void)
{
#if _DEBUG
	int i;
	char *tmp_char;

	for (i=0; i<port_resv_cnt; i++) {
		if (bit_set_count(port_resv_table[i]) == 0)
			continue;

		tmp_char = bitmap2node_name(port_resv_table[i]);
		info("Port %d: %s", (i+port_resv_min), tmp_char);
		xfree(tmp_char);
	}
#endif
}

/* Builds the job step's resv_port_array based upon resv_ports (a string) */
static void _rebuild_port_array(struct step_record *step_ptr)
{
	int i;
	char *tmp_char;
	hostlist_t hl;

	tmp_char = xstrdup_printf("[%s]", step_ptr->resv_ports);
	hl = hostlist_create(tmp_char);
	xfree(tmp_char);
	if (!hl) {
		error("%pS has invalid reserved ports: %s",
		      step_ptr, step_ptr->resv_ports);
		xfree(step_ptr->resv_ports);
		return;
	}

	step_ptr->resv_port_array = xmalloc(sizeof(int) *
					    step_ptr->resv_port_cnt);
	step_ptr->resv_port_cnt = 0;
	while ((tmp_char = hostlist_shift(hl))) {
		i = atoi(tmp_char);
		if (i > 0)
			step_ptr->resv_port_array[step_ptr->resv_port_cnt++]=i;
		free(tmp_char);
	}
	hostlist_destroy(hl);
	if (step_ptr->resv_port_cnt == 0) {
		error("Problem recovering resv_port_array for %pS: %s",
		      step_ptr, step_ptr->resv_ports);
		xfree(step_ptr->resv_ports);
	}
}

/* Update the local reservation table for one job step.
 * Builds the job step's resv_port_array based upon resv_ports (a string) */
static void _make_step_resv(struct step_record *step_ptr)
{
	int i, j;

	if ((step_ptr->resv_port_cnt == 0) ||
	    (step_ptr->resv_ports == NULL) ||
	    (step_ptr->resv_ports[0] == '\0'))
		return;

	if (step_ptr->resv_port_array == NULL)
		_rebuild_port_array(step_ptr);

	for (i=0; i<step_ptr->resv_port_cnt; i++) {
		if ((step_ptr->resv_port_array[i] < port_resv_min) ||
		    (step_ptr->resv_port_array[i] > port_resv_max))
			continue;
		j = step_ptr->resv_port_array[i] - port_resv_min;
		bit_or(port_resv_table[j], step_ptr->step_node_bitmap);
	}
}

/* Identify every job step with a port reservation and put the
 * reservation into the local reservation table. */
static void _make_all_resv(void)
{
	struct job_record *job_ptr;
	struct step_record *step_ptr;
	ListIterator job_iterator, step_iterator;

	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		step_iterator = list_iterator_create(job_ptr->step_list);
		while ((step_ptr = (struct step_record *)
				   list_next(step_iterator))) {
			if (step_ptr->state < JOB_RUNNING)
				continue;
			_make_step_resv(step_ptr);
		}
		list_iterator_destroy(step_iterator);
	}
	list_iterator_destroy(job_iterator);
}

/* Configure reserved ports.
 * Call with mpi_params==NULL to free memory */
extern int reserve_port_config(char *mpi_params)
{
	char *tmp_e=NULL, *tmp_p=NULL;
	int i, p_min, p_max;

	if (mpi_params)
		tmp_p = strstr(mpi_params, "ports=");
	if (tmp_p == NULL) {
		if (port_resv_table) {
			info("Clearing port reservations");
			for (i=0; i<port_resv_cnt; i++)
				FREE_NULL_BITMAP(port_resv_table[i]);
			xfree(port_resv_table);
			port_resv_cnt = 0;
			port_resv_min = port_resv_max = 0;
		}
		return SLURM_SUCCESS;
	}

	tmp_p += 6;
	p_min = strtol(tmp_p, &tmp_e, 10);
	if ((p_min < 1) || (tmp_e[0] != '-')) {
		info("invalid MpiParams: %s", mpi_params);
		return SLURM_ERROR;
	}
	tmp_e++;
	p_max = strtol(tmp_e, NULL, 10);
	if (p_max < p_min) {
		info("invalid MpiParams: %s", mpi_params);
		return SLURM_ERROR;
	}

	if ((p_min == port_resv_min) && (p_max == port_resv_max)) {
		_dump_resv_port_info();
		return SLURM_SUCCESS;	/* No change */
	}

	port_resv_min = p_min;
	port_resv_max = p_max;
	port_resv_cnt = p_max - p_min + 1;
	debug("Ports available for reservation %u-%u",
	      port_resv_min, port_resv_max);

	xfree(port_resv_table);
	port_resv_table = xmalloc(sizeof(bitstr_t *) * port_resv_cnt);
	for (i=0; i<port_resv_cnt; i++)
		port_resv_table[i] = bit_alloc(node_record_count);

	_make_all_resv();
	_dump_resv_port_info();
	return SLURM_SUCCESS;
}

/* Reserve ports for a job step
 * NOTE: We keep track of last port reserved and go round-robin through full
 *       set of available ports. This helps avoid re-using busy ports when
 *       restarting job steps.
 * RET SLURM_SUCCESS or an error code */
extern int resv_port_alloc(struct step_record *step_ptr)
{
	int i, port_inx;
	int *port_array = NULL;
	char port_str[16];
	hostlist_t hl;
	static int last_port_alloc = 0;
	static int dims = -1;

	if (dims == -1)
		dims = slurmdb_setup_cluster_name_dims();

	if (step_ptr->resv_port_cnt > port_resv_cnt) {
		info("%pS needs %u reserved ports, but only %d exist",
		     step_ptr, step_ptr->resv_port_cnt, port_resv_cnt);
		return ESLURM_PORTS_INVALID;
	}

	/* Identify available ports */
	port_array = xmalloc(sizeof(int) * step_ptr->resv_port_cnt);
	port_inx = 0;
	for (i=0; i<port_resv_cnt; i++) {
		if (++last_port_alloc >= port_resv_cnt)
			last_port_alloc = 0;
		if (bit_overlap(step_ptr->step_node_bitmap,
				port_resv_table[last_port_alloc]))
			continue;
		port_array[port_inx++] = last_port_alloc;
		if (port_inx >= step_ptr->resv_port_cnt)
			break;
	}
	if (port_inx < step_ptr->resv_port_cnt) {
		info("insufficient ports for %pS to reserve (%d of %u)",
		     step_ptr, port_inx, step_ptr->resv_port_cnt);
		xfree(port_array);
		return ESLURM_PORTS_BUSY;
	}

	/* Reserve selected ports */
	hl = hostlist_create(NULL);
	for (i=0; i<port_inx; i++) {
		bit_or(port_resv_table[port_array[i]],
		       step_ptr->step_node_bitmap);
		port_array[i] += port_resv_min;
		snprintf(port_str, sizeof(port_str), "%d", port_array[i]);
		hostlist_push_host(hl, port_str);
	}
	hostlist_sort(hl);
	/* get the ranged string with no brackets on it */
	step_ptr->resv_ports = hostlist_ranged_string_xmalloc_dims(hl, dims, 0);
	hostlist_destroy(hl);
	step_ptr->resv_port_array = port_array;

	debug("reserved ports %s for %pS",
	      step_ptr->resv_ports, step_ptr);

	return SLURM_SUCCESS;
}

/* Release reserved ports for a job step
 * RET SLURM_SUCCESS or an error code */
extern void resv_port_free(struct step_record *step_ptr)
{
	int i, j;

	if (step_ptr->resv_port_array == NULL)
		return;

	for (i=0; i<step_ptr->resv_port_cnt; i++) {
		if ((step_ptr->resv_port_array[i] < port_resv_min) ||
		    (step_ptr->resv_port_array[i] > port_resv_max))
			continue;
		j = step_ptr->resv_port_array[i] - port_resv_min;
		bit_and_not(port_resv_table[j], step_ptr->step_node_bitmap);

	}
	xfree(step_ptr->resv_port_array);

	debug("freed ports %s for %pS",
	      step_ptr->resv_ports, step_ptr);
}
