/*****************************************************************************\
 *  alps_emulate.c - simple ALPS emulator used for testing purposes
 *****************************************************************************
 *  Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Written by Morris Jette <jette@schedmd.com>
 *  CODE-OCEC-09-009. All rights reserved.
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
#include "src/common/xmalloc.h"
#include "../basil_alps.h"

/* If _ADD_DELAYS is set, then include sleep calls to emulate delays
 * expected for ALPS/BASIL interactions */
#define _ADD_DELAYS 0
#define _DEBUG 1

static MYSQL *mysql_handle = NULL;

/*
 * Enum-to-string mapping tables
 */

/* Basil versions */
const char *bv_names[BV_MAX] = {	/* Basil Protocol version */
	[BV_1_0] = "1.0",
	[BV_1_1] = "1.1",
	[BV_1_2] = "1.1",
	[BV_3_1] = "1.1"
};

const char *bv_names_long[BV_MAX] = {	/* Actual version name */
	[BV_1_0] = "1.0",
	[BV_1_1] = "1.1",
	[BV_1_2] = "1.2",
	[BV_3_1] = "3.1"
};

/* Basil methods */
const char *bm_names[BM_MAX] = {
	[BM_none]	= "NONE",
	[BM_engine]	= "QUERY",
	[BM_inventory]	= "QUERY",
	[BM_reserve]	= "RESERVE",
	[BM_confirm]	= "CONFIRM",
	[BM_release]	= "RELEASE",
};

/* Error codes */
const char *be_names[BE_MAX] = {
	[BE_NONE]	= "",
	[BE_INTERNAL]	= "INTERNAL",
	[BE_SYSTEM]	= "SYSTEM",
	[BE_PARSER]	= "PARSER",
	[BE_SYNTAX]	= "SYNTAX",
	[BE_BACKEND]	= "BACKEND",
	[BE_UNKNOWN]	= "UNKNOWN"
};
static const char *be_names_long[BE_MAX] = {
	[BE_NONE]	= "no ALPS error",
	[BE_INTERNAL]	= "internal error: unexpected condition encountered",
	[BE_SYSTEM]	= "system call failed",
	[BE_PARSER]	= "XML parser error",
	[BE_SYNTAX]	= "improper XML content or structure",
	[BE_BACKEND]	= "ALPS backend error",
	[BE_UNKNOWN]	= "UNKNOWN ALPS ERROR"
};

/*
 * RESERVE/INVENTORY data
 */
const char *nam_arch[BNA_MAX] = {
	[BNA_NONE]	= "UNDEFINED",
	[BNA_X2]	= "X2",
	[BNA_XT]	= "XT",
	[BNA_UNKNOWN]	= "UNKNOWN"
};

const char *nam_memtype[BMT_MAX] = {
	[BMT_NONE]	= "UNDEFINED",
	[BMT_OS]	= "OS",
	[BMT_HUGEPAGE]	= "HUGEPAGE",
	[BMT_VIRTUAL]	= "VIRTUAL",
	[BMT_UNKNOWN]	= "UNKNOWN"
};

const char *nam_labeltype[BLT_MAX] = {
	[BLT_NONE]	= "UNDEFINED",
	[BLT_HARD]	= "HARD",
	[BLT_SOFT]	= "SOFT",
	[BLT_UNKNOWN]	= "UNKNOWN"
};

const char *nam_ldisp[BLD_MAX] = {
	[BLD_NONE]	= "UNDEFINED",
	[BLD_ATTRACT]	= "ATTRACT",
	[BLD_REPEL]	= "REPEL",
	[BLD_UNKNOWN]	= "UNKNOWN"
};

/*
 * INVENTORY-only data
 */
const char *nam_noderole[BNR_MAX] = {
	[BNR_NONE]	= "UNDEFINED",
	[BNR_INTER]	= "INTERACTIVE",
	[BNR_BATCH]	= "BATCH",
	[BNR_UNKNOWN]	= "UNKNOWN"
};

const char *nam_nodestate[BNS_MAX] = {
	[BNS_NONE]	= "UNDEFINED",
	[BNS_UP]	= "UP",
	[BNS_DOWN]	= "DOWN",
	[BNS_UNAVAIL]	= "UNAVAILABLE",
	[BNS_ROUTE]	= "ROUTING",
	[BNS_SUSPECT]	= "SUSPECT",
	[BNS_ADMINDOWN]	= "ADMIN",
	[BNS_UNKNOWN]	= "UNKNOWN"
};

const char *nam_proc[BPT_MAX] = {
	[BPT_NONE]	= "UNDEFINED",
	[BPT_CRAY_X2]	= "cray_x2",
	[BPT_X86_64]	= "x86_64",
	[BPT_UNKNOWN]	= "UNKNOWN"
};

extern int ns_add_node(struct nodespec **head, uint32_t node_id)
{
#if _DEBUG
	info("ns_add_node: id:%u", node_id);
#endif
	return 0;
}

extern char *ns_to_string(const struct nodespec *head)
{
#if _DEBUG
	info("ns_to_string: start:%u end:%u", head->start, head->end);
#endif
	return NULL;
}

extern void free_nodespec(struct nodespec *head)
{
#if _DEBUG
	info("free_nodespec: start:%u end:%u", head->start, head->end);
#endif
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

	return (MYSQL_STMT *) query;
}

/** Execute and return the number of rows. */
extern int exec_stmt(MYSQL_STMT *stmt, const char *query,
		     MYSQL_BIND bind_col[], unsigned long ncols)
{
#if _DEBUG
	info("exec_stmt");
#endif
#if _ADD_DELAYS
	usleep(5000);
#endif
	return 0;
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
	return 1;
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
#if _DEBUG
	info("get_full_inventory");
#endif
	struct basil_inventory *inv;

	inv = xmalloc(sizeof(struct basil_inventory));
	inv->is_gemini = true;
	inv->batch_avail = 1;
	inv->batch_total = 1;
	inv->nodes_total = 1;
	inv->f = xmalloc(sizeof(struct basil_full_inventory));
	inv->f->node_head = xmalloc(sizeof(struct basil_node));
//FIXME: We need to generate a series of node records here based upon the
// node count as well as the reservation records below
	inv->f->node_head->node_id = 0;
	strncpy(inv->f->node_head->name, "NODE_NAME", BASIL_STRING_SHORT);
	inv->f->node_head->next = NULL;
	inv->f->rsvn_head = NULL;
	return inv;
}

extern void   free_inv(struct basil_inventory *inv)
{
#if _DEBUG
	info("free_inv");
#endif
	if (inv) {
//FIXME: Free linked list of node and reservation records
		xfree(inv->f->node_head);
		xfree(inv->f->rsvn_head);
		xfree(inv->f);
		xfree(inv);
	}
}

extern long basil_reserve(const char *user, const char *batch_id,
			  uint32_t width, uint32_t depth, uint32_t nppn,
			  uint32_t mem_mb, struct nodespec *ns_head)
{
#if _DEBUG
	info("basil_reserve user:%s batch_id:%s width:%u depth:%u nppn:%u "
	     "mem_mb:%u node_spec:start:%u,end:%u",
	     user, batch_id, width, depth, nppn, mem_mb,
	     ns_head->start, ns_head->end);
#endif
	return 0;
}

extern int basil_confirm(uint32_t rsvn_id, int job_id, uint64_t pagg_id)
{
#if _DEBUG
	info("basil_confirm: rsvn_id:%u", rsvn_id);
#endif
	return 0;
}

extern int basil_release(uint32_t rsvn_id)
{
#if _DEBUG
	info("basil_release: rsvn_id:%u", rsvn_id);
#endif
	return 0;
}

