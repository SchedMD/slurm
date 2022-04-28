/*****************************************************************************\
 *  qos_functions.c - functions dealing with qoss in the
 *                        accounting system.
 *****************************************************************************
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Copyright (C) 2002-2008 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/assoc_mgr.h"

static int _parse_preempt_modes_internal(List null, char *name, void *x)
{
	uint16_t *preempt_mode = x;
	uint16_t ret_mode = 0;

	ret_mode = preempt_mode_num(name);
	if (ret_mode == NO_VAL16) {
		error("Unknown preempt_mode given '%s'", name);
		*preempt_mode = NO_VAL16;
		return SLURM_ERROR;
	}

	/*
	 * Since we can't really add 0 to a bitstring
	 * put on one that we can track.
	 */
	if (ret_mode == PREEMPT_MODE_OFF)
		ret_mode = PREEMPT_MODE_COND_OFF;

	*preempt_mode |= ret_mode;
	return 1;
}

static uint16_t _parse_preempt_modes(char *names)
{
	uint16_t preempt_mode = 0;
	slurm_parse_char_list(NULL, names, &preempt_mode,
			      _parse_preempt_modes_internal);
 	return preempt_mode;
}

static int _set_cond(int *start, int argc, char **argv,
		     slurmdb_qos_cond_t *qos_cond,
		     List format_list)
{
	int i;
	int set = 0;
	int end = 0;
	int command_len = 0;

	if (!qos_cond) {
		error("No qos_cond given");
		return -1;
	}

	for (i = (*start); i < argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len = strlen(argv[i]);
		else {
			command_len = end - 1;
			if (argv[i][end] == '=') {
				end++;
			}
		}

		if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "WithDeleted",
						 MAX(command_len, 5))) {
			qos_cond->with_deleted = 1;
		} else if (!end && !xstrncasecmp(argv[i], "where",
						 MAX(command_len, 5))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Names",
					   MAX(command_len, 1))
			  || !xstrncasecmp(argv[i], "QOSLevel",
					   MAX(command_len, 1))) {
			if (!qos_cond->name_list) {
				qos_cond->name_list = list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(qos_cond->name_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Clusters",
					 MAX(command_len, 1))) {
			/*
			 * This is only used to remove usage, overload
			 * the description.
			 */
			if (!qos_cond->description_list) {
				qos_cond->description_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(qos_cond->description_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Descriptions",
					 MAX(command_len, 1))) {
			if (!qos_cond->description_list) {
				qos_cond->description_list =
					list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(qos_cond->description_list,
						 argv[i]+end))
				set = 1;
		} else if (!xstrncasecmp(argv[i], "Format",
					 MAX(command_len, 1))) {
			if (format_list)
				slurm_addto_char_list(format_list, argv[i]+end);
		} else if (!xstrncasecmp(argv[i], "Ids", MAX(command_len, 1))) {
			ListIterator itr = NULL;
			char *temp = NULL;
			uint32_t id = 0;

			if (!qos_cond->id_list) {
				qos_cond->id_list = list_create(xfree_ptr);
			}
			if (slurm_addto_char_list(qos_cond->id_list,
						 argv[i]+end))
				set = 1;

			/* check to make sure user gave ints here */
			itr = list_iterator_create(qos_cond->id_list);
			while ((temp = list_next(itr))) {
				if (get_uint(temp, &id, "QOS ID")
				    != SLURM_SUCCESS) {
					exit_code = 1;
					list_delete_item(itr);
				}
			}
			list_iterator_destroy(itr);
		} else if (!xstrncasecmp(argv[i], "PreemptMode",
					 MAX(command_len, 8))) {
			qos_cond->preempt_mode |=
				_parse_preempt_modes(argv[i] + end);
			if (qos_cond->preempt_mode == NO_VAL16) {
				fprintf(stderr,
					" Bad Preempt Mode given: %s\n",
					argv[i]);
				exit_code = 1;
			} else
				set = 1;
		} else {
			exit_code = 1;
			fprintf(stderr, " Unknown condition: %s\n"
				" Use keyword 'set' to modify "
				"SLURM_PRINT_VALUE\n", argv[i]);
		}
	}

	(*start) = i;
	return set;
}

static int _set_rec(int *start, int argc, char **argv,
		    List name_list,
		    slurmdb_qos_rec_t *qos)
{
	int i, mins;
	int set = 0;
	int end = 0;
	int command_len = 0;
	int option = 0;
	uint64_t tmp64;
	char *tmp_char = NULL;
	uint32_t tres_flags = TRES_STR_FLAG_SORT_ID | TRES_STR_FLAG_REPLACE;

	for (i=(*start); i<argc; i++) {
		end = parse_option_end(argv[i]);
		if (!end)
			command_len=strlen(argv[i]);
		else {
			command_len=end-1;
			if (argv[i][end] == '=') {
				option = (int)argv[i][end-1];
				end++;
			}
		}

		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i--;
			break;
		} else if (!end && !xstrncasecmp(argv[i], "set",
						 MAX(command_len, 3))) {
			continue;
		} else if (!end
			  || !xstrncasecmp(argv[i], "Name",
					   MAX(command_len, 1))) {
			if (name_list)
				slurm_addto_char_list(name_list, argv[i]+end);
		} else if (!qos)
			continue;
		else if (!xstrncasecmp(argv[i], "Description",
				       MAX(command_len, 1))) {
			if (!qos->description)
				qos->description =
					strip_quotes(argv[i]+end, NULL, 1);
			set = 1;
		} else if (!xstrncasecmp(argv[i], "Flags",
					 MAX(command_len, 2))) {
			qos->flags = str_2_qos_flags(argv[i]+end, option);
			if (qos->flags == QOS_FLAG_NOTSET) {
				char *tmp_char = NULL;
				qos->flags = INFINITE;
				qos->flags &= (~QOS_FLAG_NOTSET &
					       ~QOS_FLAG_ADD &
					       ~QOS_FLAG_REMOVE);
				tmp_char = slurmdb_qos_flags_str(qos->flags);
				printf(" Unknown QOS flag used in:\n  '%s'\n"
				       " Valid QOS flags are\n  '%s'\n",
				       argv[i]+end, tmp_char);
				xfree(tmp_char);
				exit_code = 1;
			} else
				set = 1;
		} else if (!xstrncasecmp(argv[i], "GraceTime",
					 MAX(command_len, 3))) {
			if (get_uint(argv[i]+end, &qos->grace_time,
			             "GraceTime") == SLURM_SUCCESS) {
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "GrpCPUMins",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end,
				       &tmp64,
				       "GrpCPUMins") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->grp_tres_mins, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "GrpCPURunMins",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "GrpCPURunMins") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->grp_tres_run_mins, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "GrpCPUs",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "GrpCPUs") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->grp_tres, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "GrpJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &qos->grp_jobs,
			    "GrpJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "GrpJobsAccrue",
					 MAX(command_len, 8))) {
			if (get_uint(argv[i]+end, &qos->grp_jobs_accrue,
			    "GrpJobsAccrue") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "GrpMemory",
					 MAX(command_len, 4))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "GrpMemory") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_MEM, tmp64);
				slurmdb_combine_tres_strings(
					&qos->grp_tres, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "GrpNodes",
					 MAX(command_len, 4))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "GrpNodes") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_NODE, tmp64);
				slurmdb_combine_tres_strings(
					&qos->grp_tres, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "GrpSubmitJobs",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &qos->grp_submit_jobs,
			    "GrpSubmitJobs") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "GrpTRES",
					 MAX(command_len, 7))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->grp_tres, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "GrpTRESMins",
					 MAX(command_len, 8))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->grp_tres_mins, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "GrpTRESRunMins",
					 MAX(command_len, 8))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->grp_tres_run_mins, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "GrpWall",
					 MAX(command_len, 4))) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				qos->grp_wall	= (uint32_t) mins;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad GrpWall time format: %s\n",
					argv[i]);
			}
		} else if (!xstrncasecmp(argv[i], "LimitFactor",
					 MAX(command_len, 6))) {
			if (get_double(argv[i]+end, &qos->limit_factor,
			    "LimitFactor") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxCPUMinsPerJob",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end,
				       &tmp64,
				       "MaxCPUMins") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->max_tres_mins_pj, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MaxCPUsPerJob",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "MaxCPUs") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->max_tres_pj, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MaxCPUsPerUser",
					 MAX(command_len, 11)) ||
			   !xstrncasecmp(argv[i], "MaxCPUsPU",
					 MAX(command_len, 9))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "MaxCPUsPerUser") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->max_tres_pu, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MaxJobsAccruePerAccount",
					 MAX(command_len, 17)) ||
			   !xstrncasecmp(argv[i], "MaxJobsAccruePA",
					 MAX(command_len, 15))) {
			if (get_uint(argv[i]+end, &qos->max_jobs_accrue_pa,
			    "MaxJobsAccruePA") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxJobsAccruePerUser",
					 MAX(command_len, 17)) ||
			   !xstrncasecmp(argv[i], "MaxJobsAccruePU",
					 MAX(command_len, 15))) {
			if (get_uint(argv[i]+end, &qos->max_jobs_accrue_pu,
			    "MaxJobsAccruePU") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxJobsPerAccount",
					 MAX(command_len, 11)) ||
			   !xstrncasecmp(argv[i], "MaxJobsPA",
					 MAX(command_len, 9))) {
			if (get_uint(argv[i]+end, &qos->max_jobs_pa,
			    "MaxJobsPA") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxJobsPerUser",
					 MAX(command_len, 4)) ||
			   !xstrncasecmp(argv[i], "MaxJobsPU",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &qos->max_jobs_pu,
			    "MaxJobsPU") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxNodesPerJob",
					 MAX(command_len, 4))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "MaxNodesPerJob") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_NODE, tmp64);
				slurmdb_combine_tres_strings(
					&qos->max_tres_pj, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MaxNodesPerUser",
					 MAX(command_len, 8)) ||
			   !xstrncasecmp(argv[i], "MaxNodesPU",
					 MAX(command_len, 8))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "MaxNodesPerUser") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_NODE, tmp64);
				slurmdb_combine_tres_strings(
					&qos->max_tres_pu, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MaxSubmitJobsPerAccount",
					 MAX(command_len, 17)) ||
			   !xstrncasecmp(argv[i], "MaxSubmitJobsPA",
					 MAX(command_len, 15))) {
			if (get_uint(argv[i]+end, &qos->max_submit_jobs_pa,
			    "MaxSubmitJobsPA") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxSubmitJobsPerUser",
					 MAX(command_len, 4)) ||
			   !xstrncasecmp(argv[i], "MaxSubmitJobsPU",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &qos->max_submit_jobs_pu,
			    "MaxSubmitJobsPU") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESPerAccount",
					 MAX(command_len, 11)) ||
			   !xstrncasecmp(argv[i], "MaxTRESPA",
					 MAX(command_len, 9))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_pa, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESPerJob",
					 MAX(command_len, 7))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_pj, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESPerNode",
					 MAX(command_len, 11))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_pn, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESPerUser",
					 MAX(command_len, 11)) ||
			   !xstrncasecmp(argv[i], "MaxTRESPU",
					 MAX(command_len, 9))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_pu, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESMinsPerJob",
					 MAX(command_len, 8))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_mins_pj, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESRunMinsPA",
					 MAX(command_len, 16))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_run_mins_pa, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxTRESRunMinsPU",
					 MAX(command_len, 8))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->max_tres_run_mins_pu, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "MaxWallDurationPerJob",
					 MAX(command_len, 4))) {
			mins = time_str2mins(argv[i]+end);
			if (mins != NO_VAL) {
				qos->max_wall_pj = (uint32_t) mins;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad MaxWall time format: %s\n",
					argv[i]);
			}
		} else if (!xstrncasecmp(argv[i], "MinCPUsPerJob",
					 MAX(command_len, 7))) {
			if (get_uint64(argv[i]+end, &tmp64,
				       "MinCPUs") == SLURM_SUCCESS) {
				set = 1;
				tmp_char = xstrdup_printf(
					"%d=%"PRIu64, TRES_CPU, tmp64);
				slurmdb_combine_tres_strings(
					&qos->min_tres_pj, tmp_char,
					tres_flags);
				xfree(tmp_char);
			}
		} else if (!xstrncasecmp(argv[i], "MinPrioThresh",
					 MAX(command_len, 4))) {
			if (get_uint(argv[i]+end, &qos->min_prio_thresh,
				     "MinPrioThresh") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "MinTRESPerJob",
					 MAX(command_len, 7))) {
			sacctmgr_initialize_g_tres_list();

			if ((tmp_char = slurmdb_format_tres_str(
				     argv[i]+end, g_tres_list, 1))) {
				slurmdb_combine_tres_strings(
					&qos->min_tres_pj, tmp_char,
					tres_flags);
				set = 1;
				xfree(tmp_char);
			} else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "PreemptMode",
					 MAX(command_len, 8))) {
			qos->preempt_mode = preempt_mode_num(argv[i]+end);
			if (qos->preempt_mode == NO_VAL16) {
				fprintf(stderr,
					" Bad Preempt Mode given: %s\n",
					argv[i]);
				exit_code = 1;
			} else
				set = 1;
		/* Preempt needs to follow PreemptMode */
		} else if (!xstrncasecmp(argv[i], "Preempt",
					 MAX(command_len, 7))) {
			if (!qos->preempt_list)
				qos->preempt_list = list_create(xfree_ptr);

			if (!g_qos_list)
				g_qos_list = slurmdb_qos_get(
					db_conn, NULL);

			if (slurmdb_addto_qos_char_list(qos->preempt_list,
						       g_qos_list, argv[i]+end,
						       option) > 0)
				set = 1;
			else
				exit_code = 1;
		} else if (!xstrncasecmp(argv[i], "PreemptExemptTime",
					 MAX(command_len, 8))) {
			int seconds = time_str2secs(argv[i]+end);
			if (seconds != NO_VAL) {
				qos->preempt_exempt_time = seconds;
				set = 1;
			} else {
				exit_code=1;
				fprintf(stderr,
					" Bad PreemptExemptTime format: %s\n",
					argv[i]);
			}
		} else if (!xstrncasecmp(argv[i], "Priority",
					 MAX(command_len, 3))) {
			if (get_uint(argv[i]+end, &qos->priority,
			    "Priority") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "RawUsage",
					 MAX(command_len, 7))) {
			uint32_t usage;
			qos->usage = xmalloc(sizeof(slurmdb_qos_usage_t));
			if (get_uint(argv[i]+end, &usage,
				     "RawUsage") == SLURM_SUCCESS) {
				qos->usage->usage_raw = usage;
				set = 1;
			}
		} else if (!xstrncasecmp(argv[i], "UsageFactor",
					 MAX(command_len, 6))) {
			if (get_double(argv[i]+end, &qos->usage_factor,
			    "UsageFactor") == SLURM_SUCCESS)
				set = 1;
		} else if (!xstrncasecmp(argv[i], "UsageThreshold",
					 MAX(command_len, 6))) {
			if (get_double(argv[i]+end, &qos->usage_thres,
			    "UsageThreshold") == SLURM_SUCCESS)
				set = 1;
		} else {
			exit_code = 1;
			printf(" Unknown option: %s\n"
			       " Use keyword 'where' to modify condition\n",
			       argv[i]);
		}
	}

	(*start) = i;

	return set;
}

static bool _isdefault(List qos_list)
{
	int rc = 0;
	slurmdb_assoc_cond_t assoc_cond;
	slurmdb_assoc_rec_t *assoc = NULL;
	ListIterator itr;
	List ret_list = NULL;
	char *name = NULL;

	if (!qos_list || !list_count(qos_list))
		return rc;

	/* this needs to happen before any removing takes place so we
	   can figure out things correctly */
	xassert(g_qos_list);

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.without_parent_info = 1;
	assoc_cond.def_qos_id_list = list_create(xfree_ptr);

	itr = list_iterator_create(qos_list);
	while ((name = list_next(itr))) {
		uint32_t id = str_2_slurmdb_qos(g_qos_list, name);
		if (id == NO_VAL)
			continue;
		list_append(assoc_cond.def_qos_id_list,
			    xstrdup_printf("%u", id));
	}
	list_iterator_destroy(itr);

	ret_list = slurmdb_associations_get(
		db_conn, &assoc_cond);
	FREE_NULL_LIST(assoc_cond.def_qos_id_list);

	if (!ret_list || !list_count(ret_list))
		goto end_it;

	fprintf(stderr," Associations listed below have these "
		"as their Default QOS.\n");
	itr = list_iterator_create(ret_list);
	while((assoc = list_next(itr))) {
		name = slurmdb_qos_str(g_qos_list, assoc->def_qos_id);
		if (!assoc->user) {
			// see if this isn't a user
			fprintf(stderr,
				"  DefQOS = %-10s C = %-10s A = %-20s\n",
				name, assoc->cluster, assoc->acct);
		} else if (assoc->partition) {
			// see if there is a partition name
			fprintf(stderr,
				"  DefQOS = %-10s C = %-10s A = %-20s "
				"U = %-9s P = %s\n",
				name, assoc->cluster, assoc->acct,
				assoc->user, assoc->partition);
		} else {
			fprintf(stderr,
				"  DefQOS = %-10s C = %-10s A = %-20s "
				"U = %-9s\n",
				name, assoc->cluster, assoc->acct, assoc->user);
		}
	}
	list_iterator_destroy(itr);
	rc = 1;
end_it:
	FREE_NULL_LIST(ret_list);

	return rc;
}


extern int sacctmgr_add_qos(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	int i;
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	slurmdb_qos_rec_t *start_qos = xmalloc(sizeof(slurmdb_qos_rec_t));
	List name_list = list_create(xfree_ptr);
	char *description = NULL;
	char *name = NULL;
	List qos_list = NULL;
	char *qos_str = NULL;

	slurmdb_init_qos_rec(start_qos, 0, NO_VAL);

	for (i = 0; i < argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;

		_set_rec(&i, argc, argv, name_list, start_qos);
	}

	if (exit_code) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_qos_rec(start_qos);
		return SLURM_ERROR;
	} else if (!list_count(name_list)) {
		FREE_NULL_LIST(name_list);
		slurmdb_destroy_qos_rec(start_qos);
		exit_code = 1;
		fprintf(stderr, " Need name of qos to add.\n");
		return SLURM_SUCCESS;
	}

	if (!g_qos_list) {
		g_qos_list = slurmdb_qos_get(db_conn, NULL);

		if (!g_qos_list) {
			exit_code = 1;
			fprintf(stderr, " Problem getting qos's "
				"from database.  "
				"Contact your admin.\n");
			FREE_NULL_LIST(name_list);
			slurmdb_destroy_qos_rec(start_qos);
			return SLURM_ERROR;
		}
	}

	qos_list = list_create(slurmdb_destroy_qos_rec);

	itr = list_iterator_create(name_list);
	while ((name = list_next(itr))) {
		qos = NULL;
		if (!sacctmgr_find_qos_from_list(g_qos_list, name)) {
			qos = xmalloc(sizeof(slurmdb_qos_rec_t));
			slurmdb_init_qos_rec(qos, 0, NO_VAL);
			qos->name = xstrdup(name);
			if (start_qos->description) {
				qos->description =
					xstrdup(start_qos->description);
			} else
				qos->description = xstrdup(name);
			description = qos->description;	/* No copy */
			slurmdb_copy_qos_rec_limits(qos, start_qos);

			xstrfmtcat(qos_str, "  %s\n", name);
			list_append(qos_list, qos);
		}
	}
	list_iterator_destroy(itr);
	FREE_NULL_LIST(name_list);

	FREE_NULL_LIST(g_qos_list);

	if (!list_count(qos_list)) {
		printf(" Nothing new added.\n");
		rc = SLURM_ERROR;
		goto end_it;
	}

	if (qos_str) {
		printf(" Adding QOS(s)\n%s", qos_str);
		printf(" Settings\n");
		if (description)
			printf("  Description    = %s\n", description);
		else
			printf("  Description    = %s\n", "QOS Name");

		sacctmgr_print_qos_limits(start_qos);

		xfree(qos_str);
	}

	notice_thread_init();
	if (list_count(qos_list))
		rc = slurmdb_qos_add(db_conn, qos_list);
	else
		goto end_it;

	notice_thread_fini();

	if (rc == SLURM_SUCCESS) {
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else {
		exit_code = 1;
		fprintf(stderr, " Problem adding QOS: %s\n",
			slurm_strerror(rc));
		rc = SLURM_ERROR;
	}

end_it:
	FREE_NULL_LIST(qos_list);
	slurmdb_destroy_qos_rec(start_qos);

	return rc;
}

extern int sacctmgr_list_qos(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_qos_cond_t *qos_cond = xmalloc(sizeof(slurmdb_qos_cond_t));
 	int i=0;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	List qos_list = NULL;
	int field_count = 0;

	print_field_t *field = NULL;

	List format_list = list_create(xfree_ptr);
	List print_fields_list; /* types are of print_field_t */

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		_set_cond(&i, argc, argv, qos_cond, format_list);
	}

	if (exit_code) {
		slurmdb_destroy_qos_cond(qos_cond);
		FREE_NULL_LIST(format_list);
		return SLURM_ERROR;
	} else if (!list_count(format_list)) {
		slurm_addto_char_list(format_list,
				      "Name,Prio,GraceT,"
				      "Preempt,PreemptE,PreemptM,"
				      "Flags%40,UsageThres,UsageFactor,"
				      "GrpTRES,GrpTRESMins,GrpTRESRunMins,"
				      "GrpJ,GrpS,GrpW,"
				      "MaxTRES,MaxTRESPerN,MaxTRESMins,MaxW,"
				      "MaxTRESPerUser,"
				      "MaxJobsPerUser,"
				      "MaxSubmitJobsPerUser,"
				      "MaxTRESPerAcct,"
				      "MaxJobsPerAcct,"
				      "MaxSubmitJobsPerAcct,MinTRES");
	}

	print_fields_list = sacctmgr_process_format_list(format_list);
	FREE_NULL_LIST(format_list);

	if (exit_code) {
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}
	qos_list = slurmdb_qos_get(db_conn, qos_cond);
	slurmdb_destroy_qos_cond(qos_cond);

	if (!qos_list) {
		exit_code=1;
		fprintf(stderr, " Problem with query.\n");
		FREE_NULL_LIST(print_fields_list);
		return SLURM_ERROR;
	}
	itr = list_iterator_create(qos_list);
	itr2 = list_iterator_create(print_fields_list);
	print_fields_header(print_fields_list);

	field_count = list_count(print_fields_list);

	while ((qos = list_next(itr))) {
		int curr_inx = 1;
		while ((field = list_next(itr2))) {
			switch(field->type) {
			case PRINT_DESC:
				field->print_routine(
					field, qos->description,
					(curr_inx == field_count));
				break;
			case PRINT_FLAGS:
			{
				char *tmp_char = slurmdb_qos_flags_str(
					qos->flags);
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
			case PRINT_UT:
				field->print_routine(
					field, qos->usage_thres,
					(curr_inx == field_count));
				break;
			case PRINT_GRACE:
				field->print_routine(
					field, (uint64_t)qos->grace_time,
					(curr_inx == field_count));
				break;
			case PRINT_GRPCM:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->grp_tres_mins, TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_GRPCRM:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->grp_tres_run_mins,
						TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_GRPC:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->grp_tres, TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_GRPTM:
				field->print_routine(
					field, qos->grp_tres_mins,
					(curr_inx == field_count));
				break;
			case PRINT_GRPTRM:
				field->print_routine(
					field, qos->grp_tres_run_mins,
					(curr_inx == field_count));
				break;
			case PRINT_GRPT:
				field->print_routine(
					field, qos->grp_tres,
					(curr_inx == field_count));
				break;
			case PRINT_GRPJ:
				field->print_routine(field,
						     qos->grp_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPJA:
				field->print_routine(field,
						     qos->grp_jobs_accrue,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPMEM:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->grp_tres, TRES_MEM),
					(curr_inx == field_count));
				break;
			case PRINT_GRPN:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->grp_tres, TRES_NODE),
					(curr_inx == field_count));
				break;
			case PRINT_GRPS:
				field->print_routine(field,
						     qos->grp_submit_jobs,
						     (curr_inx == field_count));
				break;
			case PRINT_GRPW:
				field->print_routine(
					field,
					qos->grp_wall,
					(curr_inx == field_count));
				break;
			case PRINT_ID:
				field->print_routine(
					field, qos->id,
					(curr_inx == field_count));
				break;
			case PRINT_MAXCM:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_mins_pj,
						TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_MAXCRM:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_run_mins_pu,
						TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_MAXC:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_pj, TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_MAXCU:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_pu, TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_MINPT:
				field->print_routine(
					field, qos->min_prio_thresh,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTM:
				field->print_routine(
					field, qos->max_tres_mins_pj,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTRM:
				field->print_routine(
					field, qos->max_tres_run_mins_pu,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTRMA:
				field->print_routine(
					field, qos->max_tres_run_mins_pa,
					(curr_inx == field_count));
				break;
			case PRINT_MAXT:
				field->print_routine(
					field, qos->max_tres_pj,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTA:
				field->print_routine(
					field, qos->max_tres_pa,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTN:
				field->print_routine(
					field, qos->max_tres_pn,
					(curr_inx == field_count));
				break;
			case PRINT_MAXTU:
				field->print_routine(
					field, qos->max_tres_pu,
					(curr_inx == field_count));
				break;
			case PRINT_MAXJ:
				field->print_routine(field,
						     qos->max_jobs_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJPA:
				field->print_routine(field,
						     qos->max_jobs_pa,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJAA:
				field->print_routine(field,
						     qos->max_jobs_accrue_pa,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXJAU:
				field->print_routine(field,
						     qos->max_jobs_accrue_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXN:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_pj, TRES_NODE),
					(curr_inx == field_count));
				break;
			case PRINT_MAXNU:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->max_tres_pu, TRES_NODE),
					(curr_inx == field_count));
				break;
			case PRINT_MAXS:
				field->print_routine(field,
						     qos->max_submit_jobs_pu,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXSA:
				field->print_routine(field,
						     qos->max_submit_jobs_pa,
						     (curr_inx == field_count));
				break;
			case PRINT_MAXW:
				field->print_routine(
					field,
					qos->max_wall_pj,
					(curr_inx == field_count));
				break;
			case PRINT_MINC:
				field->print_routine(
					field,
					slurmdb_find_tres_count_in_string(
						qos->min_tres_pj, TRES_CPU),
					(curr_inx == field_count));
				break;
			case PRINT_MINT:
				field->print_routine(
					field, qos->min_tres_pj,
					(curr_inx == field_count));
				break;
			case PRINT_NAME:
				field->print_routine(
					field, qos->name,
					(curr_inx == field_count));
				break;
			case PRINT_PREE:
				if (!g_qos_list)
					g_qos_list = slurmdb_qos_get(
						db_conn, NULL);

				field->print_routine(
					field, g_qos_list, qos->preempt_bitstr,
					(curr_inx == field_count));
				break;
			case PRINT_PREEM:
			{
				char *tmp_char;
				if (qos->preempt_mode) {
					tmp_char = xstrdup(
						preempt_mode_string(
							qos->preempt_mode));
					xstrtolower(tmp_char);
				} else {
					tmp_char = xstrdup("cluster");
				}
				field->print_routine(
					field,
					tmp_char,
					(curr_inx == field_count));
				xfree(tmp_char);
				break;
			}
			case PRINT_PRXMPT:
			{
				uint64_t tmp64;
				tmp64 = (uint64_t) qos->preempt_exempt_time;
				tmp64 = (tmp64 == INFINITE) ? INFINITE64 : tmp64;
				field->print_routine(field, tmp64,
						     (curr_inx == field_count));
				break;
			}
			case PRINT_PRIO:
				field->print_routine(
					field, qos->priority,
					(curr_inx == field_count));
				break;
			case PRINT_UF:
				field->print_routine(
					field, qos->usage_factor,
					(curr_inx == field_count));
				break;
			case PRINT_LF:
				field->print_routine(
					field, qos->limit_factor,
					(curr_inx == field_count));
				break;
			default:
				field->print_routine(
					field, NULL,
					(curr_inx == field_count));
				break;
			}
			curr_inx++;
		}
		list_iterator_reset(itr2);
		printf("\n");
	}
	list_iterator_destroy(itr2);
	list_iterator_destroy(itr);
	FREE_NULL_LIST(qos_list);
	FREE_NULL_LIST(print_fields_list);

	return rc;
}

extern int sacctmgr_modify_qos(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_qos_cond_t *qos_cond = xmalloc(sizeof(slurmdb_qos_cond_t));
	slurmdb_qos_rec_t *qos = xmalloc(sizeof(slurmdb_qos_rec_t));
	int i=0;
	int cond_set = 0, rec_set = 0, set = 0;
	List ret_list = NULL;

	slurmdb_init_qos_rec(qos, 0, NO_VAL);

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))) {
			i++;
			cond_set += _set_cond(&i, argc, argv, qos_cond, NULL);

		} else if (!xstrncasecmp(argv[i], "Set", MAX(command_len, 3))) {
			i++;
			rec_set += _set_rec(&i, argc, argv, NULL, qos);
		} else {
			cond_set += _set_cond(&i, argc, argv, qos_cond, NULL);
		}
	}

	if (exit_code) {
		slurmdb_destroy_qos_cond(qos_cond);
		slurmdb_destroy_qos_rec(qos);
		return SLURM_ERROR;
	} else if (!rec_set) {
		exit_code=1;
		fprintf(stderr, " You didn't give me anything to set\n");
		slurmdb_destroy_qos_cond(qos_cond);
		slurmdb_destroy_qos_rec(qos);
		return SLURM_ERROR;
	} else if (!cond_set) {
		if (!commit_check("You didn't set any conditions with 'WHERE'.\n"
				 "Are you sure you want to continue?")) {
			printf("Aborted\n");
			slurmdb_destroy_qos_cond(qos_cond);
			slurmdb_destroy_qos_rec(qos);
			return SLURM_SUCCESS;
		}
	}

	// Special case:  reset raw usage only
	if (qos->usage) {
		rc = SLURM_ERROR;
		if (qos->usage->usage_raw == 0.0)
			rc = sacctmgr_remove_qos_usage(qos_cond);
		else
			error("Raw usage can only be set to 0 (zero)");

		slurmdb_destroy_qos_cond(qos_cond);
		slurmdb_destroy_qos_rec(qos);
		return rc;
	}

	notice_thread_init();

	ret_list = slurmdb_qos_modify(db_conn, qos_cond, qos);
	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = list_iterator_create(ret_list);
		printf(" Modified qos...\n");
		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		set = 1;
	} else if (ret_list) {
		printf(" Nothing modified\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

	FREE_NULL_LIST(ret_list);

	notice_thread_fini();

	if (set) {
		if (commit_check("Would you like to commit changes?"))
			slurmdb_connection_commit(db_conn, 1);
		else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	}

	slurmdb_destroy_qos_cond(qos_cond);
	slurmdb_destroy_qos_rec(qos);

	return rc;
}

extern int sacctmgr_delete_qos(int argc, char **argv)
{
	int rc = SLURM_SUCCESS;
	slurmdb_qos_cond_t *qos_cond =
		xmalloc(sizeof(slurmdb_qos_cond_t));
	int i=0;
	List ret_list = NULL;
	int set = 0;

	for (i=0; i<argc; i++) {
		int command_len = strlen(argv[i]);
		if (!xstrncasecmp(argv[i], "Where", MAX(command_len, 5))
		    || !xstrncasecmp(argv[i], "Set", MAX(command_len, 3)))
			i++;
		set += _set_cond(&i, argc, argv, qos_cond, NULL);
	}

	if (!set) {
		exit_code=1;
		fprintf(stderr,
			" No conditions given to remove, not executing.\n");
		slurmdb_destroy_qos_cond(qos_cond);
		return SLURM_ERROR;
	} else if (set == -1) {
		slurmdb_destroy_qos_cond(qos_cond);
		return SLURM_ERROR;
	}

	if (!g_qos_list)
		g_qos_list = slurmdb_qos_get(
			db_conn, NULL);

	notice_thread_init();
	ret_list = slurmdb_qos_remove(db_conn, qos_cond);
	notice_thread_fini();
	slurmdb_destroy_qos_cond(qos_cond);

	if (ret_list && list_count(ret_list)) {
		char *object = NULL;
		ListIterator itr = NULL;

		/* Check to see if person is trying to remove a default
		 * qos of an association.  _isdefault only works with the
		 * output from slurmdb_qos_remove, and
		 * with a previously got g_qos_list.
		 */
		if (_isdefault(ret_list)) {
			exit_code=1;
			fprintf(stderr, " Please either remove the qos' listed "
				"above from list and resubmit,\n"
				" or change the default qos to "
				"remove the qos.\n"
				" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
			goto end_it;
		}

		itr = list_iterator_create(ret_list);
		printf(" Deleting QOS(s)...\n");

		while((object = list_next(itr))) {
			printf("  %s\n", object);
		}
		list_iterator_destroy(itr);
		if (commit_check("Would you like to commit changes?")) {
			slurmdb_connection_commit(db_conn, 1);
		} else {
			printf(" Changes Discarded\n");
			slurmdb_connection_commit(db_conn, 0);
		}
	} else if (ret_list) {
		printf(" Nothing deleted\n");
		rc = SLURM_ERROR;
	} else {
		exit_code=1;
		fprintf(stderr, " Error with request: %s\n",
			slurm_strerror(errno));
		rc = SLURM_ERROR;
	}

end_it:
	FREE_NULL_LIST(ret_list);

	return rc;
}
