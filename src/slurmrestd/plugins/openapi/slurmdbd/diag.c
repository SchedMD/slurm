/*****************************************************************************\
 *  diag.c - Slurm REST API accounting diag http operations handlers
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <stdint.h>

#include "slurm/slurm.h"
#include "slurm/slurmdb.h"

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/operations.h"
#include "api.h"

/* based on sacctmgr_list_stats() */
extern int op_handler_diag(ctxt_t *ctxt)
{
	int rc;
	slurmdb_stats_rec_t *stats_rec = NULL;

	debug4("%s: [%s] diag handler called", __func__, ctxt->id);

	if (ctxt->rc)
		goto cleanup;
	else if ((rc = slurmdb_get_stats(ctxt->db_conn, &stats_rec)))
		resp_error(ctxt, rc, "slurmdb_get_stats", "stats query failed");

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_STATS_RESP, stats_rec, ctxt);

cleanup:
	slurmdb_destroy_stats_rec(stats_rec);
	return SLURM_SUCCESS;
}

extern int op_handler_ping(ctxt_t *ctxt)
{
	slurmdbd_ping_t *pings = NULL;

	debug4("%s: [%s] ping handler called", __func__, ctxt->id);

	if (ctxt->rc)
		goto cleanup;

	if (!(pings = slurmdb_ping_all()))
		resp_error(ctxt, SLURM_ERROR, "slurmdb_ping_all",
			   "ping query failed");

	DUMP_OPENAPI_RESP_SINGLE(OPENAPI_SLURMDBD_PING_RESP, pings, ctxt);

cleanup:
	xfree(pings);
	return SLURM_SUCCESS;
}
