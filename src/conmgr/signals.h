/*****************************************************************************\
 *  signals.h - Internal declarations for signals handlers
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

#ifndef _CONMGR_SIGNALS_H
#define _CONMGR_SIGNALS_H

#include <stdbool.h>
#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

/* start the signal manager */
extern void signal_mgr_start(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * signal the signal manager to stop
 * Note: Caller must lock mgr.mutex
 */
extern void signal_mgr_stop(void);

/*
 * Add signal work to signal manager
 * IN work - work with depend=CONMGR_WORK_DEP_SIGNAL.
 * 	Takes ownership of work.
 * 	Work ptr is never released.
 */
extern void add_work_signal(work_t *work);

/* Return true if con is signal handling connection */
extern bool is_signal_connection(conmgr_fd_t *con);

/*
 * Returns true if connection has any pending incoming data that hasn't been
 * processed yet
 * WARNING: caller must hold mgr.mutex
 * WARNING: makes blocking call for FIONREAD
 */
extern bool signal_mgr_has_incoming(void);

#endif /* _CONMGR_SIGNALS_H */
