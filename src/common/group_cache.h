/*****************************************************************************\
 *  group_cache.h
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
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

#ifndef _SLURM_GROUP_CACHE_H
#define _SLURM_GROUP_CACHE_H

#include <inttypes.h>
#include <sys/types.h>

/*
 * OUT: ngids as return value
 * IN: uid
 * IN: gid - primary group id (will always exist first in gids list)
 * IN: (optional) username, will be looked up if NULL and is needed
 * IN/OUT: gids - xmalloc'd gid_t * structure with ngids elements
 */
extern int group_cache_lookup(uid_t uid, gid_t gid, char *username, gid_t **gids);

/* call on daemon shutdown to cleanup properly */
void group_cache_purge(void);

/* call periodically to remove stale records */
void group_cache_cleanup(void);

/* return an xmalloc'd array of gids, or NULL */
extern gid_t *copy_gids(int ngids, gid_t *gids);

extern char **copy_gr_names(int ngids, char **gr_names);

#endif
