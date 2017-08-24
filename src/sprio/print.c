/*****************************************************************************\
 *  print.c - sprio print job functions
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Don Lipari <lipari1@llnl.gov>
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

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/slurmctld/slurmctld.h"
#include "src/sprio/print.h"
#include "src/sprio/sprio.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

static int	_print_str(char *str, int width, bool right, bool cut_output);

/*****************************************************************************
 * Global Print Functions
 *****************************************************************************/

int print_jobs_array(List jobs, List format)
{
	if (!params.no_header)
		print_job_from_format(NULL, format);

	if (params.weights) {
		print_job_from_format((priority_factors_object_t *) -1, format);
		return SLURM_SUCCESS;
	}

	/* Print the jobs of interest */
	if (jobs)
		list_for_each (jobs, (ListForF) print_job_from_format,
			       (void *) format);

	return SLURM_SUCCESS;
}

static double _get_priority(priority_factors_object_t *prio_factors)
{
	int i = 0;
	double priority = prio_factors->priority_age
		+ prio_factors->priority_fs
		+ prio_factors->priority_js
		+ prio_factors->priority_part
		+ prio_factors->priority_qos
		- (double)((int64_t)prio_factors->nice - NICE_OFFSET);

	for (i = 0; i < prio_factors->tres_cnt; i++) {
		if (!prio_factors->priority_tres[i])
			continue;
		priority += prio_factors->priority_tres[i];
	}

	/* Priority 0 is reserved for held jobs */
        if (priority < 1)
                priority = 1;

	return priority;
}

static int _print_str(char *str, int width, bool right, bool cut_output)
{
	char format[64];
	int printed = 0;

	if (right == true && width != 0)
		snprintf(format, 64, "%%%ds", width);
	else if (width != 0)
		snprintf(format, 64, "%%.%ds", width);
	else {
		format[0] = '%';
		format[1] = 's';
		format[2] = '\0';
	}

	if ((width == 0) || (cut_output == false)) {
		if ((printed = printf(format, str)) < 0)
			return printed;
	} else {
		char temp[width + 1];
		snprintf(temp, width + 1, format, str);
		if ((printed = printf("%s",temp)) < 0)
			return printed;
	}

	while (printed++ < width)
		printf(" ");

	return printed;
}

static int _print_int(double number, int width, bool right, bool cut_output)
{
	char buf[32];

	snprintf(buf, 32, "%.0f", number);
	return _print_str(buf, width, right, cut_output);
}

static int _print_norm(double number, int width, bool right, bool cut_output)
{
	char buf[32];

	snprintf(buf, 32, "%.7lf", number);
	return _print_str(buf, width, right, cut_output);
}


/*****************************************************************************
 * Job Print Functions
 *****************************************************************************/
int print_job_from_format(priority_factors_object_t * job, List list)
{
	ListIterator i = list_iterator_create(list);
	job_format_t *current;
	int total_width = 0;

	while ((current = (job_format_t *) list_next(i)) != NULL) {
		if (current->
		    function(job, current->width, current->right_justify,
			     current->suffix)
		    != SLURM_SUCCESS)
			return SLURM_ERROR;
		if (current->width)
			total_width += (current->width + 1);
		else
			total_width += 10;
	}
	list_iterator_destroy(i);

	printf("\n");

	return SLURM_SUCCESS;
}

int job_format_add_function(List list, int width, bool right, char *suffix,
			    int (*function) (priority_factors_object_t *,
			    int, bool, char*))
{
	job_format_t *tmp = (job_format_t *) xmalloc(sizeof(job_format_t));
	tmp->function = function;
	tmp->width = width;
	tmp->right_justify = right;
	tmp->suffix = suffix;

	if (list_append(list, tmp) == NULL) {
		fprintf(stderr, "Memory exhausted\n");
		exit(1);
	}
	return SLURM_SUCCESS;
}


int _print_job_job_id(priority_factors_object_t * job, int width,
		      bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("JOBID", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_str("Weights", width, right, true);
	else {
		char id[FORMAT_STRING_SIZE];
		snprintf(id, FORMAT_STRING_SIZE, "%u", job->job_id);
		_print_str(id, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_prefix(priority_factors_object_t * job, int width,
		      bool right, char* suffix)
{
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_age_priority_normalized(priority_factors_object_t * job, int width,
				   bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("AGE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_age, width, right, true);
	else {
		double num = 0;
		if (weight_age)
			num = job->priority_age / weight_age;
		_print_norm(num, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_age_priority_weighted(priority_factors_object_t * job, int width,
				 bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("AGE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_age, width, right, true);
	else
		_print_int(job->priority_age, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_fs_priority_normalized(priority_factors_object_t * job, int width,
				  bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("FAIRSHARE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_fs, width, right, true);
	else {
		double num = 0;
		if (weight_fs)
			num = job->priority_fs / weight_fs;
		_print_norm(num, width, right, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_fs_priority_weighted(priority_factors_object_t * job, int width,
				bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("FAIRSHARE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_fs, width, right, true);
	else
		_print_int(job->priority_fs, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_priority_normalized(priority_factors_object_t * job, int width,
				   bool right, char* suffix)
{
	char temp[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("PRIORITY", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_str("", width, right, true);
	else {
		double priority = _get_priority(job);
		double prio = priority / (double) ((uint32_t) 0xffffffff);

		sprintf(temp, "%16.14f", prio);
		_print_str(temp, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_priority_weighted(priority_factors_object_t * job, int width,
				 bool right, char* suffix)
{
	char temp[FORMAT_STRING_SIZE];
	if (job == NULL)	/* Print the Header instead */
		_print_str("PRIORITY", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_str("", width, right, true);
	else {
		sprintf(temp, "%lld", (long long)_get_priority(job));
		_print_str(temp, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_js_priority_normalized(priority_factors_object_t * job, int width,
				  bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("JOBSIZE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_js, width, right, true);
	else {
		double num = 0;
		if (weight_js)
			num = job->priority_js / weight_js;
		_print_norm(num, width, right, true);
	}

	if (suffix)
		printf("%s", suffix);

	return SLURM_SUCCESS;
}

int _print_js_priority_weighted(priority_factors_object_t * job, int width,
				bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("JOBSIZE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_js, width, right, true);
	else {
		_print_int(job->priority_js, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);

	return SLURM_SUCCESS;
}

int _print_part_priority_normalized(priority_factors_object_t * job, int width,
				    bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_part, width, right, true);
	else {
		double num = 0;
		if (weight_part)
			num = job->priority_part / weight_part;
		_print_norm(num, width, right, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_part_priority_weighted(priority_factors_object_t * job, int width,
				  bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("PARTITION", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_part, width, right, true);
	else
		_print_int(job->priority_part, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_qos_priority_normalized(priority_factors_object_t * job, int width,
				   bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("QOS", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_qos, width, right, true);
	else {
		double num = 0;
		if (weight_qos)
			num = job->priority_qos / weight_qos;
		_print_norm(num, width, right, true);
	}

	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_qos_priority_weighted(priority_factors_object_t * job, int width,
				 bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("QOS", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_int(weight_qos, width, right, true);
	else
		_print_int(job->priority_qos, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_nice(priority_factors_object_t * job, int width,
				bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("NICE", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_str("", width, right, true);
	else
		_print_int((int64_t)job->nice - NICE_OFFSET, width, right, true);
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_job_user_name(priority_factors_object_t * job, int width,
			 bool right, char* suffix)
{
	if (job == NULL)	/* Print the Header instead */
		_print_str("USER", width, right, true);
	else if (job == (priority_factors_object_t *) -1)
		_print_str("", width, right, true);
	else {
		char *uname = uid_to_string_cached((uid_t) job->user_id);
		_print_str(uname, width, right, true);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_tres_normalized(priority_factors_object_t * job, int width,
			   bool right, char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("TRES", width, right, true);
	} else if (job == (priority_factors_object_t *) -1)
		_print_str("", width, right, true);
	else {
		char *values = xstrdup("");
		int i = 0;

		for (i = 0; i < job->tres_cnt; i++) {
			if (!job->priority_tres[i])
				continue;
			if (values[0])
				xstrcat(values, ",");
			xstrfmtcat(values, "%s=%.2f", job->tres_names[i],
				   job->priority_tres[i]/job->tres_weights[i]);
		}

		_print_str(values, width, right, true);
		xfree(values);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

int _print_tres_weighted(priority_factors_object_t * job, int width,
			 bool right, char* suffix)
{
	if (job == NULL) {	/* Print the Header instead */
		_print_str("TRES", width, right, true);
	} else if (job == (priority_factors_object_t *) -1)
		_print_str(weight_tres, width, right, true);
	else {
		char *values = xstrdup("");
		int i = 0;

		for (i = 0; i < job->tres_cnt; i++) {
			if (!job->priority_tres[i])
				continue;
			if (values[0])
				xstrcat(values, ",");
			xstrfmtcat(values, "%s=%.0f", job->tres_names[i],
				   job->priority_tres[i]);
		}

		_print_str(values, width, right, true);
		xfree(values);
	}
	if (suffix)
		printf("%s", suffix);
	return SLURM_SUCCESS;
}

