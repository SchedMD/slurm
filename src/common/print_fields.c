/*****************************************************************************\
 *  print.c - definitions for all printing functions.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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
#include "src/common/print_fields.h"
#include "src/common/parse_time.h"
#include "src/common/read_config.h"
#include "src/common/sluid.h"

int print_fields_parsable_print = 0;
int print_fields_have_header = 1;
char *fields_delimiter = NULL;

typedef enum {
    STATE_INIT,
    STATE_EXPAND,
    STATE_DONE,
    STATE_ESCAPE,
    STATE_ERROR
} parser_state;

extern void destroy_print_field(void *object)
{
	print_field_t *field = (print_field_t *)object;

	if (field) {
		xfree(field->name);
		xfree(field);
	}
}

extern void print_fields_header(list_t *print_fields_list)
{
	list_itr_t *itr = NULL;
	print_field_t *field = NULL;
	int curr_inx = 1;
	int field_count = 0;

	if (!print_fields_list || !print_fields_have_header)
		return;

	field_count = list_count(print_fields_list);

	itr = list_iterator_create(print_fields_list);
	while ((field = list_next(itr))) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && (curr_inx == field_count))
			printf("%s", field->name);
		else if (print_fields_parsable_print
			 && fields_delimiter) {
			printf("%s%s", field->name, fields_delimiter);
		} else if (print_fields_parsable_print
			 && !fields_delimiter) {
			printf("%s|", field->name);

		} else {
			int abs_len = abs(field->len);
			printf("%*.*s ", field->len, abs_len, field->name);
		}
		curr_inx++;
	}
	list_iterator_reset(itr);
	printf("\n");
	if (print_fields_parsable_print) {
		list_iterator_destroy(itr);
		return;
	}
	while ((field = list_next(itr))) {
		int abs_len = abs(field->len);
		for (int i = 0; i < abs_len; i++)
			putchar('-');
		putchar(' ');
	}
	list_iterator_destroy(itr);
	printf("\n");
}

extern void print_fields_date(print_field_t *field, void *input, int last)
{
	int abs_len = print_fields_parsable_print ? 256 : abs(field->len);
	char temp_char[abs_len+1];
	time_t value = 0;

	if (input)
		value = *(time_t *) input;

	slurm_make_time_str(&value, (char *)temp_char, sizeof(temp_char));
	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", temp_char);
	else if (print_fields_parsable_print && !fields_delimiter)
		printf("%s|", temp_char);
	else if (print_fields_parsable_print && fields_delimiter)
		printf("%s%s", temp_char, fields_delimiter);
	else if (field->len == abs_len)
		printf("%*.*s ", abs_len, abs_len, temp_char);
	else
		printf("%-*.*s ", abs_len, abs_len, temp_char);
}

extern void print_fields_str(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	char temp_char[abs_len+1];
	char *print_this = NULL;
	char *value = NULL;

	if (input)
		value = input;

	if (!value) {
		if (print_fields_parsable_print)
			print_this = "";
		else
			print_this = " ";
	} else
		print_this = value;

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print && !fields_delimiter)
		printf("%s|", print_this);
	else if (print_fields_parsable_print && fields_delimiter)
		printf("%s%s", print_this, fields_delimiter);
	else {
		if (value) {
			int len = strlen(value);
			memcpy(&temp_char, value, MIN(len, abs_len) + 1);

			if (len > abs_len)
				temp_char[abs_len-1] = '+';
			print_this = temp_char;
		}

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
}

extern void print_fields_uint16(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	uint16_t value = NO_VAL16;

	if (input)
		value = *(uint16_t *) input;

	/* (value == unset)  || (value == cleared) */
	if (((uint16_t)value == NO_VAL16)
	    || ((uint16_t)value == INFINITE16)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%u", value);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%u|", value);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%u%s", value, fields_delimiter);
		else if (field->len == abs_len)
			printf("%*u ", abs_len, value);
		else
			printf("%-*u ", abs_len, value);
	}
}

extern void print_fields_uint32(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	uint32_t value = NO_VAL;

	if (input)
		value = *(uint32_t *) input;

	/* (value == unset)  || (value == cleared) */
	if ((value == NO_VAL) || (value == INFINITE)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%u", value);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%u|", value);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%u%s", value, fields_delimiter);
		else if (field->len == abs_len)
			printf("%*u ", abs_len, value);
		else
			printf("%-*u ", abs_len, value);
	}
}

extern void print_fields_uint64(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	uint64_t value = NO_VAL64;

	if (input)
		value = *(uint64_t *) input;

	/* (value == unset)  || (value == cleared) */
	if ((value == NO_VAL64) || (value == INFINITE64)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%llu", (long long unsigned) value);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%llu|", (long long unsigned) value);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%llu%s", (long long unsigned) value,
				fields_delimiter);
		else if (field->len == abs_len)
			printf("%*llu ", abs_len, (long long unsigned) value);
		else
			printf("%-*llu ", abs_len, (long long unsigned) value);
	}
}

extern void print_fields_double(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	double value = (double) NO_VAL64;

	if (input)
		value = *(double *) input;

	/* (value == unset)  || (value == cleared) */
	if ((value == (double) NO_VAL64) || (value == (double) INFINITE64) ||
	    (value == (double) NO_VAL) || (value == (double) INFINITE)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%f", value);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%f|", value);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%f%s", value, fields_delimiter);
		else {
			int length, width = abs_len;
			int new_length;
			char *tmp = NULL;
			xstrfmtcat(tmp, "%*f", abs_len, value);
			length = strlen(tmp);
			if (length > width) {
				xstrfmtcat(tmp, "%*.*e", width, width, value);
				new_length = strlen(tmp) - length;
				if (new_length > width)
					width -= new_length - width;
				if (field->len == abs_len)
					printf("%*.*e ", width, width, value);
				else
					printf("%-*.*e ", width, width, value);
			} else {
				if (field->len == abs_len)
					printf("%*f ", width, value);
				else
					printf("%-*f ", width, value);
			}
			xfree(tmp);
		}
	}
}

extern void print_fields_time(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	uint32_t value = NO_VAL;

	if (input)
		value = *(uint32_t *) input;

	/* (value == unset)  || (value == cleared) */
	if ((value == NO_VAL) || (value == INFINITE)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		char time_buf[32];
		mins2time_str((time_t) value, time_buf, sizeof(time_buf));
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", time_buf);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%s|", time_buf);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s%s", time_buf, fields_delimiter);
		else if (field->len == abs_len)
			printf("%*s ", abs_len, time_buf);
		else
			printf("%-*s ", abs_len, time_buf);
	}
}

extern void print_fields_time_from_secs(print_field_t *field,
					void *input, int last)
{
	int abs_len = abs(field->len);
	uint64_t value = NO_VAL64;

	if (input)
		value = *(uint64_t *) input;

	/* (value == unset)  || (value == cleared) */
	if ((value == NO_VAL64) || (value == INFINITE64)) {
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			;
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("|");
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s", fields_delimiter);
		else
			printf("%*s ", field->len, " ");
	} else {
		char time_buf[32];
		secs2time_str((time_t) value, time_buf, sizeof(time_buf));
		if (print_fields_parsable_print
		   == PRINT_FIELDS_PARSABLE_NO_ENDING
		   && last)
			printf("%s", time_buf);
		else if (print_fields_parsable_print && !fields_delimiter)
			printf("%s|", time_buf);
		else if (print_fields_parsable_print && fields_delimiter)
			printf("%s%s", time_buf, fields_delimiter);
		else if (field->len == abs_len)
			printf("%*s ", abs_len, time_buf);
		else
			printf("%-*s ", abs_len, time_buf);
	}
}

extern void print_fields_sluid(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	sluid_t sluid = 0;
	char *sluid_str = NULL;

	if (input)
		sluid = *(sluid_t *) input;

	sluid_str = sluid2str(sluid);

	if ((print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING)
	    && last)
		printf("%s", sluid_str);
	else if (print_fields_parsable_print && !fields_delimiter)
		printf("%s|", sluid_str);
	else if (print_fields_parsable_print && fields_delimiter)
		printf("%s%s", sluid_str, fields_delimiter);
	else if (field->len == abs_len)
		printf("%*s ", abs_len, sluid_str);
	else
		printf("%-*s ", abs_len, sluid_str);

	xfree(sluid_str);
}

extern void print_fields_char_list(print_field_t *field, void *input, int last)
{
	int abs_len = abs(field->len);
	char *print_this = NULL;
	list_t *value = NULL;

	if (input)
		value = *(list_t **) input;

	if (!value || !list_count(value)) {
		if (print_fields_parsable_print)
			print_this = xstrdup("");
		else
			print_this = xstrdup(" ");
	} else {
		print_this = slurm_char_list_to_xstr(value);
	}

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	   && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print && !fields_delimiter)
		printf("%s|", print_this);
	else if (print_fields_parsable_print && fields_delimiter)
		printf("%s%s", print_this, fields_delimiter);
	else if (print_this) {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

static bool _is_wildcard(char *ptr)
{
	switch (*ptr) {
	case 'A': /* Array job ID */
	case 'a': /* Array task ID */
	case 'b': /* Array task ID modulo 10 */
	case 'J': /* Jobid.stepid */
	case 'j': /* Job ID */
	case 'N': /* Short hostname */
	case 'n': /* Node id relative to current job */
	case 's': /* Stepid of the running job */
	case 't': /* Task id (rank) relative to current job */
	case 'u': /* User name */
	case 'x':
		return true;
		break;
	default:
		break;
	}
	return false;
}

static void _expand_wildcard(char **expanded, char **pos, char *ptr,
			     uint32_t padding, job_std_pattern_t *job)
{
	switch (*ptr) {
	case 'A': /* Array job ID */
		if (job->array_job_id) {
			xstrfmtcatat(*expanded, pos, "%0*u", padding,
				     job->array_job_id);
		} else {
			xstrfmtcatat(*expanded, pos, "%0*u", padding,
				     job->jobid);
		}
		break;
	case 'J': /* Jobid.stepid */
	case 'j': /* Job ID */
		xstrfmtcatat(*expanded, pos, "%0*u", padding, job->jobid);
		break;
	case 'a': /* Array task ID */
		xstrfmtcatat(*expanded, pos, "%0*u", padding,
			     job->array_task_id);
		break;
	case 'b': /* Array task ID modulo 10 */
		xstrfmtcatat(*expanded, pos, "%0*u", padding,
			     job->array_task_id % 10);
		break;
	case 'N': /* Short hostname */
		xstrfmtcatat(*expanded, pos, "%s", job->first_step_node);
		break;
	case 's': /* Stepid of the running job */
		xstrfmtcatat(*expanded, pos, "%s", job->first_step_name);
		break;
	case 'n': /* Node id relative to current job */
	case 't': /* Task id (rank) relative to current job */
		xstrfmtcatat(*expanded, pos, "0");
		break;
	case 'u': /* User name */
		xstrfmtcatat(*expanded, pos, "%s", job->user);
		break;
	case 'x':
		xstrfmtcatat(*expanded, pos, "%s", job->jobname);
		break;
	default:
		break;
	}
}

/*
 * Special expansion function for stdin/stdout/stderr filename patterns.
 * Fields that can potientially map to a range of values will use the first in
 * that range (e.g %t is replaced by 0).
 *
 * The parser do not support steps and is only for batch jobs.
 *
 * \      If we found this symbol, don't replace anything.
 * %%     The character "%".
 * %A     Job array's master job allocation number.
 * %a     Job array ID (index) number.
 * %J     jobid.stepid of the running job. (e.g. "128.0" or "batch")
 * %j     jobid of the running job.
 * %N     short hostname. This will create a separate IO file per node.
 * %n     Node identifier relative to current job.
 * %s     stepid of the running job.
 * %t     task identifier (rank) relative to current job.
 * %u     User name.
 * %x     Job name.
 *
 * A number placed between the percent character and format specifier may be
 * used to zero-pad the result. Ignored if the format specifier corresponds to
 * non-numeric data (%N for example). The maximum padding is 10.
 * job%J.out      job128.0.out
 * job%4j.out     job0128.out
 * job%2j-%2t.out job128-00.out, job128-01.out, ...
 *
 * OUT - Expanded path as xstring. Must xfree.
 * IN stdio_path - stdin/stdout/stderr job filepath
 * IN job - job record in the database
 */
extern char *expand_stdio_fields(char *stdio_path, job_std_pattern_t *job)
{
	parser_state curr_state = STATE_INIT;
	char *ptr, *end, *expanded = NULL, *pos = NULL;
	uint32_t padding = 0;

	if (!stdio_path || !*stdio_path || !job)
		return NULL;

	if (stdio_path[0] != '/')
		xstrcatat(expanded, &pos, job->work_dir);

	/*
	 * Special case, if we find a \ it means the file has not been
	 * expanded in any case, so skip any replacement from now on.
	 */
	if (xstrstr(stdio_path, "\\"))
		curr_state = STATE_ESCAPE;

	ptr = stdio_path;
	while (*ptr) {
		switch (curr_state) {
		case STATE_ESCAPE:
			if (*ptr != '\\')
				xstrfmtcatat(expanded, &pos, "%c", *ptr);
			break;
		case STATE_INIT:
			if (*ptr == '%')
				curr_state = STATE_EXPAND;
			else
				xstrfmtcatat(expanded, &pos, "%c", *ptr);
			break;
		case STATE_EXPAND:
			/* Double %% is escape, so print one %. */
			if (*ptr == '%') {
				xstrfmtcatat(expanded, &pos, "%c", *ptr);
				curr_state = STATE_INIT;
				break;
			}
			if (isdigit(*ptr)) {
				char *tmp_ptr = ptr;
				if ((padding = strtoul(ptr, &end, 10)) > 9) {
					/* Remove % and double digit 10 */
					ptr = end;
					padding = 10;
				} else {
					ptr++;
				}
				/*
				 * weird behavior fix: we remove all the digits
				 * except the last one to match with the current
				 * fname creation.
				 */
				if (!_is_wildcard(ptr)) {
					ptr = tmp_ptr;
					/* seek until the last digit */
					while (isdigit(*(ptr + 1))) {
						ptr++;
					}
					xstrfmtcatat(expanded, &pos,
						     "%c", *ptr);
					padding = 0;
					curr_state = STATE_INIT;
					break;
				}
			}
			if (!_is_wildcard(ptr)) {
				/* If not a wildcard print also the %. */
				xstrfmtcatat(expanded, &pos, "%%%c", *ptr);
			} else {
				_expand_wildcard(&expanded, &pos, ptr, padding,
						 job);
				padding = 0;
			}
			curr_state = STATE_INIT;
			break;
		default:
			break;
		}
		ptr++;
	}

	return expanded;
}
