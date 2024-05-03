/*****************************************************************************\
 *  front_end.h - FRONT_END parameters and data structures
 *****************************************************************************
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

#ifndef _SLURM_FRONT_END_RECORD_H
#define _SLURM_FRONT_END_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "slurm/slurm.h"

#define FRONT_END_MAGIC 0xfe9b82fe

typedef struct {
	uint32_t magic;			/* magic cookie to test data integrity */
					/* DO NOT ALPHABETIZE */
	gid_t *allow_gids;		/* zero terminated list of allowed groups */
	char *allow_groups;		/* allowed group string */
	uid_t *allow_uids;		/* zero terminated list of allowed users */
	char *allow_users;		/* allowed user string */
	time_t boot_time;		/* Time of node boot,
					 * computed from up_time */
	char *comm_name;		/* communications path name to node */
	gid_t *deny_gids;		/* zero terminated list of denied groups */
	char *deny_groups;		/* denied group string */
	uid_t *deny_uids;		/* zero terminated list of denied users */
	char *deny_users;		/* denied user string */
	uint32_t job_cnt_comp;		/* count of completing jobs on node */
	uint16_t job_cnt_run;		/* count of running or suspended jobs */
	time_t last_response;		/* Time of last communication */
	char *name;			/* frontend node name */
	uint32_t node_state;		/* enum node_states, ORed with
					 * NODE_STATE_NO_RESPOND if not
					 * responding */
	bool not_responding;		/* set if fails to respond,
					 * clear after logging this */
	slurm_addr_t slurm_addr;	/* network address */
	uint16_t port;			/* frontend specific port */
	uint16_t protocol_version;	/* Slurm version number */
	char *reason;			/* reason for down frontend node */
	time_t reason_time;		/* Time stamp when reason was set,
					 * ignore if no reason is set. */
	uint32_t reason_uid;   		/* User that set the reason, ignore if
					 * no reason is set. */
	time_t slurmd_start_time;	/* Time of slurmd startup */
	char *version;			/* Slurm version */
} front_end_record_t;

extern front_end_record_t *front_end_nodes;
extern uint16_t front_end_node_cnt;
extern time_t last_front_end_update;	/* time of last front_end update */

#endif /* _SLURM_FRONT_END_RECORD_H */
