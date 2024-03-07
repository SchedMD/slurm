/*****************************************************************************\
 *  sbcast.c
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

#include <jwt.h>
#include <inttypes.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/cred.h"
#include "src/interfaces/serializer.h"

#include "src/plugins/auth/slurm/auth_slurm.h"

extern char *encode_sbcast(sbcast_cred_arg_t *cred)
{
	data_t *data = NULL, *data_sbcast = NULL;
	char *json = NULL;

	data = identity_to_data(cred->id);

	data_sbcast = data_set_dict(data_key_set(data, "sbcast"));

	data_set_string(data_key_set(data_sbcast, "nodes"), cred->nodes);
	data_set_int(data_key_set(data_sbcast, "job_id"), cred->job_id);
	data_set_int(data_key_set(data_sbcast, "het_job_id"), cred->het_job_id);
	data_set_int(data_key_set(data_sbcast, "step_id"), cred->step_id);

	serialize_g_data_to_string(&json, NULL, data, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);

	FREE_NULL_DATA(data);
	return json;
}

extern sbcast_cred_t *extract_sbcast(char *json)
{
	data_t *data = NULL;
	sbcast_cred_t *cred = NULL;

	if (serialize_g_string_to_data(&data, json, strlen(json),
				       MIME_TYPE_JSON)) {
		error("%s: failed to decode sbcast field", __func__);
		return NULL;
	}

	cred = xmalloc(sizeof(*cred));
	cred->arg.nodes = xstrdup(data_get_string(data_key_set(data, "nodes")));
	cred->arg.job_id = data_get_int(data_key_set(data, "job_id"));
	cred->arg.het_job_id = data_get_int(data_key_set(data, "het_job_id"));
	cred->arg.step_id = data_get_int(data_key_set(data, "step_id"));

	FREE_NULL_DATA(data);
	return cred;
}
