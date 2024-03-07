/*****************************************************************************\
 *  identity.h
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

#ifndef _IDENTITY_H
#define _IDENTITY_H

#include <sys/types.h>

#include "src/common/pack.h"

typedef struct {
	uid_t uid;
	gid_t gid;
	char *pw_name;		/* user_name as a string */
	char *pw_gecos;		/* user information */
	char *pw_dir;		/* home directory */
	char *pw_shell;		/* user program */
	int ngids;		/* number of extended group ids */
	gid_t *gids;		/* extended group ids for user */
	char **gr_names;	/* array of group names matching gids */

	bool fake;		/* not a complete identity, only uid/gid */
} identity_t;

extern identity_t *fetch_identity(uid_t uid, gid_t gid, bool group_names);

extern void pack_identity(identity_t *id, buf_t *buffer,
			  uint16_t protocol_version);
extern int unpack_identity(identity_t **id, buf_t *buffer,
			   uint16_t protocol_version);

extern identity_t *copy_identity(identity_t *id);
extern void destroy_identity(identity_t *id);

#define FREE_NULL_IDENTITY(_X)		\
do {					\
	if (_X)				\
		destroy_identity(_X);	\
	_X = NULL;			\
} while(0)

extern void identity_debug2(identity_t *id, const char *func);

#endif
