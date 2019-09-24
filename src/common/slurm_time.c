/*****************************************************************************\
 *  slurm_time.c - assorted time functions
 *****************************************************************************
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

#include <stdio.h>
#include <time.h>

extern time_t slurm_mktime(struct tm *tp)
{
	/* Force tm_isdt to -1. */
	tp->tm_isdst = -1;
	return mktime(tp);
}

/* Slurm variants of ctime and ctime_r without a trailing new-line */
extern char *slurm_ctime2(const time_t *timep)
{
	struct tm newtime;
	static char time_str[25];
	localtime_r(timep, &newtime);

	strftime(time_str, sizeof(time_str), "%a %b %d %T %Y", &newtime);

	return time_str;
}

extern char *slurm_ctime2_r(const time_t *timep, char *time_str)
{
	struct tm newtime;
	localtime_r(timep, &newtime);

	strftime(time_str, 25, "%a %b %d %T %Y", &newtime);

	return time_str;
}

extern void print_date(void)
{
	time_t now = time(NULL);
	char time_str[25];

	printf("%s\n", slurm_ctime2_r(&now, time_str));
}
