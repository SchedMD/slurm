/*****************************************************************************\
 *  src/plugins/task/affinity/schedutils.c - scheduling utilities
 *  $Id: schedutils.c,v 1.2 2005/11/04 02:46:51 palermo Exp $
 *****************************************************************************
 *  Routines in this file are taken from the taskset utility (schedutils pkg)
 *  Copyright (C) 2004 Robert Love
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
#include <ctype.h>
#include "affinity.h"

/*
 * taskset.c - taskset
 * Command-line utility for setting and retrieving a task's CPU affinity
 *
 * Robert Love <rml@tech9.net>		25 April 2002
 *
 * Linux kernels as of 2.5.8 provide the needed syscalls for
 * working with a task's cpu affinity.  Currently 2.4 does not
 * support these syscalls, but patches are available at:
 *
 * 	http://www.kernel.org/pub/linux/kernel/people/rml/cpu-affinity/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, v2, as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Copyright (C) 2004 Robert Love
 */

inline int val_to_char(int v)
{
	if (v >= 0 && v < 10)
		return '0' + v;
	else if (v >= 10 && v < 16)
		return ('a' - 10) + v;
	else 
		return -1;
}

inline int char_to_val(int c)
{
	int cl;

	cl = tolower(c);
	if (c >= '0' && c <= '9')
		return c - '0';
	else if (cl >= 'a' && cl <= 'f')
		return cl + (10 - 'a');
	else
		return -1;
}

int str_to_cpuset(cpu_set_t *mask, const char* str)
{
	int len = strlen(str);
	const char *ptr = str + len - 1;
	int base = 0;

	/* skip 0x, it's all hex anyway */
	if (len > 1 && !memcmp(str, "0x", 2L))
		str += 2;

	CPU_ZERO(mask);
	while (ptr >= str) {
		char val = char_to_val(*ptr);
		if (val == (char) -1)
			return -1;
		if (val & 1)
			CPU_SET(base, mask);
		if (val & 2)
			CPU_SET(base + 1, mask);
		if (val & 4)
			CPU_SET(base + 2, mask);
		if (val & 8)
			CPU_SET(base + 3, mask);
		len--;
		ptr--;
		base += 4;
	}

	return 0;
}

char * cpuset_to_str(const cpu_set_t *mask, char *str)
{
	int base;
	char *ptr = str;
	char *ret = NULL;

	for (base = CPU_SETSIZE - 4; base >= 0; base -= 4) {
		char val = 0;
		if (CPU_ISSET(base, mask))
			val |= 1;
		if (CPU_ISSET(base + 1, mask))
			val |= 2;
		if (CPU_ISSET(base + 2, mask))
			val |= 4;
		if (CPU_ISSET(base + 3, mask))
			val |= 8;
		if (!ret && val)
			ret = ptr;
		*ptr++ = val_to_char(val);
	}
	*ptr = '\0';
	return ret ? ret : ptr - 1;
}

