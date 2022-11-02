/*****************************************************************************\
 *  create_res.c - reservation creation function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bremer <dbremer@llnl.gov>
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

#include "src/common/proc_args.h"
#include "src/common/state_control.h"
#include "src/scontrol/scontrol.h"
#include "src/slurmctld/reservation.h"

#define PLUS_MINUS(sign) (((sign == '+')) ? RESERVE_FLAG_DUR_PLUS : \
			  ((sign == '-') ? RESERVE_FLAG_DUR_MINUS : 0))

/*
 *  _process_plus_minus is used to convert a string like
 *       Users+=a,b,c
 *  to   Users=+a,+b,+c
 */

static char * _process_plus_minus(char plus_or_minus, char *src)
{
	int num_commas = 0;
	int ii;
	int srclen = strlen(src);
	char *dst, *ret;

	for (ii=0; ii<srclen; ii++) {
		if (src[ii] == ',')
			num_commas++;
	}
	ret = dst = xmalloc(srclen + 2 + num_commas);

	*dst++ = plus_or_minus;
	for (ii=0; ii<srclen; ii++) {
		if (*src == ',') {
			*dst++ = *src++;
			*dst++ = plus_or_minus;
		} else {
			*dst++ = *src++;
		}
	}
	*dst = '\0';

	return ret;
}

/*
 * _parse_res_options   parse options for creating or updating a reservation
 * IN argc - count of arguments
 * IN argv - list of arguments
 * IN msg  - a string to append to any error message
 * OUT resv_msg_ptr - struct holding reservation parameters
 * OUT res_free_flags - uint32_t of flags to set various bits to free strings
 *                      afterwards if needed.
 *                      See RESV_FREE_STR_* in src/common/slurm_protocol_defs.h.
 * RET 0 on success, -1 on err and prints message
 */
static int _parse_res_options(int argc, char **argv, const char *msg,
			      resv_desc_msg_t  *resv_msg_ptr,
			      uint32_t *res_free_flags)
{
	int i;
	int duration = -3;   /* -1 == INFINITE, -2 == error, -3 == not set */
	char *err_msg = NULL;

	*res_free_flags = 0;

	for (i=0; i<argc; i++) {
		char *tag = argv[i];
		int taglen = 0;
		char plus_minus = '\0';

		char *val = strchr(argv[i], '=');
		taglen = val - argv[i];

		if (!val && xstrncasecmp(argv[i], "res", 3) == 0) {
			continue;
		}

		if (val) {
			if (val[-1] == '+' || val[-1] == '-') {
				plus_minus = val[-1];
				taglen--;
			}
			val++;
		} else if (!xstrncasecmp(tag, "Skip", MAX(taglen, 2))) {
			if (resv_msg_ptr->flags == NO_VAL64)
				resv_msg_ptr->flags = RESERVE_FLAG_SKIP;
			else
				resv_msg_ptr->flags |= RESERVE_FLAG_SKIP;
			continue;
		} else {
			exit_code = 1;
			error("Misformatted parameter '%s', most options have a parameter after '='.  %s", argv[i], msg);
			return SLURM_ERROR;
		}

		if (!xstrncasecmp(tag, "Accounts", MAX(taglen, 1))) {
			if (resv_msg_ptr->accounts) {
				exit_code = 1;
				error("Parameter %s specified more than once",
				      argv[i]);
				return SLURM_ERROR;
			}
			if (plus_minus) {
				resv_msg_ptr->accounts =
					_process_plus_minus(plus_minus, val);
				*res_free_flags |= RESV_FREE_STR_ACCT;
				plus_minus = '\0';
			} else {
				resv_msg_ptr->accounts = val;
			}
		} else if (xstrncasecmp(tag, "Comment", MAX(taglen, 3)) == 0) {
			if (resv_msg_ptr->comment) {
				exit_code = 1;
				error("Parameter %s specified more than once",
				      argv[i]);
				return SLURM_ERROR;
			}
			resv_msg_ptr->comment = val;
		} else if (!xstrncasecmp(tag, "Flags", MAX(taglen, 2))) {
			uint64_t f;
			if (plus_minus) {
				char *tmp =
					_process_plus_minus(plus_minus, val);
				f = parse_resv_flags(tmp, msg, resv_msg_ptr);
				xfree(tmp);
				plus_minus = '\0';
			} else {
				f = parse_resv_flags(val, msg, resv_msg_ptr);
			}
			if (f == INFINITE64) {
				exit_code = 1;
				return SLURM_ERROR;
			}
		} else if (!xstrncasecmp(tag, "Groups", MAX(taglen, 1))) {
			if (resv_msg_ptr->groups) {
				exit_code = 1;
				error("Parameter %s specified more than once",
				      argv[i]);
				return SLURM_ERROR;
			}
			if (plus_minus) {
				resv_msg_ptr->groups =
					_process_plus_minus(plus_minus, val);
				*res_free_flags |= RESV_FREE_STR_GROUP;
				plus_minus = '\0';
			} else {
				resv_msg_ptr->groups = val;
			}

		} else if (!xstrncasecmp(tag, "Users", MAX(taglen, 1))) {
			if (resv_msg_ptr->users) {
				exit_code = 1;
				error("Parameter %s specified more than once",
				      argv[i]);
				return SLURM_ERROR;
			}
			if (plus_minus) {
				resv_msg_ptr->users =
					_process_plus_minus(plus_minus, val);
				*res_free_flags |= RESV_FREE_STR_USER;
				plus_minus = '\0';
			} else {
				resv_msg_ptr->users = val;
			}

		} else if (!xstrncasecmp(tag, "ReservationName",
			   MAX(taglen, 1))) {
			resv_msg_ptr->name = val;

		} else if (xstrncasecmp(tag, "BurstBuffer", MAX(taglen, 2))
			   == 0) {
			resv_msg_ptr->burst_buffer = val;

		} else if (xstrncasecmp(tag, "StartTime", MAX(taglen, 2)) == 0){
			time_t  t = parse_time(val, 0);
			if (errno == ESLURM_INVALID_TIME_VALUE) {
				exit_code = 1;
				error("Invalid start time %s.  %s",
				      argv[i], msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->start_time = t;

		} else if (xstrncasecmp(tag, "EndTime", MAX(taglen, 1)) == 0) {
			time_t  t = parse_time(val, 0);
			if (errno == ESLURM_INVALID_TIME_VALUE) {
				exit_code = 1;
				error("Invalid end time %s.  %s", argv[i],msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->end_time = t;

		} else if (xstrncasecmp(tag, "Duration", MAX(taglen, 1)) == 0) {
			/* -1 == INFINITE, -2 == error, -3 == not set */
			duration = time_str2mins(val);
			if (duration < 0 && duration != INFINITE) {
				exit_code = 1;
				error("Invalid duration %s.  %s", argv[i],msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->duration = (uint32_t)duration;
			if (plus_minus) {
				if (resv_msg_ptr->flags == NO_VAL64)
					resv_msg_ptr->flags =
						PLUS_MINUS(plus_minus);
				else
					resv_msg_ptr->flags |=
						PLUS_MINUS(plus_minus);
				plus_minus = '\0';
			}
		} else if (!xstrncasecmp(tag, "MaxStartDelay",
					 MAX(taglen, 2))) {
			duration = time_str2secs(val);

			if (duration < 0) {
				exit_code = 1;
				error("Invalid duration %s.  %s", argv[i], msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->max_start_delay = (uint32_t)duration;
		} else if (xstrncasecmp(tag, "NodeCnt", MAX(taglen,5)) == 0 ||
			   xstrncasecmp(tag, "NodeCount", MAX(taglen,5)) == 0) {

			if (parse_resv_nodecnt(resv_msg_ptr, val,
					       res_free_flags, false,
					       &err_msg) == SLURM_ERROR) {
				error("%s", err_msg);
				xfree(err_msg);
				exit_code = 1;
				return SLURM_ERROR;
			}

		} else if (xstrncasecmp(tag, "CoreCnt",   MAX(taglen,5)) == 0 ||
			   xstrncasecmp(tag, "CoreCount", MAX(taglen,5)) == 0 ||
			   xstrncasecmp(tag, "CPUCnt",    MAX(taglen,5)) == 0 ||
			   xstrncasecmp(tag, "CPUCount",  MAX(taglen,5)) == 0) {

			/* only have this on a cons_res machine */
			if (state_control_corecnt_supported()
			    != SLURM_SUCCESS) {
				error("CoreCnt or CPUCnt is only supported when SelectType includes select/cons_res or SelectTypeParameters includes OTHER_CONS_RES on a Cray.");
				exit_code = 1;
				return SLURM_ERROR;
			}

			if (state_control_parse_resv_corecnt(resv_msg_ptr, val,
							     res_free_flags,
							     false, &err_msg)
			    == SLURM_ERROR) {
				error("%s", err_msg);
				xfree(err_msg);
				exit_code = 1;
				return SLURM_ERROR;
			}

		} else if (xstrncasecmp(tag, "Nodes", MAX(taglen, 5)) == 0) {
			if (plus_minus) {
				resv_msg_ptr->node_list =
					_process_plus_minus(plus_minus, val);
				*res_free_flags |= RESV_FREE_STR_NODES;
				plus_minus = '\0';
			} else {
				resv_msg_ptr->node_list = val;
			}
		} else if (xstrncasecmp(tag, "Features", MAX(taglen, 2)) == 0) {
			resv_msg_ptr->features = val;

		} else if (xstrncasecmp(tag, "Licenses", MAX(taglen, 2)) == 0) {
			resv_msg_ptr->licenses = val;

		} else if (xstrncasecmp(tag, "PartitionName", MAX(taglen, 1))
			   == 0) {
			resv_msg_ptr->partition = val;

		} else if (xstrncasecmp(tag, "TRES", MAX(taglen, 1)) == 0) {
			if (state_control_parse_resv_tres(val, resv_msg_ptr,
							  res_free_flags,
							  &err_msg)
			    == SLURM_ERROR) {
				error("%s", err_msg);
				xfree(err_msg);
				exit_code = 1;
				return SLURM_ERROR;
			}

		} else if (xstrncasecmp(tag, "Watts", MAX(taglen, 1)) == 0) {
			if (state_control_parse_resv_watts(val, resv_msg_ptr,
							   &err_msg)
			    == SLURM_ERROR) {
				error("%s", err_msg);
				xfree(err_msg);
				exit_code = 1;
				return SLURM_ERROR;
			}
		} else if (xstrncasecmp(tag, "res", 3) == 0) {
			continue;
		} else {
			exit_code = 1;
			error("Unknown parameter %s.  %s", argv[i], msg);
			return SLURM_ERROR;
		}

		if (plus_minus != '\0') {
			exit_code = 1;
			error("The +=/-= notation is not supported when updating %.*s.  %s",
			      taglen, tag, msg);
			return SLURM_ERROR;
		}

	}

	return SLURM_SUCCESS;
}

/*
 * scontrol_update_res - update the slurm reservation configuration per the
 *     supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *     error message and returns 0.
 */
extern int
scontrol_update_res(int argc, char **argv)
{
	resv_desc_msg_t resv_msg;
	int ret = 0;
	uint32_t res_free_flags = 0;

	slurm_init_resv_desc_msg (&resv_msg);
	ret = _parse_res_options(argc, argv, "No reservation update.",
				 &resv_msg, &res_free_flags);
	if (ret)
		goto SCONTROL_UPDATE_RES_CLEANUP;

	if (resv_msg.name == NULL) {
		exit_code = 1;
		error("Reservation must be given.  No reservation update.");
		goto SCONTROL_UPDATE_RES_CLEANUP;
	}

	ret = slurm_update_reservation(&resv_msg);
	if (ret) {
		exit_code = 1;
		slurm_perror("Error updating the reservation");
		ret = slurm_get_errno();
	} else {
		printf("Reservation updated.\n");
	}

SCONTROL_UPDATE_RES_CLEANUP:

	slurm_free_resv_desc_msg_part(&resv_msg, res_free_flags);

	return ret;
}

/*
 * scontrol_create_res - create the slurm reservation configuration per the
 *     supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *     error message and returns 0.
 */
extern int
scontrol_create_res(int argc, char **argv)
{
	resv_desc_msg_t resv_msg;
	char *new_res_name = NULL;
	uint32_t res_free_flags = 0;
	int ret = 0;

	slurm_init_resv_desc_msg (&resv_msg);
	ret = _parse_res_options(argc, argv, "No reservation created.",
				 &resv_msg, &res_free_flags);

	if (ret)
		goto SCONTROL_CREATE_RES_CLEANUP;

	if (resv_msg.start_time == (time_t)NO_VAL) {
		exit_code = 1;
		error("A start time must be given.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.end_time == (time_t)NO_VAL &&
	    resv_msg.duration == NO_VAL) {
		exit_code = 1;
		error("An end time or duration must be given.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.end_time != (time_t)NO_VAL &&
	    resv_msg.duration != NO_VAL &&
	    resv_msg.start_time + resv_msg.duration*60 != resv_msg.end_time) {
		exit_code = 1;
		error("StartTime + Duration does not equal EndTime.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.start_time > resv_msg.end_time &&
	    resv_msg.end_time != (time_t)NO_VAL) {
		exit_code = 1;
		error("Start time cannot be after end time.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}

	/*
	 * If "ALL" is specified for the nodes and a partition is specified,
	 * only allocate all of the nodes the partition.
	 */
	if ((resv_msg.partition != NULL) && (resv_msg.node_list != NULL) &&
	    (xstrcasecmp(resv_msg.node_list, "ALL") == 0)) {
		if (resv_msg.flags == NO_VAL64)
			resv_msg.flags = RESERVE_FLAG_PART_NODES;
		else
			resv_msg.flags |= RESERVE_FLAG_PART_NODES;
	}

	/*
	 * If RESERVE_FLAG_PART_NODES is specified for the reservation,
	 * make sure a partition name is specified and nodes=ALL.
	 */
	if ((resv_msg.flags != NO_VAL64) &&
            (resv_msg.flags & RESERVE_FLAG_PART_NODES) &&
	    (!resv_msg.partition ||
	     (xstrcasecmp(resv_msg.node_list, "ALL")))) {
		exit_code = 1;
		error("PART_NODES flag requires specifying a Partition and ALL nodes.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}

	/*
	 * If the following parameters are null, but a partition is named, then
	 * make the reservation for the whole partition.
	 */
	if ((resv_msg.core_cnt == 0) &&
	    (resv_msg.burst_buffer == NULL ||
	     resv_msg.burst_buffer[0] == '\0') &&
	    (resv_msg.node_cnt  == NULL || resv_msg.node_cnt[0]  == 0)    &&
	    (resv_msg.node_list == NULL || resv_msg.node_list[0] == '\0') &&
	    (resv_msg.licenses  == NULL || resv_msg.licenses[0]  == '\0') &&
	    (resv_msg.resv_watts == NO_VAL)) {
		if (resv_msg.partition == NULL) {
			exit_code = 1;
			error("CoreCnt, Nodes, NodeCnt, BurstBuffer, Licenses or Watts must be specified.  No reservation created.");
			goto SCONTROL_CREATE_RES_CLEANUP;
		}
		if (resv_msg.flags == NO_VAL64)
			resv_msg.flags = RESERVE_FLAG_PART_NODES;
		else
			resv_msg.flags |= RESERVE_FLAG_PART_NODES;
		resv_msg.node_list = "ALL";
	}

	if ((resv_msg.users == NULL    || resv_msg.users[0] == '\0') &&
	    (resv_msg.groups == NULL   || resv_msg.groups[0] == '\0') &&
	    (resv_msg.accounts == NULL || resv_msg.accounts[0] == '\0')) {
		exit_code = 1;
		error("Either Users/Groups and/or Accounts must be specified.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	} else if (resv_msg.users && resv_msg.groups) {
		exit_code = 1;
		error("Users and Groups are mutually exclusive.  You can have one or the other, but not both.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}

	if (resv_msg.resv_watts != NO_VAL &&
	    (!(resv_msg.flags & RESERVE_FLAG_ANY_NODES) ||
	     (resv_msg.core_cnt != 0) ||
	     (resv_msg.node_cnt  != NULL && resv_msg.node_cnt[0]  != 0) ||
	     (resv_msg.node_list != NULL && resv_msg.node_list[0] != '\0') ||
	     (resv_msg.licenses  != NULL && resv_msg.licenses[0]  != '\0'))) {
		exit_code = 1;
		error("A power reservation must be empty and set the LICENSE_ONLY flag.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	new_res_name = slurm_create_reservation(&resv_msg);
	if (!new_res_name) {
		exit_code = 1;
		slurm_perror("Error creating the reservation");
		if ((errno == ESLURM_REQUESTED_NODE_CONFIG_UNAVAILABLE) ||
		    (errno == ESLURM_NODES_BUSY))
			printf("Note, unless nodes are directly requested a reservation must exist in a single partition.\n"
			       "If no partition is requested the default partition is assumed.\n");
		ret = slurm_get_errno();
	} else {
		printf("Reservation created: %s\n", new_res_name);
		free(new_res_name);
	}

SCONTROL_CREATE_RES_CLEANUP:

	slurm_free_resv_desc_msg_part(&resv_msg, res_free_flags);

	return ret;
}
