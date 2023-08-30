/*****************************************************************************\
 **  pmix_debug.h - PMIx debug primitives
 *****************************************************************************
 *  Copyright (C) 2014-2015 Artem Polyakov. All rights reserved.
 *  Copyright (C) 2015      Mellanox Technologies. All rights reserved.
 *  Written by Artem Polyakov <artpol84@gmail.com, artemp@mellanox.com>.
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
#ifndef PMIXP_DEBUG_H
#define PMIXP_DEBUG_H

#include "pmixp_common.h"
#include "pmixp_info.h"

#define PMIXP_DEBUG(format, args...) {				\
	debug("%s [%d]: %s:%d: " format "",			\
	      pmixp_info_hostname(), pmixp_info_nodeid(),	\
	      THIS_FILE, __LINE__, ## args);			\
}

#define PMIXP_ERROR_STD(format, args...) {			\
	error(" %s: %s: %s [%d]: %s:%d: " format ": %s (%d)",	\
	      plugin_type, __func__, pmixp_info_hostname(),	\
	      pmixp_info_nodeid(), THIS_FILE, __LINE__,		\
	      ## args, strerror(errno), errno);			\
}

#define PMIXP_ERROR(format, args...) {				\
	error(" %s: %s: %s [%d]: %s:%d: " format,		\
	      plugin_type, __func__, pmixp_info_hostname(),	\
	      pmixp_info_nodeid(), THIS_FILE, __LINE__,		\
	      ## args);						\
}

#define PMIXP_ABORT(format, args...) {				\
	PMIXP_ERROR(format, ##args);				\
	slurm_kill_job_step(pmixp_info_jobid(),			\
			    pmixp_info_stepid(), SIGKILL);	\
}

#define PMIXP_ERROR_NO(err, format, args...) {			\
	error(" %s: %s: %s [%d]: %s:%d: " format ": %s (%d)",	\
	      plugin_type, __func__, pmixp_info_hostname(),	\
	      pmixp_info_nodeid(), THIS_FILE, __LINE__,		\
	      ## args, strerror(err), err);			\
}

#ifdef NDEBUG
#define pmixp_debug_hang(x)
#else
static inline void _pmixp_debug_hang(int delay)
{
	while (delay) {
		sleep(1);
	}
}

#define pmixp_debug_hang(x) _pmixp_debug_hang(x)

#endif
#endif /* PMIXP_DEBUG_H */
