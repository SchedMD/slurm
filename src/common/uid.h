/*****************************************************************************\
 * src/common/uid.h - uid/gid lookup utility functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef __SLURM_UID_UTILITY_H__
#define __SLURM_UID_UTILITY_H__

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <stdbool.h>

/*
 * In an ideal world, we could use sysconf(_SC_GETPW_R_SIZE_MAX) to get the
 * maximum buffer size needed for getpwnam_r(), but if there is no maximum
 * value configured, the value returned is 1024, which can too small.
 * Diito for _SC_GETGR_R_SIZE_MAX.
 */
#define PW_BUF_SIZE 65536

/*
 * Handle EINTR and ERANGE when possible for getpwuid_r().
 * This accepts a pointer to the buffer currently being used as well as one that
 * can be xmalloxed if we encounter ERANGE.  The caller is expected to free
 * buf_malloc() at the appropriate time.
 *
 * If this fails, *result will be NULL.
 */
extern void slurm_getpwuid_r(uid_t uid, struct passwd *pwd, char **curr_buf,
			     char **buf_malloc, size_t *bufsize,
			     struct passwd **result);
/*
 * uid_from_string() - given a string with either a username or a uid in it
 *      populate the passed uid pointer with the correct uid and return an
 *      integer indicated the degree of success.  This function is sanitizing
 *      inputs against current system state and provides UIDs that the Slurm
 *      daemons use in a security sensitive manner.
 *
 * IN name: string with either a provided username or provided string uid
 *          ownership of the username pointer
 * IN/OUT uidp: pointer to the caller's uid_t memory, will be populated with
 *          the identified uid, though degree of success is communicated via
 *          the return code.
 *
 * Returns:
 *     SLURM_SUCCESS: uid was successfully looked up, uidp populated with
 *                    the passwd database version of the uid
 *     SLURM_ERROR: the name string was not a username nor was it a valid
 *                    number that *might* be a uid. uidp is NOT populated,
 *                    caller should NOT proceed with the results
 *     ESLURM_USER_ID_UNKNOWN: the name string had an encoded number but was
 *                    not in the user database accessible to the system.
 *                    uidp is populated with the parsed uid number. caller
 *                    should proceed with care.
 *     SLURM_AUTH_NOBODY: requested name matches SLURM_AUTH_NOBODY_NAME,
 *     		      the in/out variables are untouched.
 */
extern int uid_from_string(const char *name, uid_t *uidp);

/*
 * Return the primary group id for a given user id, or
 * (gid_t) -1 on failure.
 */
extern gid_t gid_from_uid (uid_t uid);

/*
 * Same as uid_from_name(), but for group name/id.
 */
extern int gid_from_string(const char *name, gid_t *gidp);

/*
 * Translate uid to user name.
 * Will return NULL on error.
 * NOTE: xfree the return value.
 */
extern char *uid_to_string_or_null(uid_t uid);

/*
 * Translate uid to user name,
 * If lookup fails, will return the uid printed as a string.
 * NOTE: xfree the return value
 */
extern char *uid_to_string(uid_t uid);

/* Empty the uid cache and free any memory */
extern void uid_cache_clear(void);

/*
 * Translate uid to user name, using a cache.
 * Call uid_cache_clear() to free memory.
 */
extern char *uid_to_string_cached(uid_t uid);

/*
 * Translate uid to home directory.
 * NOTE: xfree the return value
 */
extern char *uid_to_dir(uid_t uid);

/*
 * Translate uid to shell.
 * NOTE: xfree the return value
 */
extern char *uid_to_shell(uid_t uid);

/*
 * Same as uid_to_string, but for group name.
 * If lookup fails, will return the gid printed as a string.
 * NOTE: xfree the return value
 */
extern char *gid_to_string(gid_t gid);

/*
 * Translate gid to user name.
 * Will return NULL on error.
 * NOTE: xfree the return value.
 */
extern char *gid_to_string_or_null(gid_t gid);

#endif /*__SLURM_UID_UTILITY_H__*/
