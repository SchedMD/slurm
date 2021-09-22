/*****************************************************************************\
 *  prog7.24.prog.c - SPANK plugin for testing purposes
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
 *  Written by Nathan Rini <nate@schedmd.com>
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

#include <slurm/spank.h>

/*
 * All spank plugins must define this macro for the Slurm plugin loader.
 */
SPANK_PLUGIN(test_suite, 1);

static const char *_ctxt(const spank_t sp)
{
	switch (spank_context())
	{
	case S_CTX_ERROR:
		return "error";
	case S_CTX_LOCAL:
		return "local";
	case S_CTX_REMOTE:
		return "remote";
	case S_CTX_ALLOCATOR:
		return "allocator";
	case S_CTX_SLURMD:
		return "slurmd";
	case S_CTX_JOB_SCRIPT:
		return "job_script";
	}

	return "INVALID";
}

#define sptest(name)                                                      \
	extern int name(spank_t sp, int ac, char **av)                    \
	{                                                                 \
		char ctxt[256] = {0};                                     \
		char f[256] = {0};                                        \
		uint32_t jobid = 0;                                       \
									  \
		spank_get_item(sp, S_JOB_ID, &jobid);                     \
		if (spank_context() == S_CTX_REMOTE) {                    \
			if (spank_getenv(sp, "TEST_CTXT", ctxt,           \
				     (sizeof(ctxt) - 1)) ||               \
			spank_getenv(sp, "TEST_FUNC", f,                  \
				     (sizeof(f) - 1)))                    \
					 return ESPANK_SUCCESS;           \
		} else {                                                  \
			char *ctxtp = getenv("TEST_CTXT");                \
			char *fp = getenv("TEST_FUNC");                   \
									  \
			if (!ctxtp || !fp)                                \
				return ESPANK_SUCCESS;                    \
									  \
			strncpy(ctxt, ctxtp, (sizeof(ctxt) - 1));         \
			strncpy(f, fp, (sizeof(f) - 1));                  \
		}                                                         \
									  \
		if (strcmp(ctxt, _ctxt(sp)) || strcmp(f, __func__)) {     \
			slurm_spank_log("[Job: %u] Looking for (%s,%s) but found (%s,%s). Continuing...", \
				jobid, f, ctxt, __func__, _ctxt(sp));     \
			return ESPANK_SUCCESS;                            \
		}                                                         \
		slurm_spank_log("[Job: %u] Found (%s,%s)",                \
				jobid, f, ctxt);                          \
									  \
		return -ESPANK_ERROR;                                     \
	}

sptest(slurm_spank_init)
sptest(slurm_spank_init_post_opt)
sptest(slurm_spank_local_user_init)
sptest(slurm_spank_task_init)
sptest(slurm_spank_task_post_fork)
sptest(slurm_spank_task_exit)
sptest(slurm_spank_exit)
sptest(slurm_spank_job_prolog)
sptest(slurm_spank_user_init)
sptest(slurm_spank_task_init_privileged)
sptest(slurm_spank_job_epilog)
sptest(slurm_spank_slurmd_exit)
