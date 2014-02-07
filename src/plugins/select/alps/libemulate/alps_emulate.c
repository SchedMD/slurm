/*****************************************************************************\
 *  alps_emulate.c - simple ALPS emulator used for testing purposes
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 *  Supported by the Oak Ridge National Laboratory Extreme Scale Systems Center
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  endif
#endif
#include <stdlib.h>
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xmalloc.h"
#include "../basil_alps.h"
#include "../parser_common.h"
#include "hilbert.h"

/* Global variables */
const char *bv_names[BV_MAX];
const char *bv_names_long[BV_MAX];
const char *bm_names[BM_MAX];
const char *be_names[BE_MAX];

const char *nam_arch[BNA_MAX];
const char *nam_memtype[BMT_MAX];
const char *nam_labeltype[BLT_MAX];
const char *nam_ldisp[BLD_MAX];

const char *nam_noderole[BNR_MAX];
const char *nam_nodestate[BNS_MAX];
const char *nam_proc[BPT_MAX];
const char *nam_rsvn_mode[BRM_MAX];
const char *nam_gpc_mode[BGM_MAX];

const char *nam_acceltype[BA_MAX];
const char *nam_accelstate[BAS_MAX];

bool node_rank_inv = 0;

/* If _ADD_DELAYS is set, then include sleep calls to emulate delays
 * expected for ALPS/BASIL interactions */
#define _ADD_DELAYS  0
#define _DEBUG       0
#define MAX_RESV_CNT 500
#define NODES_PER_COORDINATE 1

static MYSQL *mysql_handle = NULL;
static MYSQL_BIND *my_bind_col = NULL;
static struct node_record *my_node_ptr = NULL;
static int my_node_inx = 0;

static int hw_cabinet, hw_row, hw_cage, hw_slot, hw_cpu;
static int coord[3], max_dim[3];

static int sys_spur_cnt = 0, last_spur_inx = 0;
static int *sys_coords = NULL;
static coord_t *sys_hilbert;

static int last_resv_id = 0;
static uint32_t resv_jobid[MAX_RESV_CNT];


/* Given a count of elements to distribute over a "dims" size space, 
 * compute the minimum number of elements in each dimension to accomodate
 * them assuming the number of elements in each dimension is similar (i.e.
 * a cube rather than a long narrow box shape).
 * IN spur_cnt - number of nodes at each coordinate
 * IN/OUT coord - maximum coordinates in each dimension
 * IN dims - number of dimensions to use */
static void _get_dims(int spur_cnt, int *coord, int dims)
{
	int count = 1, i, j;
	coord_t hilbert[3];

	xfree(sys_coords);
	xfree(sys_hilbert);
	for (i = 0; i < dims; i++)
		coord[i] = 1;

	do {
		/* Increase size of dimensions from high to low here and do so
		 * by doubling sizes, but fill from low to high to improve
		 * performance of Hilbert curve fitting for better job
		 * locality */
		for (i = (dims - 1); i >= 0; i--) {
			if (count >= spur_cnt)
				break;
			count /= coord[i];
			coord[i] *= 2;
			count *= coord[i];
		}
	} while (count < spur_cnt);

	/* Build table of possible coordinates */
	sys_spur_cnt = spur_cnt;
	sys_coords  = xmalloc(sizeof(int) * spur_cnt * dims);
	/* We leave record zero at coordinate 000 */
	for (i = 1; i < spur_cnt; i++) {
		for (j = 0; j < dims; j++)
			sys_coords[i*dims + j] = sys_coords[i*dims + j - dims];
		for (j = 0; j < dims; j++) {
			sys_coords[i*dims+j]++;
			if (sys_coords[i*dims+j] < coord[j])
				break;
			sys_coords[i*dims+j] = 0;
		}
	}

	/* For each coordinate, generate it's Hilbert number */
	sys_hilbert = xmalloc(sizeof(coord_t) * spur_cnt);
	for (i = 0; i < spur_cnt; i++) {
		for (j = 0; j < dims; j++)
			hilbert[j] = sys_coords[i*dims + j];
		AxestoTranspose(hilbert, 5, dims);
		/* A variation on the below calculation would be required here
		 * for other dimension counts */
		sys_hilbert[i] =
			((hilbert[0]>>4 & 1) << 14) +
			((hilbert[1]>>4 & 1) << 13) +
			((hilbert[2]>>4 & 1) << 12) +
			((hilbert[0]>>3 & 1) << 11) +
			((hilbert[1]>>3 & 1) << 10) +
			((hilbert[2]>>3 & 1) <<  9) +
			((hilbert[0]>>2 & 1) <<  8) +
			((hilbert[1]>>2 & 1) <<  7) +
			((hilbert[2]>>2 & 1) <<  6) +
			((hilbert[0]>>1 & 1) <<  5) +
			((hilbert[1]>>1 & 1) <<  4) +
			((hilbert[2]>>1 & 1) <<  3) +
			((hilbert[0]>>0 & 1) <<  2) +
			((hilbert[1]>>0 & 1) <<  1) +
			((hilbert[2]>>0 & 1) <<  0);
	}

	/* Sort the entries by increasing hilbert number */
	for (i = 0; i < spur_cnt; i++) {
		int tmp_int, low_inx = i;
		for (j = i+1; j < spur_cnt; j++) {
			if (sys_hilbert[j] < sys_hilbert[low_inx])
				low_inx = j;
		}
		if (low_inx == i)
			continue;
		tmp_int = sys_hilbert[i];
		sys_hilbert[i] = sys_hilbert[low_inx];
		sys_hilbert[low_inx] = tmp_int;
		for (j = 0; j < dims; j++) {
			tmp_int = sys_coords[i*dims + j];
			sys_coords[i*dims + j] = sys_coords[low_inx*dims + j];
			sys_coords[low_inx*dims + j] = tmp_int;
		}
	}

#if _DEBUG
	for (i = 0; i < spur_cnt; i++) {
		info("coord:%d:%d:%d hilbert:%d", sys_coords[i*dims],
		     sys_coords[i*dims+1], sys_coords[i*dims+2],
		     sys_hilbert[i]);
	}
#endif
}

/* increment coordinates for a node */
static void _incr_dims(int *coord, int dims)
{
	int j;

	last_spur_inx++;
	if (last_spur_inx >= sys_spur_cnt) {
		error("alps_emualte: spur count exceeded");
		last_spur_inx = 0;
	}

	for (j = 0; j < dims; j++)
		coord[j] = sys_coords[last_spur_inx*dims + j];
}

/* Initialize the hardware pointer records */
static void _init_hw_recs(int dims)
{
	int j, spur_cnt;

	hw_cabinet = 0;
	hw_row = 0;
	hw_cage = 0;
	hw_slot = 0;
	hw_cpu = 0;

	my_node_ptr = node_record_table_ptr;
	my_node_inx = 0;
	spur_cnt = node_record_count + NODES_PER_COORDINATE - 1;
	spur_cnt /= NODES_PER_COORDINATE;
	_get_dims(spur_cnt, max_dim, 3);

	last_spur_inx = 0;
	for (j = 0; j < dims; j++)
		coord[j] = sys_coords[last_spur_inx*dims + j];
}

/* Increment the hardware pointer records */
static void _incr_hw_recs(void)
{
	if (++my_node_inx >= node_record_count)
		return;	/* end of node table */

	my_node_ptr++;
	if ((my_node_inx % NODES_PER_COORDINATE) == 0)
		_incr_dims(coord, 3);
	hw_cpu++;
	if (hw_cpu > 3) {
		hw_cpu = 0;
		hw_slot++;
	}
	if (hw_slot > 7) {
		hw_slot = 0;
		hw_cage++;
	}
	if (hw_cage > 2) {
		hw_cage = 0;
		hw_cabinet++;
	}
	if (hw_cabinet > 16) {
		hw_cabinet = 0;
		hw_row++;
	}
}

extern void free_nodespec(struct nodespec *head)
{
#if _DEBUG
	info("free_nodespec: start:%u end:%u", head->start, head->end);
#endif

	if (head) {
		free_nodespec(head->next);
		xfree(head);
	}
}


static void _rsvn_free_param_accel(struct basil_accel_param *a)
{
	if (a) {
		_rsvn_free_param_accel(a->next);
		xfree(a);
	}
}

/*
 *	Routines to interact with SDB database (uses prepared statements)
 */
/** Connect to the XTAdmin table on the SDB */
extern MYSQL *cray_connect_sdb(void)
{
#if _DEBUG
	info("cray_connect_sdb");
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	if (mysql_handle)
		error("cray_connect_sdb: Duplicate MySQL connection");
	else
		mysql_handle = (MYSQL *) xmalloc(1);

	return mysql_handle;
}

/** Initialize and prepare statement */
extern MYSQL_STMT *prepare_stmt(MYSQL *handle, const char *query,
				MYSQL_BIND bind_parm[], unsigned long nparams,
				MYSQL_BIND bind_cols[], unsigned long ncols)
{
#if _DEBUG
	info("prepare_stmt: query:%s", query);
#endif
	if (handle != mysql_handle)
		error("prepare_stmt: bad MySQL handle");
	_init_hw_recs(3);

	return (MYSQL_STMT *) query;
}

/** Execute and return the number of rows. */
extern int exec_stmt(MYSQL_STMT *stmt, const char *query,
		     MYSQL_BIND *bind_col, unsigned long ncols)
{
#if _DEBUG
	info("exec_stmt");
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	my_bind_col = bind_col;

	return 0;
}

extern int fetch_stmt(MYSQL_STMT *stmt)
{
#if _DEBUG
	info("fetch_stmt");
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	if (my_node_inx >=node_record_count)
		return 1;

	strncpy(my_bind_col[COL_TYPE].buffer, "compute", BASIL_STRING_SHORT);

	*((int *)my_bind_col[COL_X].buffer) = coord[0];
	*((int *)my_bind_col[COL_Y].buffer) = coord[1];
	*((int *)my_bind_col[COL_Z].buffer) = coord[2];

	*((my_bool *)my_bind_col[COL_X].is_null)  = (my_bool) 0;
	*((my_bool *)my_bind_col[COL_Y].is_null)  = (my_bool) 0;
	*((my_bool *)my_bind_col[COL_Z].is_null)  = (my_bool) 0;

	_incr_hw_recs();

	return 0;
}

my_bool free_stmt_result(MYSQL_STMT *stmt)
{
#if _DEBUG
	info("free_stmt_result");
#endif
	return (my_bool) 0;
}

my_bool stmt_close(MYSQL_STMT *stmt)
{
#if _DEBUG
	info("stmt_close");
#endif
	sys_spur_cnt = 0;
	xfree(sys_coords);
	xfree(sys_hilbert);

	return (my_bool) 0;
}

void cray_close_sdb(MYSQL *handle)
{
#if _DEBUG
	info("cray_close_sdb");
#endif
	xfree(mysql_handle);
	return;
}

/** Find out interconnect chip: Gemini (XE) or SeaStar (XT) */
extern int cray_is_gemini_system(MYSQL *handle)
{
#if _DEBUG
	info("cray_is_gemini_system");
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	if (handle != mysql_handle)
		error("cray_is_gemini_system: bad MySQL handle");
	return 0;
}

/*
 *	Basil XML-RPC API prototypes
 */
extern enum basil_version get_basil_version(void)
{
#if _DEBUG
	info("basil_version get_basil_version");
#endif
	return BV_3_1;
}

extern int basil_request(struct basil_parse_data *bp)
{
#if _DEBUG
	info("basil_request");
#endif
	return 0;
}

extern struct basil_inventory *get_full_inventory(enum basil_version version)
{
	int i;
	char *end_ptr;
	struct basil_inventory *inv;
	struct node_record *node_ptr;
	struct basil_node *basil_node_ptr, **last_basil_node_ptr;
	struct basil_rsvn *basil_rsvn_ptr, **last_basil_rsvn_ptr;

#if _DEBUG
	info("get_full_inventory");
#endif

	inv = xmalloc(sizeof(struct basil_inventory));
	inv->is_gemini   = true;
	inv->batch_avail = node_record_count;
	inv->batch_total = node_record_count;
	inv->nodes_total = node_record_count;
	inv->f = xmalloc(sizeof(struct basil_full_inventory));
	last_basil_node_ptr = &inv->f->node_head;
	for (i = 0, node_ptr = node_record_table_ptr; i <node_record_count;
	     i++, node_ptr++) {
		basil_node_ptr = xmalloc(sizeof(struct basil_node));
		*last_basil_node_ptr = basil_node_ptr;
		basil_node_ptr->node_id = strtol(node_ptr->name+3, &end_ptr,
						 10);
		if (end_ptr[0] != '\0') {
			error("Invalid node name: %s", basil_node_ptr->name);
			basil_node_ptr->node_id = i;
		}
		strncpy(basil_node_ptr->name, node_ptr->name,
			BASIL_STRING_SHORT);
		basil_node_ptr->state = BNS_UP;
		basil_node_ptr->role  = BNR_BATCH;
		basil_node_ptr->arch  = BNA_XT;
		last_basil_node_ptr = &basil_node_ptr->next;
	}
	last_basil_rsvn_ptr = &inv->f->rsvn_head;
	for (i = 0; i < MAX_RESV_CNT; i++) {
		if (resv_jobid[i] == 0)
			continue;
		basil_rsvn_ptr = xmalloc(sizeof(struct basil_rsvn));
		*last_basil_rsvn_ptr = basil_rsvn_ptr;
		basil_rsvn_ptr->rsvn_id = i + 1;
		last_basil_rsvn_ptr = &basil_rsvn_ptr->next;
	}
	return inv;
}

extern void   free_inv(struct basil_inventory *inv)
{
	struct basil_node *basil_node_ptr, *next_basil_node_ptr;
	struct basil_rsvn *basil_rsvn_ptr, *next_basil_rsvn_ptr;
#if _DEBUG
	info("free_inv");
#endif
	if (inv) {
		basil_node_ptr = inv->f->node_head;
		while (basil_node_ptr) {
			next_basil_node_ptr = basil_node_ptr->next;
			xfree(basil_node_ptr);
			basil_node_ptr = next_basil_node_ptr;
		}
		basil_rsvn_ptr = inv->f->rsvn_head;
		while (basil_rsvn_ptr) {
			next_basil_rsvn_ptr = basil_rsvn_ptr->next;
			xfree(basil_rsvn_ptr);
			basil_rsvn_ptr = next_basil_rsvn_ptr;
		}
		xfree(inv->f);
		xfree(inv);
	}
}

extern long basil_reserve(const char *user, const char *batch_id,
			  uint32_t width, uint32_t depth, uint32_t nppn,
			  uint32_t mem_mb, uint32_t nppcu, struct nodespec *ns_head,
			  struct basil_accel_param *accel_head)
{
	int i;
	uint32_t job_id;

#if _DEBUG
	struct nodespec *my_node_spec;
	info("basil_reserve user:%s batch_id:%s width:%u depth:%u nppn:%u "
	     "mem_mb:%u nppcu:%u",
	     user, batch_id, width, depth, nppn, mem_mb, nppcu);
	my_node_spec = ns_head;
	while (my_node_spec) {
		info("basil_reserve node_spec:start:%u,end:%u",
		     my_node_spec->start, my_node_spec->end);
		my_node_spec = my_node_spec->next;
	}
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif

	free_nodespec(ns_head);
	_rsvn_free_param_accel(accel_head);
	job_id = atol(batch_id);
	for (i = 0; i < MAX_RESV_CNT; i++) {
		int my_resv_id;
		if (resv_jobid[last_resv_id])
			continue;
		resv_jobid[last_resv_id] = job_id;
		my_resv_id = ++last_resv_id;	/* one origin */
		last_resv_id %= MAX_RESV_CNT;
		return my_resv_id;
	}

	return 0;
}

extern int basil_confirm(uint32_t rsvn_id, int job_id, uint64_t pagg_id)
{
#if _DEBUG
	info("basil_confirm: rsvn_id:%u", rsvn_id);
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	if ((job_id == 0) || (rsvn_id > MAX_RESV_CNT))
		return -BE_NO_RESID;
#if 0
	/* This is executed from the slurmd, so we really can not confirm
	 * here if the reseravation was made by the slurmctld. Just assume
	 * it is valid. */
	if (resv_jobid[rsvn_id-1] != job_id)
		return -1;
#endif

	return 0;
}

extern int basil_release(uint32_t rsvn_id)
{
#if _DEBUG
	info("basil_release: rsvn_id:%u", rsvn_id);
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif

	resv_jobid[rsvn_id - 1] = 0;

	return 0;
}

int basil_signal_apids(int32_t rsvn_id, int signal, struct basil_inventory *inv)
{
#if _DEBUG
	info("basil_signal_apids: rsvn_id:%u signal:%d", rsvn_id, signal);
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif

	return 0;
}

extern bool node_is_allocated(const struct basil_node *node)
{
	char nid[9];	/* nid%05d\0 */
	struct node_record *node_ptr;

	snprintf(nid, sizeof(nid), "nid%05u", node->node_id);
	node_ptr = find_node_record(nid);
	if (node_ptr == NULL)
		return false;

	return IS_NODE_ALLOCATED(node_ptr);
}

int basil_switch(uint32_t rsvn_id, bool suspend)
{
	return 0;
}
