/*****************************************************************************\
 *  slurm_acct_gather.h - generic interface needed for some
 *                        acct_gather plugins.
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#ifndef __SLURM_ACCT_GATHER_H__
#define __SLURM_ACCT_GATHER_H__

#include <inttypes.h>

#include "src/common/read_config.h"

#include "acct_gather_profile.h"

typedef struct acct_gather_data {
	uint64_t last_time; /* holder for last time data was read */
	uint64_t	num_reads;  /* count of reads */
	uint64_t	num_writes; /* count of writes */
	uint64_t	size_read;  /* raw amount read (in) */
	uint64_t	size_write; /* raw amount written (out) */
} acct_gather_data_t;

extern int acct_gather_conf_init(void);
extern int acct_gather_write_conf(int fd);
extern int acct_gather_read_conf(int fd);
extern int acct_gather_reconfig(void);
extern int acct_gather_conf_destroy(void);

/* don't forget to free this */
extern List acct_gather_conf_values(void);
extern int acct_gather_parse_freq(int type, char *freq);
extern int acct_gather_check_acct_freq_task(uint64_t job_mem_lim,
					    char *acctg_freq);
extern void acct_gather_suspend_poll(void);
extern void acct_gather_resume_poll(void);
extern bool acct_gather_suspend_test(void);

#endif
