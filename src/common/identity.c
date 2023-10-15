/*****************************************************************************\
 *  identity.c
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

#include "src/common/identity.h"
#include "src/common/pack.h"
#include "src/common/xmalloc.h"

extern void pack_identity(identity_t *id, buf_t *buffer,
			  uint16_t protocol_version)
{
	/*
	 * The gr_names array is optional. If the array exists the length
	 * must match that of the gids array.
	 */
	uint32_t gr_names_cnt = (id->gr_names) ? id->ngids : 0;

	packstr(id->pw_name, buffer);
	packstr(id->pw_gecos, buffer);
	packstr(id->pw_dir, buffer);
	packstr(id->pw_shell, buffer);
	pack32_array(id->gids, id->ngids, buffer);
	packstr_array(id->gr_names, gr_names_cnt, buffer);
}

extern void destroy_identity(identity_t *id)
{
	if (!id)
		return;

	xfree(id->pw_name);
	xfree(id->pw_gecos);
	xfree(id->pw_dir);
	xfree(id->pw_shell);
	xfree(id->gids);

	if (id->gr_names) {
		for (int i = 0; i < id->ngids; i++)
			xfree(id->gr_names[i]);
		xfree(id->gr_names);
	}
	id->ngids = 0;
}
