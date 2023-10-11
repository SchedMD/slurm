/*****************************************************************************\
 *  privileges.c
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#define _GNU_SOURCE

#include <grp.h>
#include <pwd.h>
#include <sys/types.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/strlcpy.h"
#include "src/common/xmalloc.h"
#include "src/interfaces/auth.h"
#include "src/slurmd/common/privileges.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

/*
 * If get_list is false make sure ps->gid_list is initialized before
 * hand to prevent xfree.
 */
extern int drop_privileges(stepd_step_rec_t *step, bool do_setuid,
			   struct priv_state *ps, bool get_list)
{
	auth_setuid_lock();
	ps->saved_uid = getuid();
	ps->saved_gid = getgid();

	if (!getcwd(ps->saved_cwd, sizeof(ps->saved_cwd))) {
		error ("Unable to get current working directory: %m");
		strlcpy(ps->saved_cwd, "/tmp", sizeof(ps->saved_cwd));
	}

	ps->ngids = getgroups(0, NULL);
	if (ps->ngids == -1) {
		error("%s: getgroups(): %m", __func__);
		return SLURM_ERROR;
	}
	if (get_list) {
		ps->gid_list = xcalloc(ps->ngids, sizeof(gid_t));

		if (getgroups(ps->ngids, ps->gid_list) < 0) {
			error("%s: couldn't get %d groups: %m",
			      __func__, ps->ngids);
			xfree(ps->gid_list);
			return SLURM_ERROR;
		}
	}

	/* No need to drop privileges if we're not running as root */
	if (getuid())
		return SLURM_SUCCESS;

	if (setegid(step->gid) < 0) {
		error("setegid: %m");
		return SLURM_ERROR;
	}

	if (setgroups(step->ngids, step->gids) < 0) {
		error("setgroups: %m");
		return SLURM_ERROR;
	}

	if (do_setuid && seteuid(step->uid) < 0) {
		error("seteuid: %m");
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern int reclaim_privileges(struct priv_state *ps)
{
	int rc = SLURM_SUCCESS;

	/*
	 * No need to reclaim privileges if our uid == step->uid
	 */
	if (geteuid() == ps->saved_uid)
		goto done;

	if (seteuid(ps->saved_uid) < 0) {
		error("seteuid: %m");
		rc = SLURM_ERROR;
	} else if (setegid(ps->saved_gid) < 0) {
		error("setegid: %m");
		rc = SLURM_ERROR;
	} else if (setgroups(ps->ngids, ps->gid_list) < 0) {
		error("setgroups: %m");
		rc = SLURM_ERROR;
	}

done:
	auth_setuid_unlock();
	xfree(ps->gid_list);

	return rc;
}
