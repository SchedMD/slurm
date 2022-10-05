/*****************************************************************************\
 *  cron.h
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#ifndef _COMMON_CRON_H_
#define _COMMON_CRON_H_

#include <time.h>

#include "src/common/bitstring.h"
#include "src/common/pack.h"

typedef enum {
	CRON_WILD_MINUTE = 1 << 1,
	CRON_WILD_HOUR = 1 << 2,
	CRON_WILD_DOM = 1 << 3,
	CRON_WILD_MONTH = 1 << 4,
	CRON_WILD_DOW = 1 << 5,
} cron_entry_flag_t;

typedef struct {
	uint32_t flags;
	bitstr_t *minute;
	bitstr_t *hour;
	bitstr_t *day_of_month;
	bitstr_t *month;
	bitstr_t *day_of_week;
	char *cronspec;
	char *command;
	uint32_t line_start;	/* start of this entry in file */
	uint32_t line_end;	/* end of this entry in file */
} cron_entry_t;

extern cron_entry_t *new_cron_entry(void);
/*
 * Function signature adjusted for use with list_create().
 */
extern void free_cron_entry(void *entry);

extern bool valid_cron_entry(cron_entry_t *entry);

extern char *cronspec_from_cron_entry(cron_entry_t *entry);

/*
 * Calculate the next starting time given a cron entry.
 * Always advances at least one minute into the future.
 */
extern time_t calc_next_cron_start(cron_entry_t *entry, time_t next);

/*
 * Function signatures adjusted for use with slurm_{pack,unpack}_list().
 */
extern void pack_cron_entry(void *entry, uint16_t protocol_version,
			    buf_t *buffer);
extern int unpack_cron_entry(void **entry_ptr, uint16_t protocol_version,
			     buf_t *buffer);

#endif
