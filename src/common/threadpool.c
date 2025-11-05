/*****************************************************************************\
 *  threadpool.c - Thread Pool
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include <pthread.h>
#include <stdint.h>

#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/threadpool.h"

extern int threadpool_join(const pthread_t id, const char *caller)
{
	int rc = EINVAL;
	void *ret = 0;

	if (!id) {
		log_flag(THREAD, "%s->%s: Ignoring invalid pthread id=0x0",
		       caller, __func__);
		return SLURM_SUCCESS;
	}

	if ((rc = pthread_join(id, &ret))) {
		error("%s->%s: pthread_join(id=0x%"PRIx64") failed: %m",
		      caller, __func__, (uint64_t) id);
		return rc;
	}

	if (ret == PTHREAD_CANCELED)
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" was cancelled",
		       caller, __func__, (uint64_t) id);
	else
		log_flag(THREAD, "%s->%s: pthread id=0x%"PRIx64" returned: 0x%"PRIxPTR,
		       caller, __func__, (uint64_t) id, (uintptr_t) ret);

	return rc;
}
