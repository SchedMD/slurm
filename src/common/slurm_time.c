/*****************************************************************************\
 *  time.h - Slurm wrappers for the glibc time functions. Unlike the glibc
 *  functions, these are re-entrant. If a process is forked while glibc is
 *  in a lock, the child process will deadlock if it tries to use another
 *  glibc function, but not with these functions.
 *
 *  Based upon glibc version 2.21 and the fork handler logic from Slurm.
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

#include <pthread.h>
#include <time.h>

#include "src/common/macros.h"

static pthread_mutex_t  time_lock = PTHREAD_MUTEX_INITIALIZER;
static void _atfork_child()  { slurm_mutex_init(&time_lock); }
static bool at_forked = false;

inline static void _init(void)
{
	while (!at_forked) {
		pthread_atfork(NULL, NULL, _atfork_child);
		at_forked = true;
	}
}

extern char *slurm_ctime(const time_t *timep)
{
	char *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = ctime(timep);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern char *slurm_ctime_r(const time_t *timep, char *buf)
{
	char *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = ctime_r(timep, buf);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern struct tm *slurm_gmtime(const time_t *timep)
{
	struct tm *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = gmtime(timep);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern struct tm *slurm_gmtime_r(const time_t *timep, struct tm *result)
{
	struct tm *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = gmtime_r(timep, result);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern struct tm *slurm_localtime(const time_t *timep)
{
	struct tm *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = localtime(timep);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern struct tm *slurm_localtime_r(const time_t *timep, struct tm *result)
{
	struct tm *rc;
	slurm_mutex_lock(&time_lock);
	_init();
	rc = localtime_r(timep, result);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

extern time_t slurm_mktime(struct tm *tp)
{
	time_t rc;
	slurm_mutex_lock(&time_lock);
	_init();
	/* Force tm_isdt to -1. */
	tp->tm_isdst = -1;
	rc = mktime(tp);
	slurm_mutex_unlock(&time_lock);
	return rc;
}

/* Slurm variants of ctime and ctime_r without a trailing new-line */
extern char *slurm_ctime2(const time_t *timep)
{
	static char time_str[25];

	strftime(time_str, sizeof(time_str), "%a %b %d %T %Y",
		 slurm_localtime(timep));

	return time_str;
}

extern char *slurm_ctime2_r(const time_t *timep, char *time_str)
{
	struct tm newtime;
	slurm_localtime_r(timep, &newtime);

	strftime(time_str, 25, "%a %b %d %T %Y", &newtime);

	return time_str;
}
