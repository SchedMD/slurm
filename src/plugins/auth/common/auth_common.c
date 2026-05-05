/*****************************************************************************\
 *  auth_common.c - Common authentication utilities implementation
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Copyright Amazon.com Inc. or its affiliates.
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

#include <inttypes.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/plugins/auth/common/auth_common.h"

static identity_t *_extract_identity_from_data(data_t *data_id, uid_t uid,
					       gid_t gid);

extern char *auth_common_get_identity_string(identity_t *id, uid_t uid,
					     gid_t gid)
{
	data_t *data = NULL;
	char *json = NULL;
	identity_t *id_local = NULL;

	if (!id && !(id = id_local = fetch_identity(uid, gid, true)))
		return NULL;

	data = auth_common_identity_to_data(id);
	FREE_NULL_IDENTITY(id_local);

	serialize_g_data_to_string(&json, NULL, data, MIME_TYPE_JSON,
				   SER_FLAGS_COMPACT);
	FREE_NULL_DATA(data);

	return json;
}

extern data_t *auth_common_identity_to_data(identity_t *id)
{
	data_t *data = NULL, *data_id = NULL, *groups = NULL;

	data = data_set_dict(data_new());

	/*
	 * Don't bother constructing incomplete identities here,
	 * none of the relevant fields are set here.
	 */
	if (!id || id->fake)
		return data;

	data_id = data_set_dict(data_key_set(data, "id"));

	data_set_string(data_key_set(data_id, "name"), id->pw_name);
	data_set_string(data_key_set(data_id, "gecos"), id->pw_gecos);
	data_set_string(data_key_set(data_id, "dir"), id->pw_dir);
	data_set_string(data_key_set(data_id, "shell"), id->pw_shell);

	if (id->gr_names) {
		groups = data_set_dict(data_key_set(data_id, "groups"));
		for (int i = 0; i < id->ngids; i++)
			data_set_int(data_key_set(groups, id->gr_names[i]),
				     id->gids[i]);
	} else if (id->ngids) {
		data_t *data_gids =
			data_set_list(data_key_set(data_id, "gids"));
		for (int i = 0; i < id->ngids; i++)
			data_set_int(data_list_append(data_gids), id->gids[i]);
	}

	return data;
}

static data_for_each_cmd_t _for_each_group(const char *key, const data_t *data,
					   void *arg)
{
	identity_t *id = (identity_t *) arg;
	int64_t gid_val;

	if (data_get_int_converted(data, &gid_val)) {
		error("%s: failed to convert group gid", __func__);
		return DATA_FOR_EACH_FAIL;
	}

	if (gid_val < 0 || gid_val > UINT32_MAX) {
		error("%s: invalid gid value: %ld", __func__, gid_val);
		return DATA_FOR_EACH_FAIL;
	}

	id->gids[id->ngids] = (gid_t) gid_val;
	id->gr_names[id->ngids] = xstrdup(key);
	id->ngids++;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _for_each_gid(const data_t *data, void *arg)
{
	identity_t *id = (identity_t *) arg;
	int64_t gid_val;

	if (data_get_int_converted(data, &gid_val)) {
		error("%s: failed to convert gid", __func__);
		return DATA_FOR_EACH_FAIL;
	}

	if (gid_val < 0 || gid_val > UINT32_MAX) {
		error("%s: invalid gid value: %ld", __func__, gid_val);
		return DATA_FOR_EACH_FAIL;
	}

	id->gids[id->ngids] = (gid_t) gid_val;
	id->ngids++;

	return DATA_FOR_EACH_CONT;
}

extern identity_t *auth_common_extract_identity(char *json, uid_t uid,
						gid_t gid)
{
	data_t *data_id = NULL;
	identity_t *id = NULL;

	if (serialize_g_string_to_data(&data_id, json, strlen(json),
				       MIME_TYPE_JSON)) {
		error("%s: failed to decode id field", __func__);
		FREE_NULL_IDENTITY(id);
		return NULL;
	}

	id = _extract_identity_from_data(data_id, uid, gid);

	FREE_NULL_DATA(data_id);
	return id;
}

static identity_t *_extract_identity_from_data(data_t *data_id, uid_t uid,
					       gid_t gid)
{
	data_t *groups = NULL, *gids = NULL;
	int ngids;
	identity_t *id = xmalloc(sizeof(*id));

	id->uid = uid;
	id->gid = gid;

	id->pw_name = xstrdup(data_get_string(data_key_get(data_id, "name")));
	id->pw_gecos = xstrdup(data_get_string(data_key_get(data_id, "gecos")));
	id->pw_dir = xstrdup(data_get_string(data_key_get(data_id, "dir")));
	id->pw_shell = xstrdup(data_get_string(data_key_get(data_id, "shell")));

	if ((groups = data_key_get(data_id, "groups"))) {
		ngids = data_get_dict_length(groups);
		id->gids = xcalloc(ngids, sizeof(gid_t));
		id->gr_names = xcalloc(ngids, sizeof(char *));

		if (data_dict_for_each_const(groups, _for_each_group, id) < 0) {
			error("%s: data_dict_for_each_const failed", __func__);
			FREE_NULL_IDENTITY(id);
			return NULL;
		}
	} else if ((gids = data_key_get(data_id, "gids"))) {
		ngids = data_get_list_length(gids);
		id->gids = xcalloc(ngids, sizeof(gid_t));
		if (data_list_for_each_const(gids, _for_each_gid, id) < 0) {
			error("%s: data_list_for_each_const failed", __func__);
			FREE_NULL_IDENTITY(id);
			return NULL;
		}
	}

	return id;
}
