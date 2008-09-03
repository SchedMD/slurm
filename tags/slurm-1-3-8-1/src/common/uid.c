/*****************************************************************************\
 * src/common/uid.c - uid/gid lookup utility functions
 * $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  LLNL-CODE-402394.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
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

#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

uid_t
uid_from_string (char *name)
{
	struct passwd pwd, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	int rc;
	uid_t uid = (uid_t) strtoul (name, &p, 10);

	if (*p != '\0') {
		while (1) {
			rc = getpwnam_r(name, &pwd, buffer, PW_BUF_SIZE, 
					&result);
			if (rc == EINTR)
				continue;
			if (rc != 0)
				result = NULL;
			break;
		}
		if (result == NULL)
			uid = (uid_t) -1;
		else
			uid = result->pw_uid;
	} else {
		while (1) {
			rc = getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, 
					&result);
			if (rc == EINTR)
				continue;
			if (rc != 0)
				result = NULL;
			break;
		}
		if (result == NULL)
			uid = (uid_t) -1;
		/* else uid is already correct */
	}
	return uid; 
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

	while (1) {
		rc = getpwuid_r(uid, &pwd, buffer, PW_BUF_SIZE, &result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			result = NULL;
		break;
	}
	if (result)
		ustring = xstrdup(result->pw_name);
	else
		ustring = xstrdup("nobody");
	return ustring;
}

gid_t
gid_from_string (char *name)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *p = NULL;
	int rc;
	gid_t gid = (gid_t) strtoul (name, &p, 10);

	if (*p != '\0') {
		while (1) {
			rc = getgrnam_r(name, &grp, buffer, PW_BUF_SIZE, 
					&result);
			if (rc == EINTR)
				continue;
			if (rc != 0)
				result = NULL;
			break;
		}
		if (result == NULL)
			gid = (gid_t) -1;
		else
			gid = result->gr_gid;
	} else {
		while (1) {
			rc = getgrgid_r(gid, &grp, buffer, PW_BUF_SIZE, 
					&result);
			if (rc == EINTR)
				continue;
			if (rc != 0)
				result = NULL;
			break;
		}
		if (result == NULL)
			gid = (gid_t) -1;
		/* else gid is already correct */
	}
	return gid; 
}

char *
gid_to_string (gid_t gid)
{
	struct group grp, *result;
	char buffer[PW_BUF_SIZE], *gstring;
	int rc;

	while (1) {
		rc = getgrgid_r(gid, &grp, buffer, PW_BUF_SIZE, &result);
		if (rc == EINTR)
			continue;
		if (rc != 0)
			result = NULL;
		break;
	}
	if (result)
		gstring = xstrdup(result->gr_name);
	else
		gstring = xstrdup("nobody");
	return gstring;
}
