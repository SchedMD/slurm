/*****************************************************************************\
 * src/common/uid.h - uid/gid lookup utility functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __SLURM_UID_UTILITY_H__
#define __SLURM_UID_UTILITY_H__

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>

/*
 * In an ideal world, we could use sysconf(_SC_GETPW_R_SIZE_MAX) to get the
 * maximum buffer size neede for getpwnam_r(), but if there is no maximum
 * value configured, the value returned is 1024, which can too small.
 * Diito for _SC_GETGR_R_SIZE_MAX.
 */
#define PW_BUF_SIZE 65536

/* Retry getpwuid_r while return code is EINTR so we always get the
 * info.  Return return code of getpwuid_r.
 */
extern int slurm_getpwuid_r (uid_t uid, struct passwd *pwd, char *buf,
			     size_t bufsiz, struct passwd **result);

/*
 * Return validated uid_t for string in ``name'' which contains
 *  either the UID number or user name
 *
 * Returns uid int uidp after verifying presence in /etc/passwd, or
 *  -1 on failure.
 */
int uid_from_string (char *name, uid_t *uidp);

/*
 * Return the primary group id for a given user id, or
 * (gid_t) -1 on failure.
 */
gid_t gid_from_uid (uid_t uid);

/*
 * Same as uid_from_name(), but for group name/id.
 */
int gid_from_string (char *name, gid_t *gidp);

/*
 * Translate uid to user name,
 * NOTE: xfree the return value
 */
char *uid_to_string (uid_t uid);

/* Free any memory allocated by uid_to_string_cached() */
extern void uid_cache_clear(void);

/*
 * Translate uid to user name, using a cache.
 * Call uid_cache_clear() to free memory.
 */
extern char *uid_to_string_cached(uid_t uid);

/*
 * Same as uid_to_string, but for group name.
 * NOTE: xfree the return value
 */
char *gid_to_string (gid_t gid);
#endif /*__SLURM_UID_UTILITY_H__*/
