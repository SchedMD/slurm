/*****************************************************************************\
 *  print.c - print functions for sstat
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
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

#include "sstat.h"
#include "src/common/cpu_frequency.h"
#include "src/common/parse_time.h"
#include "slurm.h"
#define FORMAT_STRING_SIZE 34

print_field_t *field = NULL;
int curr_inx = 1;
char outbuf[FORMAT_STRING_SIZE];

static int tres_disk_id = -1;

char *_elapsed_time(long secs, long usecs);

char *_elapsed_time(long secs, long usecs)
{
	long	days, hours, minutes, seconds;
	long    subsec = 0;
	char *str = NULL;

	if ((secs < 0) || (secs == NO_VAL))
		return NULL;


	while (usecs >= 1E6) {
		secs++;
		usecs -= 1E6;
	}
	if (usecs > 0) {
		/* give me 3 significant digits to tack onto the sec */
		subsec = (usecs/1000);
	}
	seconds =  secs % 60;
	minutes = (secs / 60)   % 60;
	hours   = (secs / 3600) % 24;
	days    =  secs / 86400;

	if (days)
		str = xstrdup_printf("%ld-%2.2ld:%2.2ld:%2.2ld",
				     days, hours, minutes, seconds);
	else if (hours)
		str = xstrdup_printf("%2.2ld:%2.2ld:%2.2ld",
				     hours, minutes, seconds);
	else
		str = xstrdup_printf("%2.2ld:%2.2ld.%3.3ld",
				     minutes, seconds, subsec);
	return str;
}

static void _print_small_double(
	char *outbuf, int buf_size, double dub, int units)
{
	if (fuzzy_equal(dub, NO_VAL))
		return;

	if (dub > 1)
		convert_num_unit((double)dub, outbuf, buf_size, units, NO_VAL,
				 params.convert_flags);
	else if (dub > 0)
		snprintf(outbuf, buf_size, "%.2fM", dub);
	else
		snprintf(outbuf, buf_size, "0");
}

static void _print_tres_field(char *tres_in, char *nodes, bool convert)
{
	char *tmp_char = slurmdb_make_tres_string_from_simple(
		tres_in, assoc_mgr_tres_list,
		convert ? params.units : NO_VAL,
		convert ? params.convert_flags : CONVERT_NUM_UNIT_RAW,
		nodes);

	field->print_routine(field, tmp_char, (curr_inx == field_count));
	xfree(tmp_char);
	return;
}

static void _set_disk_tres_id(void)
{
	static bool first = 1;
	slurmdb_tres_rec_t *tres_rec;
	char *name;

	if (!first && tres_disk_id != -1)
		return;

	first = 0;

	name = "fs/disk";
	if ((tres_rec = list_find_first(assoc_mgr_tres_list,
					slurmdb_find_tres_in_list_by_type,
					name)))
		tres_disk_id = tres_rec->id;
}

void print_fields(slurmdb_step_rec_t *step)
{
//	print_field_t *field = NULL;
//	int curr_inx = 1;
//	char outbuf[FORMAT_STRING_SIZE];

	list_iterator_reset(print_fields_itr);
	while ((field = list_next(print_fields_itr))) {
		char *tmp_char = NULL;
		uint64_t tmp_uint64 = NO_VAL64;

		memset(&outbuf, 0, sizeof(outbuf));
		switch(field->type) {
		case PRINT_AVECPU:

			tmp_char = _elapsed_time((long)step->stats.cpu_ave, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_ACT_CPUFREQ:

			convert_num_unit2((double)step->stats.act_cpufreq,
					  outbuf, sizeof(outbuf), UNIT_KILO,
					  NO_VAL, 1000, params.convert_flags &
					  (~CONVERT_NUM_UNIT_EXACT));

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY:
			if (!fuzzy_equal(step->stats.consumed_energy,
					 NO_VAL64)) {
				convert_num_unit2((double)
						  step->stats.consumed_energy,
						  outbuf, sizeof(outbuf),
						  UNIT_NONE, NO_VAL, 1000,
						  params.convert_flags &
						  (~CONVERT_NUM_UNIT_EXACT));
			}
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY_RAW:
			field->print_routine(field,
					     step->stats.consumed_energy,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEDISKREAD:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.tres_usage_in_ave,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			if (tmp_uint64 != NO_VAL64)
				_print_small_double(outbuf, sizeof(outbuf),
						    (double)tmp_uint64,
						    UNIT_NONE);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEDISKWRITE:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.tres_usage_out_ave,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			if (tmp_uint64 != NO_VAL64)
				_print_small_double(outbuf, sizeof(outbuf),
						    (double)tmp_uint64,
						    UNIT_NONE);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEPAGES:
			convert_num_unit((double)step->stats.pages_ave, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVERSS:
			convert_num_unit((double)step->stats.rss_ave, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEVSIZE:
			convert_num_unit((double)step->stats.vsize_ave, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_JOBID:
			if (step->stepid == SLURM_BATCH_SCRIPT)
				snprintf(outbuf, sizeof(outbuf), "%u.batch",
					 step->job_ptr->jobid);
			else if (step->stepid == SLURM_EXTERN_CONT)
				snprintf(outbuf, sizeof(outbuf), "%u.extern",
					 step->job_ptr->jobid);
			else
				snprintf(outbuf, sizeof(outbuf), "%u.%u",
					 step->job_ptr->jobid,
					 step->stepid);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREAD:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.tres_usage_in_max,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			if (tmp_uint64 != NO_VAL64)
				_print_small_double(outbuf, sizeof(outbuf),
						    (double)tmp_uint64,
						    UNIT_NONE);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREADNODE:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				tmp_char = find_hostname(
					slurmdb_find_tres_count_in_string(
						step->stats.
						tres_usage_in_max_nodeid,
						tres_disk_id),
					step->nodes);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKREADTASK:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.
					     tres_usage_in_max_taskid,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			field->print_routine(field,
					     tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITE:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.tres_usage_out_max,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			if (tmp_uint64 != NO_VAL64)
				_print_small_double(outbuf, sizeof(outbuf),
						    (double)tmp_uint64,
						    UNIT_NONE);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITENODE:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				tmp_char = find_hostname(
					slurmdb_find_tres_count_in_string(
						step->stats.
						tres_usage_out_max_nodeid,
						tres_disk_id),
					step->nodes);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKWRITETASK:
			_set_disk_tres_id();

			if (tres_disk_id != -1)
				if ((tmp_uint64 =
				     slurmdb_find_tres_count_in_string(
					     step->stats.
					     tres_usage_in_max_taskid,
					     tres_disk_id)) == INFINITE64)
					tmp_uint64 = NO_VAL64;

			field->print_routine(field,
					     tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGES:
			convert_num_unit((double)step->stats.pages_max, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGESNODE:
			tmp_char = find_hostname(
					step->stats.pages_max_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXPAGESTASK:
			field->print_routine(field,
					     step->stats.pages_max_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSS:
			convert_num_unit((double)step->stats.rss_max, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSSNODE:
			tmp_char = find_hostname(
					step->stats.rss_max_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXRSSTASK:
			field->print_routine(field,
					     step->stats.rss_max_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZE:
			convert_num_unit((double)step->stats.vsize_max, outbuf,
					 sizeof(outbuf), UNIT_KILO, NO_VAL,
					 params.convert_flags);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZENODE:
			tmp_char = find_hostname(
					step->stats.vsize_max_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXVSIZETASK:
			field->print_routine(field,
					     step->stats.vsize_max_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MINCPU:
			tmp_char = _elapsed_time((long)step->stats.cpu_min, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUNODE:
			tmp_char = find_hostname(
					step->stats.cpu_min_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUTASK:
			field->print_routine(field,
					     step->stats.cpu_min_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_TRESUIA:
			_print_tres_field(step->stats.tres_usage_in_ave,
					  NULL, 1);
			break;
		case PRINT_TRESUIM:
			_print_tres_field(step->stats.tres_usage_in_max,
					  NULL, 1);
			break;
		case PRINT_TRESUIMN:
			_print_tres_field(step->stats.tres_usage_in_max_nodeid,
					  step->nodes, 0);
			break;
		case PRINT_TRESUIMT:
			_print_tres_field(step->stats.tres_usage_in_max_taskid,
					  NULL, 0);
			break;
		case PRINT_TRESUOA:
			_print_tres_field(step->stats.tres_usage_out_ave,
					  NULL, 1);
			break;
		case PRINT_TRESUOM:
			_print_tres_field(step->stats.tres_usage_out_max,
					  NULL, 1);
			break;
		case PRINT_TRESUOMN:
			_print_tres_field(step->stats.tres_usage_out_max_nodeid,
					  step->nodes, 0);
			break;
		case PRINT_TRESUOMT:
			_print_tres_field(step->stats.tres_usage_out_max_taskid,
					  NULL, 0);
			break;
		case PRINT_NODELIST:
			field->print_routine(field,
					     step->nodes,
					     (curr_inx == field_count));
			break;
		case PRINT_NTASKS:
			field->print_routine(field,
					     step->ntasks,
					     (curr_inx == field_count));
			break;
		case PRINT_PIDS:
                        field->print_routine(field,
                                             step->pid_str,
                                             (curr_inx == field_count));
                        break;
		case PRINT_REQ_CPUFREQ_MIN:
			cpu_freq_to_string(outbuf, sizeof(outbuf),
					   step->req_cpufreq_min);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ_MAX:
			cpu_freq_to_string(outbuf, sizeof(outbuf),
					   step->req_cpufreq_max);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ_GOV:
			cpu_freq_to_string(outbuf, sizeof(outbuf),
					   step->req_cpufreq_gov);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		default:
			break;
		}
		curr_inx++;
	}
	printf("\n");
}

