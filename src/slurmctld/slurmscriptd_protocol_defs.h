/*****************************************************************************\
 *  slurmscriptd_protocol_defs.h - definitions used for slurmscriptd RPCs.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
 *  Written by Marshall Garey <marshall@schedmd.com>
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
#ifndef _SLURMSCRIPTD_PROTOCOL_DEFS_H
#define _SLURMSCRIPTD_PROTOCOL_DEFS_H

#include <stdbool.h>
#include <stdint.h>

#include "src/common/slurm_protocol_defs.h"

/* slurmscriptd message types are defined in slurm_msg_type_t */

typedef enum {
	SLURMSCRIPTD_NONE = 0, /* 0 so that initializing data to zero will init
				* to this. This is used for response messages
				* from RPC's that weren't running a script. */
	SLURMSCRIPTD_BB_LUA,
	SLURMSCRIPTD_EPILOG,
	SLURMSCRIPTD_MAIL,
	SLURMSCRIPTD_POWER,
	SLURMSCRIPTD_PROLOG,
	SLURMSCRIPTD_REBOOT,
	SLURMSCRIPTD_RESV,
} script_type_t;

typedef struct {
	char *key;
	void *msg_data;
	uint32_t msg_type;
} slurmscriptd_msg_t; /* Generic slurmscriptd msg */

typedef struct {
	uint32_t job_id;
	char *resp_msg;
	char *script_name;
	script_type_t script_type;
	bool signalled;
	int status;
	bool timed_out;
} script_complete_t;

typedef struct {
	uint32_t argc;
	char **argv;
	char **env;
	char *extra_buf;
	uint32_t extra_buf_size;
	uint32_t job_id;
	char *script_name;
	char *script_path;
	script_type_t script_type;
	uint32_t timeout;
	char *tmp_file_env_name;
	char *tmp_file_str;
} run_script_msg_t;

typedef struct {
	uint32_t job_id;
} flush_job_msg_t;

typedef struct {
	uint64_t debug_flags;
} debug_flags_msg_t;

typedef struct {
	uint32_t debug_level;
	bool log_rotate;
} log_msg_t;

typedef struct {
	uint64_t debug_flags;
	char *logfile;
	uint16_t log_fmt;
	uint16_t slurmctld_debug;
	uint16_t syslog_debug;
} reconfig_msg_t;

/* Free message functions */
extern void slurmscriptd_free_reconfig(reconfig_msg_t *msg);
extern void slurmscriptd_free_run_script_msg(run_script_msg_t *msg);
extern void slurmscriptd_free_script_complete(script_complete_t *msg);

/*
 * This function checks msg->msg_type and frees msg->msg_data accordingly.
 * This function does not free msg, however, since we don't know if msg
 * was malloc'd.
 */
extern void slurmscriptd_free_msg(slurmscriptd_msg_t *msg);

#endif /* _SLURMSCRIPTD_PROTOCOL_DEFS_H */
