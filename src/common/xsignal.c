/*****************************************************************************\
 * src/common/xsignal.c - POSIX signal functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
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

#include <errno.h>
#include <signal.h>

#include "slurm/slurm_errno.h"

#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/proc_args.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"

#include "src/conmgr/conmgr.h"

/*
 * Define slurm-specific aliases for use by plugins, see slurm_xlator.h
 * for details.
 */
strong_alias(xsignal,		slurm_xsignal);
strong_alias(xsignal_save_mask,	slurm_xsignal_save_mask);
strong_alias(xsignal_set_mask,	slurm_xsignal_set_mask);
strong_alias(xsignal_block,	slurm_xsignal_block);
strong_alias(xsignal_unblock,	slurm_xsignal_unblock);
strong_alias(xsignal_sigset_create, slurm_xsignal_sigset_create);

SigFunc *
xsignal(int signo, SigFunc *f)
{
	struct sigaction sa, old_sa;

	if (conmgr_enabled())
		return SLURM_SUCCESS;

	sa.sa_handler = f;
	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, signo);
	sa.sa_flags = 0;

	if (sigaction(signo, &sa, &old_sa) < 0)
		error("xsignal(%d) failed: %m", signo);

	if (get_log_level() >= LOG_LEVEL_DEBUG4) {
		char *name = sig_num2name(signo);
		debug4("%s: Swap signal %s[%d] to 0x%"PRIxPTR" from 0x%"PRIxPTR,
		       __func__, name, signo, (uintptr_t) f,
		       (uintptr_t) old_sa.sa_handler);
		xfree(name);
	}

	return (old_sa.sa_handler);
}

extern SigFunc *xsignal_default(int sig)
{
	struct sigaction act;

	if (conmgr_enabled())
		return SLURM_SUCCESS;

	if (sigaction(sig, NULL, &act)) {
		error("sigaction(%d): %m", sig);
		return NULL;
	}
	if (act.sa_handler != SIG_IGN)
		return act.sa_handler;

	xsignal(sig, SIG_DFL);

	return act.sa_handler;
}

/*
 *  Wrapper for pthread_sigmask.
 */
static int
_sigmask(int how, sigset_t *set, sigset_t *oset)
{
	int err;

	if (conmgr_enabled())
		return SLURM_SUCCESS;

	if ((err = pthread_sigmask(how, set, oset)))
		return error ("pthread_sigmask: %s", slurm_strerror(err));

	return SLURM_SUCCESS;
}


/*
 *  Fill in the sigset_t with the list of signals in the
 *  (zero-terminated) array of signals `sigarray.'
 */
int
xsignal_sigset_create(int sigarray[], sigset_t *setp)
{
	if (conmgr_enabled())
		return SLURM_SUCCESS;

	int i = 0, sig;

	if (sigemptyset(setp) < 0)
		error("sigemptyset: %m");

	while ((sig = sigarray[i++])) {
		if (sigaddset(setp, sig) < 0)
			return error ("sigaddset(%d): %m", sig);
	}

	return SLURM_SUCCESS;
}

int
xsignal_save_mask(sigset_t *set)
{
	if (conmgr_enabled())
		return SLURM_SUCCESS;

	sigemptyset(set);
	return _sigmask(SIG_SETMASK, NULL, set);
}

int
xsignal_set_mask(sigset_t *set)
{
	if (conmgr_enabled())
		return SLURM_SUCCESS;

	return _sigmask(SIG_SETMASK, set, NULL);
}

int
xsignal_block(int sigarray[])
{
	sigset_t set;

	if (conmgr_enabled())
		return SLURM_SUCCESS;

	xassert(sigarray != NULL);

	if (xsignal_sigset_create(sigarray, &set) < 0)
		return SLURM_ERROR;

	return _sigmask(SIG_BLOCK, &set, NULL);
}

int
xsignal_unblock(int sigarray[])
{
	sigset_t set;

	if (conmgr_enabled())
		return SLURM_SUCCESS;

	xassert(sigarray != NULL);

	if (xsignal_sigset_create(sigarray, &set) < 0)
		return SLURM_ERROR;

	return _sigmask(SIG_UNBLOCK, &set, NULL);
}
