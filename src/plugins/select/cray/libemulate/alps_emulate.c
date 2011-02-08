/*****************************************************************************\
 *  alps_emulate.c - simple ALPS emulator used for testing purposes
 *****************************************************************************
 *  Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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
#include <unistd.h>

#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/xmalloc.h"
#include "../basil_alps.h"
#include "../parser_common.h"

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

/* If _ADD_DELAYS is set, then include sleep calls to emulate delays
 * expected for ALPS/BASIL interactions */
#define _ADD_DELAYS  0
#define _DEBUG       0
#define MAX_RESV_CNT 500

static MYSQL *mysql_handle = NULL;
static MYSQL_BIND *my_bind_col = NULL;
static struct node_record *my_node_ptr = NULL;
static int my_node_inx = 0;

static int hw_cabinet, hw_row, hw_cage, hw_slot, hw_cpu;
static int coord[3], max_dim[3];

static int last_resv_id = 0;
static uint32_t resv_jobid[MAX_RESV_CNT];

/* Given a count of elements to distribute over a "dims" size space, 
 * compute the minimum number of elements in each dimension to accomodate
 * them assuming the number of elements in each dimension is similar (i.e.
 * a cube rather than a long narrow box shape).
 * IN spur_cnt - number of nodes at each coordinate
 * IN/OUT coord - maximum coordinates in each dimension
 * IN dims - -number of dimensions to use */
static void _get_dims(int spur_cnt, int *coord, int dims)
{
	int count = 1, i;

	for (i = 0; i < dims; i++)
		coord[i] = 1;

	while (1) {
		for (i = 0; i < dims; i++) {
			if (count >= spur_cnt)
				return;
			count /= coord[i];
			coord[i]++;
			count *= coord[i];
		}
	}
}

/* increment coordinates for a node */
static void _incr_dims(int *coord, int *max_dim, int dims)
{
	int i;

	for (i = 0; i < dims; i++) {
		coord[i]++;
		if (coord[i] < max_dim[i])
			return;
		coord[i] = 0;
	}
}

/* Initialize the hardware pointer records */
static void _init_hw_recs(void)
{
	int i;

	hw_cabinet = 0;
	hw_row = 0;
	hw_cage = 0;
	hw_slot = 0;
	hw_cpu = 0;

	for (i = 0; i < 3; i++)
		coord[i] = 0;

	my_node_ptr = node_record_table_ptr;
	my_node_inx = 0;
	_get_dims(node_record_count/4, max_dim, 3);
}

/* Increment the hardware pointer records */
static void _incr_hw_recs(void)
{
	hw_cpu++;
	if (hw_cpu > 3) {
		hw_cpu = 0;
		hw_slot++;
		_incr_dims(coord, max_dim, 3);
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

	my_node_ptr++;
	my_node_inx++;
}

extern void free_nodespec(struct nodespec *head)
{
#if _DEBUG
	info("free_nodespec: start:%u end:%u", head->start, head->end);
#endif

	if (head) {
		free_nodespec(head->next);
		free(head);
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
	_init_hw_recs();

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
	*((unsigned int *)my_bind_col[COL_CORES].buffer)  =
			my_node_ptr->config_ptr->cpus;
	*((my_bool *)my_bind_col[COL_CORES].is_null)  = (my_bool) 0;
	*((unsigned int *)my_bind_col[COL_MEMORY].buffer) =
			my_node_ptr->config_ptr->real_memory;
	*((my_bool *)my_bind_col[COL_MEMORY].is_null)  = (my_bool) 0;

	*((int *)my_bind_col[COL_CAB].buffer) = hw_cabinet;
	*((int *)my_bind_col[COL_ROW].buffer) = hw_row;
	*((int *)my_bind_col[COL_CAGE].buffer) = hw_cage;
	*((int *)my_bind_col[COL_SLOT].buffer) = hw_slot;
	*((int *)my_bind_col[COL_CPU].buffer) = hw_cpu;

	*((int *)my_bind_col[COL_X].buffer) = coord[0];
	*((int *)my_bind_col[COL_Y].buffer) = coord[1];
	*((int *)my_bind_col[COL_Z].buffer) = coord[2];

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
	return (my_bool) 0;
}

void cray_close_sdb(MYSQL *handle)
{
#if _DEBUG
	info("cray_close_sdb");
#endif
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
	struct basil_inventory *inv;
	struct node_record *node_ptr;
	struct basil_node *basil_node_ptr, **last_basil_node_ptr;
	struct basil_rsvn *basil_rsvn_ptr, **last_basil_rsvn_ptr;

#if _DEBUG
	info("get_full_inventory");
#endif

	inv = xmalloc(sizeof(struct basil_inventory));
	inv->is_gemini = true;
	inv->batch_avail =node_record_count;
	inv->batch_total =node_record_count;
	inv->nodes_total =node_record_count;
	inv->f = xmalloc(sizeof(struct basil_full_inventory));
	last_basil_node_ptr = &inv->f->node_head;
	for (i = 0, node_ptr = node_record_table_ptr; i <node_record_count;
	     i++, node_ptr++) {
		basil_node_ptr = xmalloc(sizeof(struct basil_node));
		*last_basil_node_ptr = basil_node_ptr;
		basil_node_ptr->node_id = i;
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
		basil_rsvn_ptr->rsvn_id = i;
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
			  uint32_t mem_mb, struct nodespec *ns_head)
{
	int i;
	uint32_t job_id;

#if _DEBUG
	struct nodespec *my_node_spec;
	info("basil_reserve user:%s batch_id:%s width:%u depth:%u nppn:%u "
	     "mem_mb:%u",
	     user, batch_id, width, depth, nppn, mem_mb);
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

	job_id = atol(batch_id);
	for (i = 0; i < MAX_RESV_CNT; i++) {
		if (resv_jobid[last_resv_id])
			continue;
		resv_jobid[last_resv_id] = job_id;
		last_resv_id++;
		last_resv_id %= MAX_RESV_CNT;
		return last_resv_id;
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
		return -1;
	if (resv_jobid[rsvn_id-1] != job_id)
		return -1;

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
