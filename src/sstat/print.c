/*****************************************************************************\
 *  print.c - print functions for sstat
 *
 *  $Id: print.c 7541 2006-03-18 01:44:58Z da $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>.
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

#include "sstat.h"
#include "src/common/cpu_frequency.h"
#include "src/common/parse_time.h"
#include "slurm.h"
#define FORMAT_STRING_SIZE 34

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
		convert_num_unit((double)dub, outbuf, buf_size, units);
	else if (dub > 0)
		snprintf(outbuf, buf_size, "%.2fM", dub);
	else
		snprintf(outbuf, buf_size, "0");
}

void print_fields(slurmdb_step_rec_t *step)
{
	print_field_t *field = NULL;
	int curr_inx = 1;
	char outbuf[FORMAT_STRING_SIZE];

	list_iterator_reset(print_fields_itr);
	while ((field = list_next(print_fields_itr))) {
		char *tmp_char = NULL;

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
					  outbuf, sizeof(outbuf),
					  UNIT_KILO, 1000, false);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY:
			if (!fuzzy_equal(step->stats.consumed_energy, NO_VAL)) {
				convert_num_unit2((double)
						  step->stats.consumed_energy,
						  outbuf, sizeof(outbuf),
						  UNIT_NONE, 1000, false);
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
			_print_small_double(outbuf, sizeof(outbuf),
					    step->stats.disk_read_ave,
					    UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEDISKWRITE:
			_print_small_double(outbuf, sizeof(outbuf),
					    step->stats.disk_write_ave,
					    UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEPAGES:
			convert_num_unit((double)step->stats.pages_ave,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVERSS:
			convert_num_unit((double)step->stats.rss_ave,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEVSIZE:
			convert_num_unit((double)step->stats.vsize_ave,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_JOBID:
			if (step->stepid == NO_VAL)
				snprintf(outbuf, sizeof(outbuf), "%u.batch",
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
			_print_small_double(outbuf, sizeof(outbuf),
					    step->stats.disk_read_max,
					    UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREADNODE:
			tmp_char = find_hostname(
					step->stats.disk_read_max_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKREADTASK:
			field->print_routine(field,
					     step->stats.disk_read_max_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITE:
			_print_small_double(outbuf, sizeof(outbuf),
					    step->stats.disk_write_max,
					    UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITENODE:
			tmp_char = find_hostname(
					step->stats.disk_write_max_nodeid,
					step->nodes);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKWRITETASK:
			field->print_routine(field,
					     step->stats.disk_write_max_taskid,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGES:
			convert_num_unit((double)step->stats.pages_max,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

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
			convert_num_unit((double)step->stats.rss_max,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

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
			convert_num_unit((double)step->stats.vsize_max,
					 outbuf, sizeof(outbuf),
					 UNIT_KILO);

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
		case PRINT_REQ_CPUFREQ:
			cpu_freq_to_string(outbuf, sizeof(outbuf),
					   step->req_cpufreq);
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

