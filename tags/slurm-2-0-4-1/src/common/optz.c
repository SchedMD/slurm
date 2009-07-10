/*****************************************************************************\
 * optz.c - dynamic option table handling
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <slurm/slurm_errno.h>

#include "src/common/optz.h"
#include "src/common/xmalloc.h"

static const struct option opt_table_end = { NULL, 0, NULL, 0 };

struct option *optz_create(void)
{
	struct option *optz = xmalloc(sizeof(*optz));
	optz[0] = opt_table_end;
	return (optz);
}

void optz_destroy(struct option *optz)
{
	xfree(optz);
	return;
}

int optz_add(struct option **optz, const struct option *opt)
{
	int len = 0;
	struct option *op = *optz;
	struct option *t = *optz;

	for (; op->name != NULL; op++) {
		if (strcmp(op->name, opt->name) == 0)
			slurm_seterrno_ret(EEXIST);
		len++;
	}

	++len;			/* Add one for incoming option */

	t = xrealloc(t, (len + 1) * sizeof(struct option));

	t[len - 1] = *opt;
	t[len] = opt_table_end;

	*optz = t;

	return (0);
}

int optz_append(struct option **optz, const struct option *opts)
{
	int len1 = 0;
	int len2 = 0;
	int i;
	const struct option *op;
	struct option *t = *optz;

	if (opts == NULL)
		return (0);

	for (op = *optz; op && op->name != NULL; op++)
		len1++;

	for (op = opts; op && op->name != NULL; op++)
		len2++;

	t = xrealloc(t, (len1 + len2 + 2) * sizeof(struct option));

	i = len1;
	for (op = opts; op->name != NULL; op++, i++)
		t[i] = *op;

	t[i] = opt_table_end;

	*optz = t;

	return (0);
}
