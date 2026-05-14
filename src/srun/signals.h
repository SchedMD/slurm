/*****************************************************************************\
 *  signals.h - Signal handler logic for srun
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

#ifndef _SRUN_SIGNALS_H
#define _SRUN_SIGNALS_H

/* Called only once at the beginning of the process */
extern void srun_sig_init(void);

/*
 * If a signal comes in to destroy srun, this will be set to the signo used to
 * destroy srun. Otherwise, this will be set to 0.
 */
extern int srun_destroy_sig;

/* Set when SRUN_JOB_COMPLETE is received. */
extern bool srun_job_complete_recvd;

extern pthread_mutex_t srun_destroy_sig_lock;

extern int srun_sig_eventfd;

/*
 * True if signals should be forwarded to running job. Otherwise, srun's
 * internal signal handler will be used
 */
extern bool srun_sig_forward;
extern pthread_mutex_t srun_sig_forward_lock;

/* Returns true if signo is a signal handled by srun */
extern bool srun_sig_is_handled(int signo);

/* Returns true if signo may be specified to --ignore-signals */
extern bool srun_sig_is_ignorable(int signo);

#endif /* _SRUN_SIGNALS_H */
