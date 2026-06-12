/*****************************************************************************\
 *  common.c - definitions for functions common to all modules in scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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

#include "scontrol.h"

/*
 * scontrol_process_plus_minus - convert a string like
 *       Users+=a,b,c
 *  to   Users=+a,+b,+c
 *
 *  or if nodestr true
 *       Nodes+=h1[1,3],h[5-10]
 *  to   Nodes=+h1[1,3],+h[5-10]
 *
 * IN plus_or_minus - '+' or '-'
 * IN src - source string
 * IN nodestr - process as node string
 * RET converted string
 */
extern char *scontrol_process_plus_minus(char plus_or_minus, char *src,
					 bool nodestr)
{
	int num_commas = 0;
	int i;
	int srclen = strlen(src);
	char *dst, *ret;

	for (i = 0; i < srclen; i++) {
		/*
		 * for a node string a comma within a bracketed
		 * expression is expected to have a digit following so
		 * only add plus/minus if this is not the case
		 */
		if ((src[i] == ',') && (!nodestr || !isdigit(src[i + 1])))
			num_commas++;
	}
	ret = dst = xmalloc(srclen + 2 + num_commas);

	*dst++ = plus_or_minus;
	for (i = 0; i < srclen; i++) {
		if ((*src == ',') && (!nodestr || !isdigit(src[1]))) {
			*dst++ = *src++;
			*dst++ = plus_or_minus;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';

	return ret;
}
