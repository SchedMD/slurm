/*****************************************************************************\
 *  delayed.h - Internal declarations for delayed work handlers
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

#ifndef _CONMGR_DELAYED_H
#define _CONMGR_DELAYED_H

#include "src/conmgr/conmgr.h"
#include "src/conmgr/mgr.h"

/*
 * Set all time delayed work as cancelled and run queue to run
 */
extern void cancel_delayed_work(void);

extern void init_delayed_work(void);
extern void free_delayed_work(void);

extern void on_signal_alarm(conmgr_callback_args_t conmgr_args, void *arg);

/*
 * Enqueue new delayed work
 * Caller must hold mgr.mutex lock
 */
extern void add_work_delayed(work_t *work);

/*
 * Create string describing delayed work for logging
 * IN work - delayed work to describe
 * RET log string (caller must xfree())
 */
extern char *work_delayed_to_str(work_t *work);

#endif /* _CONMGR_DELAYED_H */
