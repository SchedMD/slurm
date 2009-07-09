/*****************************************************************************\
 * src/common/uid.c - uid/gid lookup utility functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int _getpwnam_r (const char *name, struct passwd *pwd, char *buf,
		size_t bufsiz, struct passwd **result)
{
	int rc;
	while (1) {
		rc = getpwnam_r(name, pwd, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return (rc);
}

static int _getpwuid_r (uid_t uid, struct passwd *pwd, char *buf,
		size_t bufsiz, struct passwd **result)
{
	int rc;
	while (1) {
		rc = getpwuid_r(uid, pwd, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return rc;
}

uid_t
uid_from_string (char *name)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	long l;

	/*
	 *  Check to see if name is a valid username first.
	 */
	if ((_getpwnam_r (name, &pwd, buffer, PW_BUF_SIZE, &result) == 0)
	    && result != NULL)
		return result->pw_uid;

	/*
	 *  If username was not valid, check for a valid UID.
	 */
	errno = 0;
	l = strtol (name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX)))
	   || (name == p)
	   || (*p != '\0')
	   || (l < 0)
	   || (l > INT_MAX))
		return (uid_t) -1;

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	if (_getpwuid_r (l, &pwd, buffer, PW_BUF_SIZE, &result) == 0)
		return (uid_t) l;

	return (uid_t) -1;
}

char *
uid_to_string (uid_t uid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE], *ustring;
	int rc;

	/* Suse Linux does not handle multiple users with UID=0 well */
	if (uid == 0)
		return xstrdup("root");

	rc = getpwuid_r (uid, &pwd, buffer, PW_BUF_SIZE, &result);

	if (result)
		ustring = xstrdup(result->pw_name);
	else
		ustring = xstrdup("nobody");
	return ustring;
}

gid_t
gid_from_uid (uid_t uid)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE];
	gid_t gid;
	int rc;

	rc = getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
	if (result == NULL) {
		gid = (gid_t) -1;
	} else {
		gid = result->pw_gid;
	}

	return gid;
}

static int _getgrnam_r (const char *name, struct group *grp, char *buf,
		size_t bufsiz, struct group **result)
{
	int rc;
	while (1) {
		rc = getgrnam_r (name, grp, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return (rc);
}

static int _getgrgid_r (gid_t gid, struct group *grp, char *buf,
		size_t bufsiz, struct group **result)
{
	int rc;
	while (1) {
		rc = getgrgid_r (gid, grp, buf, bufsiz, result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			*result = NULL;
		break;
	}
	return rc;
}

gid_t
gid_from_string (char *name)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	long l;

	/*
	 *  Check for valid group name first.
	 */
	if ((_getgrnam_r (name, &grp, buffer, PW_BUF_SIZE, &result) == 0)
	    && result != NULL)
		return result->gr_gid;

	/*
	 *  If group name was not valid, perhaps it is a  valid GID.
	 */
	errno = 0;
	l = strtol (name, &p, 10);
	if (((errno == ERANGE) && ((l == LONG_MIN) || (l == LONG_MAX)))
	   || (name == p)
	   || (*p != '\0')
	   || (l < 0)
	   || (l > INT_MAX))
		return (gid_t) -1;

	/*
	 *  Now ensure the supplied uid is in the user database
	 */
	if ((_getgrgid_r (l, &grp, buffer, PW_BUF_SIZE, &result) == 0)
	    && result != NULL)
		return (gid_t) l;

	return (gid_t) -1;
}

char *
gid_to_string (gid_t gid)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *gstring;
	int rc;

	rc = _getgrgid_r(gid, &grp, buffer, PW_BUF_SIZE, &result);
	if (rc == 0 && result)
		gstring = xstrdup(result->gr_name);
	else
		gstring = xstrdup("nobody");
	return gstring;
}
