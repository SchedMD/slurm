/*****************************************************************************\
 *  util.c
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
#include "src/common/group_cache.h"
#include "src/common/log.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/serializer.h"

#include "src/plugins/auth/common/auth_common.h"
#include "src/plugins/auth/slurm/auth_slurm.h"

extern auth_cred_t *new_cred(void)
{
	auth_cred_t *cred = xmalloc(sizeof(*cred));

	/* just to be explicit */
	cred->verified = false;
	cred->uid = (uid_t) -1;
	cred->gid = (gid_t) -1;

	return cred;
}

extern void destroy_cred(auth_cred_t *cred)
{
	if (!cred)
		return;

	xfree(cred->cluster);
	xfree(cred->context);
	xfree(cred->data);
	FREE_NULL_IDENTITY(cred->id);
	xfree(cred->hostname);
	xfree(cred->token);
	xfree(cred);
}

extern int copy_jwt_grants_to_cred(jwt_t *jwt, auth_cred_t *cred)
{
	const char *hostname, *context, *payload;

	errno = 0;
	cred->ctime = jwt_get_grant_int(jwt, "iat");
	if (errno == EINVAL) {
		error("%s: jwt_get_grant_int failure for iat", __func__);
		return SLURM_ERROR;
	}

	errno = 0;
	cred->uid = jwt_get_grant_int(jwt, "uid");
	if (errno == EINVAL) {
		error("%s: jwt_get_grant_int failure for uid", __func__);
		return SLURM_ERROR;
	}

	errno = 0;
	cred->gid = jwt_get_grant_int(jwt, "gid");
	if (errno == EINVAL) {
		error("%s: jwt_get_grant_int failure for gid", __func__);
		return SLURM_ERROR;
	}

	errno = 0;
	hostname = jwt_get_grant(jwt, "host");
	if (!hostname || (errno == EINVAL)) {
		error("%s: jwt_get_grant failure for host", __func__);
		return SLURM_ERROR;
	}
	cred->hostname = xstrdup(hostname);

	/* this isn't mandatory. NULL if the grant is missing is fine */
	cred->cluster = xstrdup(jwt_get_grant(jwt, "cluster"));

	errno = 0;
	context = jwt_get_grant(jwt, "context");
	if (!context || (errno == EINVAL)) {
		error("%s: jwt_get_grant failure for context", __func__);
		return SLURM_ERROR;
	}
	cred->context = xstrdup(context);

	errno = 0;
	if ((payload = jwt_get_grant(jwt, "payload"))) {
		cred->data = xmalloc(strlen(payload));
		cred->dlen = jwt_Base64decode(cred->data, payload);
	}

	return SLURM_SUCCESS;
}
