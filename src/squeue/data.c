/*****************************************************************************\
 *  data.c - data functions for squeue
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>.
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

#include "config.h"

#include "slurm/slurm.h"

#include "src/common/fd.h"
#include "src/common/log.h"
#include "src/common/openapi.h"
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/squeue/squeue.h"

#define TARGET "/slurm/v0.0.38/jobs/"
#define PLUGIN "openapi/v0.0.38"

openapi_handler_t dump_job = NULL;

extern void *openapi_get_db_conn(void *ctxt)
{
	fatal("%s should never be called in squeue", __func__);
}

extern int bind_operation_handler(const char *str_path,
				  openapi_handler_t callback, int callback_tag)
{
	debug3("%s: binding %s to 0x%"PRIxPTR,
	       __func__, str_path, (uintptr_t) callback);

	if (!xstrcmp(str_path, TARGET))
		dump_job = callback;

	return SLURM_SUCCESS;
}

extern int unbind_operation_handler(openapi_handler_t callback)
{
	/* no-op */
	return SLURM_SUCCESS;
}

extern int dump_data(int argc, char **argv)
{
	openapi_t *oas = NULL;
	data_t *resp = data_new();
	char *out = NULL;
	char *ctxt;

	if (init_openapi(&oas, PLUGIN, NULL))
		fatal("unable to load openapi plugins");

	ctxt = fd_resolve_path(STDIN_FILENO);

	dump_job(ctxt, HTTP_REQUEST_GET, NULL, NULL, 0, resp, NULL);

	data_g_serialize(&out, resp, params.mimetype, DATA_SER_FLAGS_PRETTY);

	printf("%s", out);

#ifdef MEMORY_LEAK_DEBUG
	xfree(ctxt);
	xfree(out);
	FREE_NULL_DATA(resp);
	destroy_openapi(oas);
#endif /* MEMORY_LEAK_DEBUG */

	return SLURM_SUCCESS;
}
