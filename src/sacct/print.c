/*****************************************************************************\
 *  print.c - print functions for sacct
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

#include "sacct.h"
#include "src/common/parse_time.h"
#include "slurm.h"

char *_elapsed_time(long secs, long usecs);

char *_elapsed_time(long secs, long usecs)
{
	long	days, hours, minutes, seconds;
	long    subsec = 0;
	char *str = NULL;

	if (secs < 0 || secs == (long)NO_VAL)
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
	else if (subsec)
		str = xstrdup_printf("%2.2ld:%2.2ld.%3.3ld",
				     minutes, seconds, subsec);
	else
		str = xstrdup_printf("00:%2.2ld:%2.2ld",
				     minutes, seconds);
	return str;
}

static char *_find_qos_name_from_list(
	List qos_list, int qosid)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;

	if (!qos_list || qosid == NO_VAL)
		return NULL;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (qosid == qos->id)
			break;
	}
	list_iterator_destroy(itr);

	if (qos)
		return qos->name;
	else
		return "Unknown";
}

static void _print_small_double(
	char *outbuf, int buf_size, double dub, int units)
{
	if (fuzzy_equal(dub, NO_VAL))
		return;

	if (dub > 1)
		convert_num_unit((float)dub, outbuf, buf_size, units);
	else if (dub > 0)
		snprintf(outbuf, buf_size, "%.2fM", dub);
	else
		snprintf(outbuf, buf_size, "0");
}

void print_fields(type_t type, void *object)
{
	slurmdb_job_rec_t *job = (slurmdb_job_rec_t *)object;
	slurmdb_step_rec_t *step = (slurmdb_step_rec_t *)object;
	jobcomp_job_rec_t *job_comp = (jobcomp_job_rec_t *)object;
	print_field_t *field = NULL;
	int curr_inx = 1;
	struct passwd *pw = NULL;
	struct	group *gr = NULL;
	char outbuf[FORMAT_STRING_SIZE];

	switch(type) {
	case JOB:
		step = NULL;
		if (!job->track_steps)
			step = (slurmdb_step_rec_t *)job->first_step_ptr;
		/* set this to avoid printing out info for things that
		   don't mean anything.  Like an allocation that never
		   ran anything.
		*/
		if (!step)
			job->track_steps = 1;

		break;
	default:
		break;
	}

	list_iterator_reset(print_fields_itr);
	while((field = list_next(print_fields_itr))) {
		char *tmp_char = NULL;
		int tmp_int = NO_VAL, tmp_int2 = NO_VAL;
		double tmp_dub = (double)NO_VAL;
		uint32_t tmp_uint32 = (uint32_t)NO_VAL;
		uint64_t tmp_uint64 = (uint64_t)NO_VAL;

		memset(&outbuf, 0, sizeof(outbuf));
		switch(field->type) {
		case PRINT_ALLOC_CPUS:
			switch(type) {
			case JOB:
				tmp_int = job->alloc_cpus;
				// we want to use the step info
				if (!step)
					break;
			case JOBSTEP:
				tmp_int = step->ncpus;
				break;
			case JOBCOMP:
			default:
				tmp_int = NO_VAL;
				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_ACCOUNT:
			switch(type) {
			case JOB:
				tmp_char = job->account;
				break;
			case JOBSTEP:
				tmp_char = step->job_ptr->account;
				break;
			case JOBCOMP:
			default:
				tmp_char = "n/a";
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_ACT_CPUFREQ:
			switch (type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = step->stats.act_cpufreq;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.act_cpufreq;
				break;
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit2((float)tmp_dub,
						  outbuf, sizeof(outbuf),
						  UNIT_KILO, 1000, false);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_ASSOCID:
			switch(type) {
			case JOB:
				tmp_int = job->associd;
				break;
			case JOBSTEP:
				tmp_int = step->job_ptr->associd;
				break;
			case JOBCOMP:
			default:
				tmp_int = NO_VAL;
				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_AVECPU:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.cpu_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.cpu_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}

			if (!fuzzy_equal(tmp_dub, NO_VAL))
				tmp_char = _elapsed_time((long)tmp_dub, 0);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_AVEDISKREAD:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.disk_read_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.disk_read_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}
			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEDISKWRITE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.disk_write_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.disk_write_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}
			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEPAGES:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.pages_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.pages_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit((float)tmp_dub,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVERSS:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.rss_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.rss_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit((float)tmp_dub,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_AVEVSIZE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.vsize_ave;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.vsize_ave;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit((float)tmp_dub,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_BLOCKID:
			switch(type) {
			case JOB:
				tmp_char = job->blockid;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:
				tmp_char = job_comp->blockid;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CLUSTER:
			switch(type) {
			case JOB:
				tmp_char = job->cluster;
				break;
			case JOBSTEP:
				tmp_char = step->job_ptr->cluster;
				break;
			case JOBCOMP:
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_COMMENT:
			switch(type) {
			case JOB:
				tmp_char = job->derived_es;
				break;
			case JOBSTEP:
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY:
			switch (type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = step->stats.consumed_energy;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.consumed_energy;
				break;
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit2((float)tmp_dub,
						  outbuf, sizeof(outbuf),
						  UNIT_NONE, 1000, false);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_CONSUMED_ENERGY_RAW:
			switch (type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = step->stats.consumed_energy;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.consumed_energy;
				break;
			default:
				break;
			}

			field->print_routine(field,
					     tmp_dub,
					     (curr_inx == field_count));
			break;
		case PRINT_CPU_TIME:
			switch(type) {
			case JOB:
				tmp_uint64 = (uint64_t)job->elapsed
					* (uint64_t)job->alloc_cpus;
				break;
			case JOBSTEP:
				tmp_uint64 = (uint64_t)step->elapsed
					* (uint64_t)step->ncpus;
				break;
			case JOBCOMP:
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_CPU_TIME_RAW:
			switch(type) {
			case JOB:
				tmp_uint64 = (uint64_t)job->elapsed
					* (uint64_t)job->alloc_cpus;
				break;
			case JOBSTEP:
				tmp_uint64 = (uint64_t)step->elapsed
					* (uint64_t)step->ncpus;
				break;
			case JOBCOMP:
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_uint64,
					     (curr_inx == field_count));
			break;
		case PRINT_DERIVED_EC:
			tmp_int = 0;
			tmp_int2 = 0;
			switch(type) {
			case JOB:
				tmp_int = job->derived_ec;
				if (tmp_int == NO_VAL)
					tmp_int = 0;
				if (WIFSIGNALED(tmp_int))
					tmp_int2 = WTERMSIG(tmp_int);

				snprintf(outbuf, sizeof(outbuf), "%d:%d",
					 WEXITSTATUS(tmp_int), tmp_int2);
				break;
			case JOBSTEP:
			case JOBCOMP:
			default:
				outbuf[0] = '\0';
				break;
			}

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_ELAPSED:
			switch(type) {
			case JOB:
				tmp_int = job->elapsed;
				break;
			case JOBSTEP:
				tmp_int = step->elapsed;
				break;
			case JOBCOMP:
				tmp_int = job_comp->end_time
					- job_comp->start_time;
				break;
			default:
				tmp_int = NO_VAL;
				break;
			}
			field->print_routine(field,
					     (uint64_t)tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_ELIGIBLE:
			switch(type) {
			case JOB:
				tmp_int = job->eligible;
				break;
			case JOBSTEP:
				tmp_int = step->start;
				break;
			case JOBCOMP:
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_END:
			switch(type) {
			case JOB:
				tmp_int = job->end;
				break;
			case JOBSTEP:
				tmp_int = step->end;
				break;
			case JOBCOMP:
				tmp_int = parse_time(job_comp->end_time, 1);
				break;
			default:
				tmp_int = NO_VAL;
				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_EXITCODE:
			tmp_int = 0;
			tmp_int2 = 0;
			switch(type) {
			case JOB:
				tmp_int = job->exitcode;
				break;
			case JOBSTEP:
				tmp_int = step->exitcode;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (WIFSIGNALED(tmp_int))
				tmp_int2 = WTERMSIG(tmp_int);

			snprintf(outbuf, sizeof(outbuf), "%d:%d",
				 WEXITSTATUS(tmp_int), tmp_int2);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_GID:
			switch(type) {
			case JOB:
				tmp_int = job->gid;
				break;
			case JOBSTEP:
				tmp_int = NO_VAL;
				break;
			case JOBCOMP:
				tmp_int = job_comp->gid;
				break;
			default:
				tmp_int = NO_VAL;
				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_GROUP:
			switch(type) {
			case JOB:
				tmp_int = job->gid;
				break;
			case JOBSTEP:
				tmp_int = NO_VAL;
				break;
			case JOBCOMP:
				tmp_int = job_comp->gid;
				break;
			default:
				tmp_int = NO_VAL;
				break;
			}
			tmp_char = NULL;
			if ((gr=getgrgid(tmp_int)))
				tmp_char=gr->gr_name;

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_JOBID:
			switch(type) {
			case JOB:
				tmp_char = xstrdup_printf("%u", job->jobid);
				break;
			case JOBSTEP:
				if (step->stepid == NO_VAL)
					tmp_char = xstrdup_printf(
						"%u.batch",
						step->job_ptr->jobid);
				else
					tmp_char = xstrdup_printf(
						"%u.%u",
						step->job_ptr->jobid,
						step->stepid);
				break;
			case JOBCOMP:
				tmp_char = xstrdup_printf("%u",
							  job_comp->jobid);
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_JOBNAME:
			switch(type) {
			case JOB:
				tmp_char = job->jobname;
				break;
			case JOBSTEP:
				tmp_char = step->stepname;
				break;
			case JOBCOMP:
				tmp_char = job_comp->jobname;
				break;
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_LAYOUT:
			switch(type) {
			case JOB:
				/* below really should be step.  It is
				   not a typo */
				if (!job->track_steps)
					tmp_char = slurm_step_layout_type_name(
						step->task_dist);
				break;
			case JOBSTEP:
				tmp_char = slurm_step_layout_type_name(
					step->task_dist);
				break;
			case JOBCOMP:
				break;
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREAD:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.disk_read_max;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.disk_read_max;
				break;
			case JOBCOMP:
			default:
				break;
			}
			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKREADNODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
						job->stats.disk_read_max_nodeid,
						job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.disk_read_max_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKREADTASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 =
						job->stats.disk_read_max_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.disk_read_max_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.disk_write_max;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.disk_write_max;
				break;
			case JOBCOMP:
			default:
				break;
			}
			_print_small_double(outbuf, sizeof(outbuf),
					    tmp_dub, UNIT_MEGA);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXDISKWRITENODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
					   job->stats.disk_write_max_nodeid,
					   job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.disk_write_max_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXDISKWRITETASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 =
					   job->stats.disk_write_max_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.disk_write_max_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGES:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_int = job->stats.pages_max;
				break;
			case JOBSTEP:
				tmp_int = step->stats.pages_max;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (tmp_int != NO_VAL)
				convert_num_unit((float)tmp_int,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXPAGESNODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
						job->stats.pages_max_nodeid,
						job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.pages_max_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXPAGESTASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 =
						job->stats.pages_max_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.pages_max_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSS:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_int = job->stats.rss_max;
				break;
			case JOBSTEP:
				tmp_int = step->stats.rss_max;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (tmp_int != NO_VAL)
				convert_num_unit((float)tmp_int,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXRSSNODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
						job->stats.rss_max_nodeid,
						job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.rss_max_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXRSSTASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 = job->stats.rss_max_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.rss_max_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_int = job->stats.vsize_max;
				break;
			case JOBSTEP:
				tmp_int = step->stats.vsize_max;
				break;
			case JOBCOMP:
			default:
				tmp_int = NO_VAL;
				break;
			}
			if (tmp_int != NO_VAL)
				convert_num_unit((float)tmp_int,
						 outbuf, sizeof(outbuf),
						 UNIT_KILO);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_MAXVSIZENODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
						job->stats.vsize_max_nodeid,
						job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.vsize_max_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MAXVSIZETASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 =
						job->stats.vsize_max_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.vsize_max_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_MINCPU:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_dub = job->stats.cpu_min;
				break;
			case JOBSTEP:
				tmp_dub = step->stats.cpu_min;
				break;
			case JOBCOMP:
			default:
				break;
			}
			if (!fuzzy_equal(tmp_dub, NO_VAL))
				tmp_char = _elapsed_time((long)tmp_dub, 0);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUNODE:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_char = find_hostname(
						job->stats.cpu_min_nodeid,
						job->nodes);
				break;
			case JOBSTEP:
				tmp_char = find_hostname(
					step->stats.cpu_min_nodeid,
					step->nodes);
				break;
			case JOBCOMP:
			default:
				tmp_char = NULL;
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_MINCPUTASK:
			switch(type) {
			case JOB:
				if (!job->track_steps)
					tmp_uint32 = job->stats.cpu_min_taskid;
				break;
			case JOBSTEP:
				tmp_uint32 = step->stats.cpu_min_taskid;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}
			if (tmp_uint32 == (uint32_t)NO_VAL)
				tmp_uint32 = NO_VAL;
			field->print_routine(field,
					     tmp_uint32,
					     (curr_inx == field_count));
			break;
		case PRINT_NODELIST:
			switch(type) {
			case JOB:
				tmp_char = job->nodes;
				break;
			case JOBSTEP:
				tmp_char = step->nodes;
				break;
			case JOBCOMP:
				tmp_char = job_comp->nodelist;
				break;
			default:
				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_NNODES:
			switch(type) {
			case JOB:
				tmp_int = job->alloc_nodes;
				tmp_char = job->nodes;
				break;
			case JOBSTEP:
				tmp_int = step->nnodes;
				tmp_char = step->nodes;
				break;
			case JOBCOMP:
				tmp_int = job_comp->node_cnt;
				tmp_char = job_comp->nodelist;
				break;
			default:
				break;
			}

			if (!tmp_int) {
				hostlist_t hl = hostlist_create(tmp_char);
				tmp_int = hostlist_count(hl);
				hostlist_destroy(hl);
			}
			convert_num_unit((float)tmp_int,
					 outbuf, sizeof(outbuf), UNIT_NONE);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_NTASKS:
			switch(type) {
			case JOB:
				if (!job->track_steps && !step)
					tmp_int = job->alloc_cpus;
				// we want to use the step info
				if (!step)
					break;
			case JOBSTEP:
				tmp_int = step->ntasks;
				break;
			case JOBCOMP:
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_PRIO:
			switch(type) {
			case JOB:
				tmp_int = job->priority;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_PARTITION:
			switch(type) {
			case JOB:
				tmp_char = job->partition;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:
				tmp_char = job_comp->partition;
				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_QOS:
			switch(type) {
			case JOB:
				tmp_int = job->qosid;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			if (!g_qos_list) {
				slurmdb_qos_cond_t qos_cond;
				memset(&qos_cond, 0,
				       sizeof(slurmdb_qos_cond_t));
				qos_cond.with_deleted = 1;
				g_qos_list = slurmdb_qos_get(
					acct_db_conn, &qos_cond);
			}

			tmp_char = _find_qos_name_from_list(g_qos_list,
							    tmp_int);
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_QOSRAW:
			switch(type) {
			case JOB:
				tmp_int = job->qosid;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUFREQ:
			switch (type) {
			case JOB:
				if (!job->track_steps && !step)
					tmp_dub = NO_VAL;
				// we want to use the step info
				if (!step)
					break;
			case JOBSTEP:
				tmp_dub = step->req_cpufreq;
				break;
			default:
				break;
			}
			if (tmp_dub == CPU_FREQ_LOW)
				snprintf(outbuf, sizeof(outbuf), "Low");
			else if (tmp_dub == CPU_FREQ_MEDIUM)
				snprintf(outbuf, sizeof(outbuf), "Medium");
			else if (tmp_dub == CPU_FREQ_HIGH)
				snprintf(outbuf, sizeof(outbuf), "High");
			else if (!fuzzy_equal(tmp_dub, NO_VAL))
				convert_num_unit2((float)tmp_dub,
						  outbuf, sizeof(outbuf),
						  UNIT_KILO, 1000, false);
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_CPUS:
			switch(type) {
			case JOB:
				tmp_int = job->req_cpus;
				break;
			case JOBSTEP:
				tmp_int = step->ncpus;
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_REQ_MEM:
			switch(type) {
			case JOB:
				tmp_uint32 = job->req_mem;
				break;
			case JOBSTEP:
				tmp_uint32 = step->job_ptr->req_mem;
				break;
			case JOBCOMP:
			default:
				tmp_uint32 = NO_VAL;
				break;
			}

			if (tmp_uint32 != (uint32_t)NO_VAL) {
				bool per_cpu = false;
				if (tmp_uint32 & MEM_PER_CPU) {
					tmp_uint32 &= (~MEM_PER_CPU);
					per_cpu = true;
				}
				convert_num_unit((float)tmp_uint32,
						 outbuf, sizeof(outbuf),
						 UNIT_MEGA);
				if (per_cpu)
					sprintf(outbuf+strlen(outbuf), "c");
				else
					sprintf(outbuf+strlen(outbuf), "n");
			}
			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV:
			switch(type) {
			case JOB:
				if (job->start)
					tmp_int = job->start - job->eligible;
				else
					tmp_int = time(NULL) - job->eligible;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     (uint64_t)tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_CPU:
			switch(type) {
			case JOB:
				if (job->start)
					tmp_int = (job->start - job->eligible)
						* job->req_cpus;
				else
					tmp_int = (time(NULL) - job->eligible)
						* job->req_cpus;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     (uint64_t)tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_RESV_CPU_RAW:
			switch(type) {
			case JOB:
				if (job->start)
					tmp_int = (job->start - job->eligible)
						* job->req_cpus;
				else
					tmp_int = (time(NULL) - job->eligible)
						* job->req_cpus;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_START:
			switch(type) {
			case JOB:
				tmp_int = job->start;
				break;
			case JOBSTEP:
				tmp_int = step->start;
				break;
			case JOBCOMP:
				tmp_int = parse_time(job_comp->start_time, 1);
				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_STATE:
			switch(type) {
			case JOB:
				tmp_int = job->state;
				tmp_int2 = job->requid;
				break;
			case JOBSTEP:
				tmp_int = step->state;
				tmp_int2 = step->requid;
				break;
			case JOBCOMP:
				tmp_char = job_comp->state;
				break;
			default:

				break;
			}

			if (((tmp_int & JOB_STATE_BASE) == JOB_CANCELLED) &&
			    (tmp_int2 != -1))
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s by %d",
					 job_state_string(tmp_int),
					 tmp_int2);
			else if (tmp_int != NO_VAL)
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s",
					 job_state_string(tmp_int));
			else if (tmp_char)
				snprintf(outbuf, FORMAT_STRING_SIZE,
					 "%s",
					 tmp_char);

			field->print_routine(field,
					     outbuf,
					     (curr_inx == field_count));
			break;
		case PRINT_SUBMIT:
			switch(type) {
			case JOB:
				tmp_int = job->submit;
				break;
			case JOBSTEP:
				tmp_int = step->start;
				break;
			case JOBCOMP:
				tmp_int = parse_time(job_comp->start_time, 1);
				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_SUSPENDED:
			switch(type) {
			case JOB:
				tmp_int = job->suspended;
				break;
			case JOBSTEP:
				tmp_int = step->suspended;
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     (uint64_t)tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_SYSTEMCPU:
			switch(type) {
			case JOB:
				tmp_int = job->sys_cpu_sec;
				tmp_int2 = job->sys_cpu_usec;
				break;
			case JOBSTEP:
				tmp_int = step->sys_cpu_sec;
				tmp_int2 = step->sys_cpu_usec;
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			tmp_char = _elapsed_time(tmp_int, tmp_int2);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_TIMELIMIT:
			switch(type) {
			case JOB:
				if (job->timelimit == INFINITE)
					tmp_char = "UNLIMITED";
				else if (job->timelimit == NO_VAL)
					tmp_char = "Partition_Limit";
				else if (job->timelimit) {
					char tmp1[128];
					mins2time_str(job->timelimit,
						      tmp1, sizeof(tmp1));
					tmp_char = tmp1;
				}
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:
				tmp_char = job_comp->timelimit;
				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_TOTALCPU:
			switch(type) {
			case JOB:
				tmp_int = job->tot_cpu_sec;
				tmp_int2 = job->tot_cpu_usec;
				break;
			case JOBSTEP:
				tmp_int = step->tot_cpu_sec;
				tmp_int2 = step->tot_cpu_usec;
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			tmp_char = _elapsed_time(tmp_int, tmp_int2);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_UID:
			switch(type) {
			case JOB:
				if (job->user) {
					if ((pw=getpwnam(job->user)))
						tmp_int = pw->pw_uid;
				} else
					tmp_int = job->uid;
				break;
			case JOBSTEP:
				break;
			case JOBCOMP:
				tmp_int = job_comp->uid;
				break;
			default:

				break;
			}

			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		case PRINT_USER:
			switch(type) {
			case JOB:
				if (job->user)
					tmp_char = job->user;
				else if (job->uid != -1) {
					if ((pw=getpwuid(job->uid)))
						tmp_char = pw->pw_name;
				}
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:
				tmp_char = job_comp->uid_name;
				break;
			default:

				break;
			}

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_USERCPU:
			switch(type) {
			case JOB:
				tmp_int = job->user_cpu_sec;
				tmp_int2 = job->user_cpu_usec;
				break;
			case JOBSTEP:
				tmp_int = step->user_cpu_sec;
				tmp_int2 = step->user_cpu_usec;
				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			tmp_char = _elapsed_time(tmp_int, tmp_int2);

			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			xfree(tmp_char);
			break;
		case PRINT_WCKEY:
			switch(type) {
			case JOB:
				tmp_char = job->wckey;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_char,
					     (curr_inx == field_count));
			break;
		case PRINT_WCKEYID:
			switch(type) {
			case JOB:
				tmp_int = job->wckeyid;
				break;
			case JOBSTEP:

				break;
			case JOBCOMP:

				break;
			default:

				break;
			}
			field->print_routine(field,
					     tmp_int,
					     (curr_inx == field_count));
			break;
		default:
			break;
		}
		curr_inx++;
	}
	printf("\n");
}
