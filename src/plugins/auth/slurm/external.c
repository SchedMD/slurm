/*****************************************************************************\
 *  external.c
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

#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/common/sack_api.h"
#include "src/common/xmalloc.h"
#include "src/plugins/auth/slurm/auth_slurm.h"

extern auth_cred_t *create_external(uid_t r_uid, void *data, int dlen)
{
	auth_cred_t *cred = new_cred();

	if (!(cred->token = sack_create(r_uid, data, dlen))) {
		error("%s: failed to create token", __func__);
		xfree(cred);
	}

	return cred;
}

extern int verify_external(auth_cred_t *cred)
{
	int rc = SLURM_ERROR;
	jwt_t *jwt = NULL;

	if (!cred) {
		error("%s: rejecting NULL cred", __func__);
		goto fail;
	}

	if (cred->verified)
		return SLURM_SUCCESS;

	if (!cred->token) {
		error("%s: rejecting NULL token", __func__);
		goto fail;
	}

	if ((rc = sack_verify(cred->token))) {
		error("%s: sack_verify failure: %s",
		      __func__, slurm_strerror(rc));
		goto fail;
	}

	cred->verified = true;

	if ((rc = jwt_decode(&jwt, cred->token, NULL, 0))) {
		error("%s: jwt_decode failure: %s",
		      __func__, slurm_strerror(rc));
		goto fail;
	}

	/* provides own logging on failures */
	if ((rc = copy_jwt_grants_to_cred(jwt, cred)))
		goto fail;

	debug2("token verified");
fail:
	if (jwt)
		jwt_free(jwt);
	return rc;
}
