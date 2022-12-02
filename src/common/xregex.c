/*****************************************************************************\
 *  xregex.c - regular expressions
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xregex.h"

extern void dump_regex_error(int rc, const regex_t *regex_ptr, char *fmt, ...)
{
	va_list ap;
	char *buffer = NULL, *desc;
	size_t len = regerror(rc, regex_ptr, NULL, 0);

	va_start(ap, fmt);
	desc = vxstrfmt(fmt, ap);
	va_end(ap);

	if (len == 0) {
		error("%s: %s: unknown regex error code %d",
		      __func__, desc, rc);
		xfree(desc);
		return;
	}

	buffer = xmalloc(len);
	len = regerror(rc, regex_ptr, buffer, len);

	if (len)
		error("%s: %s: %s", __func__, desc, buffer);
	else
		error("%s: %s: unexpected failure to get regex error",
		      __func__, desc);

	xfree(buffer);
	xfree(desc);
}

extern bool regex_quick_match(const char *str, const regex_t *regex_ptr)
{
	int rc;
	regmatch_t pmatch[1];

	/* not possible to match a NULL string */
	if (!str)
		return false;

	rc = regexec(regex_ptr, str, 1, pmatch, 0);
	if (!rc) { /* matched */
		return true;
	} else if (rc == REG_NOMATCH) {
		return false;
	} else { /* other error */
		dump_regex_error(rc, regex_ptr, "%s(%s)", __func__, str);
		return false;
	}
}
