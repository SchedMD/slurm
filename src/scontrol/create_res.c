/*****************************************************************************\
 *  create_res.c - reservation creation function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bremer <dbremer@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#define _GNU_SOURCE
#include "src/scontrol/scontrol.h"
#include "src/slurmctld/reservation.h"
#include "src/common/proc_args.h"

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

static int _parse_resv_node_cnt(resv_desc_msg_t *resv_msg_ptr, char *val,
				bool from_tres)
{
	char *endptr = NULL, *node_cnt, *tok, *ptrptr = NULL;
	int node_inx = 0;
	node_cnt = xstrdup(val);
	tok = strtok_r(node_cnt, ",", &ptrptr);
	while (tok) {
		xrealloc(resv_msg_ptr->node_cnt,
			 sizeof(uint32_t) * (node_inx + 2));
		resv_msg_ptr->node_cnt[node_inx] =
			strtol(tok, &endptr, 10);
		if ((endptr != NULL) &&
		    ((endptr[0] == 'k') ||
		     (endptr[0] == 'K'))) {
			resv_msg_ptr->node_cnt[node_inx] *= 1024;
		} else if ((endptr != NULL) &&
			   ((endptr[0] == 'm') ||
			    (endptr[0] == 'M'))) {
			resv_msg_ptr->node_cnt[node_inx] *= 1024 * 1024;
		} else if ((endptr == NULL) ||
			   (endptr[0] != '\0') ||
			   (tok[0] == '\0')) {
			exit_code = 1;
			if (from_tres)
				error("Invalid TRES node count %s", val);
			else
				error("Invalid node count %s", val);
			xfree(node_cnt);
			return SLURM_ERROR;
		}
		node_inx++;
		tok = strtok_r(NULL, ",", &ptrptr);
	}

	xfree(node_cnt);
	return SLURM_SUCCESS;
}

static int _parse_resv_core_cnt(resv_desc_msg_t *resv_msg_ptr, char *val,
				bool from_tres)
{

	char *endptr = NULL, *core_cnt, *tok, *ptrptr = NULL;
	char *type;
	int node_inx = 0;

	type = slurm_get_select_type();
	if (strcasestr(type, "cray")) {
		int param;
		param = slurm_get_select_type_param();
		if (! (param & CR_OTHER_CONS_RES)) {
			error("CoreCnt or CPUCnt is only "
			      "supported when "
			      "SelectTypeParameters "
			      "includes OTHER_CONS_RES");
			xfree(type);
			return SLURM_ERROR;
		}
	} else if (strcasestr(type, "cons_res") == NULL) {
		error("CoreCnt or CPUCnt is only "
		      "supported when "
		      "SelectType includes "
		      "select/cons_res");
		xfree(type);
		return SLURM_ERROR;
	}

	xfree(type);
	core_cnt = xstrdup(val);
	tok = strtok_r(core_cnt, ",", &ptrptr);
	while (tok) {
		xrealloc(resv_msg_ptr->core_cnt,
			 sizeof(uint32_t) * (node_inx + 2));
		resv_msg_ptr->core_cnt[node_inx] =
			strtol(tok, &endptr, 10);
		if ((endptr == NULL) ||
		    (endptr[0] != '\0') ||
		    (tok[0] == '\0')) {
			exit_code = 1;
			if (from_tres)
				error("Invalid TRES core count %s", val);
			else
				error("Invalid core count %s", val);
			xfree(core_cnt);
			return SLURM_ERROR;
		}
		node_inx++;
		tok = strtok_r(NULL, ",", &ptrptr);
	}

	xfree(core_cnt);
	return SLURM_SUCCESS;
}

/* -1 = error, 0 = is configured, 1 = isn't configured */
static int _is_configured_tres(char *type)
{

	int i, cc;
	assoc_mgr_info_request_msg_t req;
	assoc_mgr_info_msg_t *msg = NULL;

	memset(&req, 0, sizeof(assoc_mgr_info_request_msg_t));
	cc = slurm_load_assoc_mgr_info(&req, &msg);
	if (cc != SLURM_PROTOCOL_SUCCESS) {
		slurm_perror("slurm_load_assoc_mgr_info error");
		slurm_free_assoc_mgr_info_msg(msg);
		return SLURM_ERROR;
	}

	for (i = 0; i < msg->tres_cnt; ++i) {
		if (!strcasecmp(msg->tres_names[i], type)) {
			slurm_free_assoc_mgr_info_msg(msg);
			return SLURM_SUCCESS;
		}
	}

	error("'%s' is not a configured TRES", type);
	slurm_free_assoc_mgr_info_msg(msg);
	return SLURM_ERROR;

}

static int _parse_resv_tres(char *val, resv_desc_msg_t  *resv_msg_ptr,
			    int *free_tres_license, int *free_tres_bb,
			    int *free_tres_corecnt, int *free_tres_nodecnt)
{
	int i, ret, len;
	char *tres_bb = NULL, *tres_license = NULL,
		*tres_corecnt = NULL, *tres_nodecnt = NULL,
		*token, *type = NULL, *saveptr1 = NULL,
		*value_str = NULL, *name = NULL, *compound = NULL,
		*tmp = NULL;
	bool discard, first;

	*free_tres_license = 0;
	*free_tres_bb = 0;
	*free_tres_corecnt = 0;
	*free_tres_nodecnt = 0;

	token = strtok_r(val, ",", &saveptr1);
	while (token) {

		compound = strtok_r(token, "=", &value_str);

		if (!value_str || !*value_str) {
			error("TRES component '%s' has an invalid value '%s'",
			      type, token);
			goto error;
		}

		if (strchr(compound, '/')) {
			tmp = xstrdup(compound);
			type = strtok_r(tmp, "/", &name);
		} else
			type = compound;

		if (_is_configured_tres(compound) < 0)
			goto error;

		if (!strcasecmp(type, "license")) {
			if (tres_license && tres_license[0] != '\0')
				xstrcatchar(tres_license, ',');
			xstrfmtcat(tres_license, "%s:%s", name, value_str);
			token = strtok_r(NULL, ",", &saveptr1);
			if (tmp)
				xfree(tmp);

		} else if (strcasecmp(type, "bb") == 0) {
			if (tres_bb && tres_bb[0] != '\0')
				xstrcatchar(tres_bb, ',');
			xstrfmtcat(tres_bb, "%s:%s", name, value_str);
			token = strtok_r(NULL, ",", &saveptr1);
			if (tmp)
				xfree(tmp);

		} else if (strcasecmp(type, "cpu") == 0) {
			first = true;
			discard = false;
			do {
				len = strlen(value_str);
				for (i = 0; i < len; i++) {
					if (!isdigit(value_str[i])) {
						if (first) {
							error("TRES value '%s' "
							      "is invalid",
							      value_str);
							goto error;
						} else
							discard = true;
						break;
					}
				}
				first = false;
				if (!discard) {
					if (tres_corecnt && tres_corecnt[0]
					    != '\0')
						xstrcatchar(tres_corecnt, ',');
					xstrcat(tres_corecnt, value_str);

					token = strtok_r(NULL, ",", &saveptr1);
					value_str = token;
				}
			} while (!discard && token);

		} else if (strcasecmp(type, "node") == 0) {
			if (tres_nodecnt && tres_nodecnt[0] != '\0')
				xstrcatchar(tres_nodecnt, ',');
			xstrcat(tres_nodecnt, value_str);
			token = strtok_r(NULL, ",", &saveptr1);
		} else {
			error("TRES type '%s' not supported with reservations",
			      compound);
			goto error;
		}

	}

	if (tres_corecnt && tres_corecnt[0] != '\0') {
		ret = _parse_resv_core_cnt(resv_msg_ptr, tres_corecnt, true);
		xfree(tres_corecnt);
		if (ret != SLURM_SUCCESS)
			goto error;
		*free_tres_corecnt = 1;
	}

	if (tres_nodecnt && tres_nodecnt[0] != '\0') {
		ret = _parse_resv_node_cnt(resv_msg_ptr, tres_nodecnt, true);
		xfree(tres_nodecnt);
		if (ret != SLURM_SUCCESS)
			goto error;
		*free_tres_nodecnt = 1;
	}

	if (tres_license && tres_license[0] != '\0') {
		resv_msg_ptr->licenses = tres_license;
		*free_tres_license = 1;
	}

	if (tres_bb && tres_bb[0] != '\0') {
		resv_msg_ptr->burst_buffer = tres_bb;
		*free_tres_bb = 1;
	}

	return SLURM_SUCCESS;

error:
	xfree(tres_nodecnt);
	xfree(tres_corecnt);
	exit_code = 1;
	return SLURM_ERROR;
}


/*
 * scontrol_parse_res_options   parse options for creating or updating a
 reservation
 * IN argc - count of arguments
 * IN argv - list of arguments
 * IN msg  - a string to append to any error message
 * OUT resv_msg_ptr - struct holding reservation parameters
 * OUT free_user_str - bool indicating that resv_msg_ptr->users should be freed
 * OUT free_acct_str - bool indicating that resv_msg_ptr->accounts should be
 *		       freed
 * RET 0 on success, -1 on err and prints message
 */
extern int
scontrol_parse_res_options(int argc, char *argv[], const char *msg,
			   resv_desc_msg_t  *resv_msg_ptr,
			   int *free_user_str, int *free_acct_str,
			   int *free_tres_license, int *free_tres_bb,
			   int *free_tres_corecnt, int *free_tres_nodecnt)
{
	int i;
	int duration = -3;   /* -1 == INFINITE, -2 == error, -3 == not set */

	*free_user_str = 0;
	*free_acct_str = 0;
	*free_tres_license = 0;
	*free_tres_bb = 0;
	*free_tres_corecnt = 0;
	*free_tres_nodecnt = 0;

	for (i=0; i<argc; i++) {
		char *tag = argv[i];
		int taglen = 0;
		char plus_minus = '\0';

		char *val = strchr(argv[i], '=');
		taglen = val - argv[i];

		if (!val && strncasecmp(argv[i], "res", 3) == 0) {
			continue;
		} else if (!val || taglen == 0) {
			exit_code = 1;
			error("Unknown parameter %s.  %s", argv[i], msg);
			return SLURM_ERROR;
		}
		if (val[-1] == '+' || val[-1] == '-') {
			plus_minus = val[-1];
			taglen--;
		}
		val++;

		if (strncasecmp(tag, "ReservationName", MAX(taglen, 1)) == 0) {
			resv_msg_ptr->name = val;

		} else if (strncasecmp(tag, "Accounts", MAX(taglen, 1)) == 0) {
			if (plus_minus) {
				resv_msg_ptr->accounts =
					_process_plus_minus(plus_minus, val);
				*free_acct_str = 1;
			} else {
				resv_msg_ptr->accounts = val;
			}
		} else if (strncasecmp(tag, "BurstBuffer", MAX(taglen, 2))
			   == 0) {
			resv_msg_ptr->burst_buffer = val;
		} else if (strncasecmp(tag, "StartTime", MAX(taglen, 1)) == 0){
			time_t  t = parse_time(val, 0);
			if (errno == ESLURM_INVALID_TIME_VALUE) {
				exit_code = 1;
				error("Invalid start time %s.  %s",
				      argv[i], msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->start_time = t;

		} else if (strncasecmp(tag, "EndTime", MAX(taglen, 1)) == 0) {
			time_t  t = parse_time(val, 0);
			if (errno == ESLURM_INVALID_TIME_VALUE) {
				exit_code = 1;
				error("Invalid end time %s.  %s", argv[i],msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->end_time = t;

		} else if (strncasecmp(tag, "Duration", MAX(taglen, 1)) == 0) {
			/* -1 == INFINITE, -2 == error, -3 == not set */
			duration = time_str2mins(val);
			if (duration < 0 && duration != INFINITE) {
				exit_code = 1;
				error("Invalid duration %s.  %s", argv[i],msg);
				return SLURM_ERROR;
			}
			resv_msg_ptr->duration = (uint32_t)duration;

		} else if (strncasecmp(tag, "Flags", MAX(taglen, 2)) == 0) {
			uint32_t f;
			if (plus_minus) {
				char *tmp =
					_process_plus_minus(plus_minus, val);
				f = parse_resv_flags(tmp, msg);
				xfree(tmp);
			} else {
				f = parse_resv_flags(val, msg);
			}
			if (f == 0xffffffff) {
				return SLURM_ERROR;
			} else {
				resv_msg_ptr->flags = f;
			}
		} else if (strncasecmp(tag, "NodeCnt", MAX(taglen,5)) == 0 ||
			   strncasecmp(tag, "NodeCount", MAX(taglen,5)) == 0) {

			if (_parse_resv_node_cnt(resv_msg_ptr, val, false)
			    == SLURM_ERROR)
				return SLURM_ERROR;

		} else if (strncasecmp(tag, "CoreCnt",   MAX(taglen,5)) == 0 ||
		           strncasecmp(tag, "CoreCount", MAX(taglen,5)) == 0 ||
		           strncasecmp(tag, "CPUCnt",    MAX(taglen,5)) == 0 ||
			   strncasecmp(tag, "CPUCount",  MAX(taglen,5)) == 0) {

			if (_parse_resv_core_cnt(resv_msg_ptr, val, false)
			    == SLURM_ERROR)
				return SLURM_ERROR;

		} else if (strncasecmp(tag, "Nodes", MAX(taglen, 5)) == 0) {
			resv_msg_ptr->node_list = val;

		} else if (strncasecmp(tag, "Features", MAX(taglen, 2)) == 0) {
			resv_msg_ptr->features = val;

		} else if (strncasecmp(tag, "Licenses", MAX(taglen, 2)) == 0) {
			resv_msg_ptr->licenses = val;

		} else if (strncasecmp(tag, "PartitionName", MAX(taglen, 1))
			   == 0) {
			resv_msg_ptr->partition = val;

		} else if (strncasecmp(tag, "TRES", MAX(taglen, 1)) == 0) {
			if (_parse_resv_tres(val, resv_msg_ptr,
					     free_tres_license, free_tres_bb,
					     free_tres_corecnt,
					     free_tres_nodecnt) == SLURM_ERROR)
				return SLURM_ERROR;

		} else if (strncasecmp(tag, "Users", MAX(taglen, 1)) == 0) {
			if (plus_minus) {
				resv_msg_ptr->users =
					_process_plus_minus(plus_minus, val);
				*free_user_str = 1;
			} else {
				resv_msg_ptr->users = val;
			}
		} else if (strncasecmp(tag, "Watts", MAX(taglen, 1)) == 0) {
			if (parse_uint32(val, &(resv_msg_ptr->resv_watts))) {
				error("Invalid Watts value: %s", val);
				return SLURM_ERROR;
			}
		} else if (strncasecmp(tag, "res", 3) == 0) {
			continue;
		} else {
			exit_code = 1;
			error("Unknown parameter %s.  %s", argv[i], msg);
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
scontrol_update_res(int argc, char *argv[])
{
	resv_desc_msg_t   resv_msg;
	int err, ret = 0;
	int free_user_str = 0, free_acct_str = 0, free_tres_license = 0,
		free_tres_bb = 0, free_tres_corecnt = 0, free_tres_nodecnt = 0;

	slurm_init_resv_desc_msg (&resv_msg);
	err = scontrol_parse_res_options(argc, argv, "No reservation update.",
					 &resv_msg, &free_user_str,
					 &free_acct_str, &free_tres_license,
					 &free_tres_bb, &free_tres_corecnt,
					 &free_tres_nodecnt);
	if (err)
		goto SCONTROL_UPDATE_RES_CLEANUP;

	if (resv_msg.name == NULL) {
		exit_code = 1;
		error("Reservation must be given.  No reservation update.");
		goto SCONTROL_UPDATE_RES_CLEANUP;
	}

	err = slurm_update_reservation(&resv_msg);
	if (err) {
		exit_code = 1;
		slurm_perror("Error updating the reservation");
		ret = slurm_get_errno();
	} else {
		printf("Reservation updated.\n");
	}

SCONTROL_UPDATE_RES_CLEANUP:
	if (free_user_str)
		xfree(resv_msg.users);
	if (free_acct_str)
		xfree(resv_msg.accounts);
	if (free_tres_license)
		xfree(resv_msg.licenses);
	if (free_tres_bb)
		xfree(resv_msg.burst_buffer);
	if (free_tres_corecnt)
		xfree(resv_msg.core_cnt);
	if (free_tres_nodecnt)
		xfree(resv_msg.node_cnt);
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
scontrol_create_res(int argc, char *argv[])
{
	resv_desc_msg_t resv_msg;
	char *new_res_name = NULL;
	int free_user_str = 0, free_acct_str = 0, free_tres_license = 0,
		free_tres_bb = 0, free_tres_corecnt = 0, free_tres_nodecnt = 0;
	int err, ret = 0;

	slurm_init_resv_desc_msg (&resv_msg);
	err = scontrol_parse_res_options(argc, argv, "No reservation created.",
					 &resv_msg, &free_user_str,
					 &free_acct_str, &free_tres_license,
					 &free_tres_bb, &free_tres_corecnt,
					 &free_tres_nodecnt);

	if (err)
		goto SCONTROL_CREATE_RES_CLEANUP;

	if (resv_msg.start_time == (time_t)NO_VAL) {
		exit_code = 1;
		error("A start time must be given.  No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.end_time == (time_t)NO_VAL &&
	    resv_msg.duration == (uint32_t)NO_VAL) {
		exit_code = 1;
		error("An end time or duration must be given.  "
		      "No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.end_time != (time_t)NO_VAL &&
	    resv_msg.duration != (uint32_t)NO_VAL &&
            resv_msg.start_time + resv_msg.duration*60 != resv_msg.end_time) {
		exit_code = 1;
		error("StartTime + Duration does not equal EndTime.  "
		      "No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.start_time > resv_msg.end_time &&
	    resv_msg.end_time != (time_t)NO_VAL) {
		exit_code = 1;
		error("Start time cannot be after end time.  "
		      "No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}

	/*
	 * If "ALL" is specified for the nodes and a partition is specified,
	 * only allocate all of the nodes the partition.
	 */
	if ((resv_msg.partition != NULL) && (resv_msg.node_list != NULL) &&
	    (strcasecmp(resv_msg.node_list, "ALL") == 0)) {
		if (resv_msg.flags == NO_VAL)
			resv_msg.flags = RESERVE_FLAG_PART_NODES;
		else
			resv_msg.flags |= RESERVE_FLAG_PART_NODES;
	}

	/*
	 * If "ALL" is specified for the nodes and RESERVE_FLAG_PART_NODES
	 * flag is set make sure a partition name is specified.
	 */
	if ((resv_msg.partition == NULL) && (resv_msg.node_list != NULL) &&
	    (strcasecmp(resv_msg.node_list, "ALL") == 0) &&
	    (resv_msg.flags != NO_VAL) &&
	    (resv_msg.flags & RESERVE_FLAG_PART_NODES)) {
		exit_code = 1;
		error("Part_Nodes flag requires specifying a Partition. "
		      "No reservation created.");
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
			error("CoreCnt, Nodes, NodeCnt, BurstBuffer, Licenses"
			      "or Watts must be specified. No reservation "
			      "created.");
			goto SCONTROL_CREATE_RES_CLEANUP;
		}
		if (resv_msg.flags == (uint16_t) NO_VAL)
			resv_msg.flags = RESERVE_FLAG_PART_NODES;
		else
			resv_msg.flags |= RESERVE_FLAG_PART_NODES;
		resv_msg.node_list = "ALL";
	}
	if ((resv_msg.users == NULL    || resv_msg.users[0] == '\0') &&
	    (resv_msg.accounts == NULL || resv_msg.accounts[0] == '\0')) {
		exit_code = 1;
		error("Either Users or Accounts must be specified.  "
		      "No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	if (resv_msg.resv_watts != NO_VAL &&
	    (!(resv_msg.flags & RESERVE_FLAG_ANY_NODES) ||
	     (resv_msg.core_cnt != 0) ||
	     (resv_msg.node_cnt  != NULL && resv_msg.node_cnt[0]  != 0) ||
	     (resv_msg.node_list != NULL && resv_msg.node_list[0] != '\0') ||
	     (resv_msg.licenses  != NULL && resv_msg.licenses[0]  != '\0'))) {
		exit_code = 1;
		error("A power reservation must be empty and set the "
		      "LICENSE_ONLY flag. No reservation created.");
		goto SCONTROL_CREATE_RES_CLEANUP;
	}
	new_res_name = slurm_create_reservation(&resv_msg);
	if (!new_res_name) {
		exit_code = 1;
		slurm_perror("Error creating the reservation");
		ret = slurm_get_errno();
	} else {
		printf("Reservation created: %s\n", new_res_name);
		free(new_res_name);
	}

SCONTROL_CREATE_RES_CLEANUP:
	if (free_user_str)
		xfree(resv_msg.users);
	if (free_acct_str)
		xfree(resv_msg.accounts);
	if (free_tres_license)
		xfree(resv_msg.licenses);
	if (free_tres_bb)
		xfree(resv_msg.burst_buffer);
	if (free_tres_corecnt)
		xfree(resv_msg.core_cnt);
	if (free_tres_nodecnt)
		xfree(resv_msg.node_cnt);

	return ret;
}
