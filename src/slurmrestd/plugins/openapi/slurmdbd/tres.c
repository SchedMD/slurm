/*****************************************************************************\
 *  tres.c - Slurm REST API accounting TRES http operations handlers
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

#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/slurmrestd/openapi.h"
#include "src/slurmrestd/operations.h"
#include "api.h"

extern int update_tres(ctxt_t *ctxt, bool commit, list_t *tres_list)
{
#ifdef NDEBUG
	/*
	 * Updating TRES is not currently supported and is disabled
	 * except for developer testing as the TRES id can not be
	 * maintained while updating or adding new TRES.
	 */
	if (commit)
		resp_error(ctxt, ESLURM_NOT_SUPPORTED, __func__,
			   "Updating TRES is not currently supported");
	return ESLURM_NOT_SUPPORTED;
#else
	int rc;

	if (!(rc = db_query_rc(ctxt, tres_list, slurmdb_tres_add)) &&
	    !ctxt->rc && commit)
		db_query_commit(ctxt);

	return rc;
#endif /* NDEBUG */
}

extern int op_handler_tres(ctxt_t *ctxt)
{
	if (ctxt->method == HTTP_REQUEST_GET) {
		list_t *tres_list = NULL;
		slurmdb_tres_cond_t tres_cond = {
			/* mimic slurmdb_init_tres_cond() */
			.with_deleted = 1,
			.count = NO_VAL,
		};

		if (!db_query_list(ctxt, &tres_list, slurmdb_tres_get,
				   &tres_cond))
			DUMP_OPENAPI_RESP_SINGLE(OPENAPI_TRES_RESP, tres_list,
						 ctxt);

		FREE_NULL_LIST(tres_list);
	} else if (ctxt->method == HTTP_REQUEST_POST) {
#ifdef NDEBUG
		/*
		 * Updating TRES is not currently supported and is disabled
		 * except for developer testing as the TRES id can not be
		 * maintained while updating or adding new TRES.
		 */
		resp_error(ctxt, ESLURM_NOT_SUPPORTED, __func__,
			   "Updating TRES is not currently supported");
#else
		openapi_resp_single_t resp = {0};
		openapi_resp_single_t *resp_ptr = &resp;
		list_t *tres_list = NULL;

		if (!DATA_PARSE(ctxt->parser, OPENAPI_TRES_RESP, tres_list,
				ctxt->query, ctxt->parent_path))
			update_tres(ctxt, true, tres_list);

		FREE_NULL_LIST(tres_list);
		FREE_OPENAPI_RESP_COMMON_CONTENTS(resp_ptr);
#endif /* NDEBUG */
	} else {
		resp_error(ctxt, ESLURM_REST_INVALID_QUERY, __func__,
			   "Unsupported HTTP method requested: %s",
			   get_http_method_string(ctxt->method));
	}

	return SLURM_SUCCESS;
}
