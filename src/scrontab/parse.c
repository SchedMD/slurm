/*****************************************************************************\
 *  parse.c
 *****************************************************************************
 *  Copyright (C) 2020 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <ctype.h>
#include <getopt.h>
#include <unistd.h>

#include "src/common/bitstring.h"
#include "src/common/cron.h"
#include "src/common/fetch_config.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/slurm_time.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "scrontab.h"

static int _parse_range(bitstr_t *b, char **pos)
{
	char *end = *pos;
	char save;

	while (isdigit((int) *end) || *end == '-' || *end == ',')
		end++;

	save = *end;
	*end = '\0';

	if (bit_unfmt(b, *pos)) {
		*end = save;
		return SLURM_ERROR;
	}

	*end = save;
	*pos = end;

	return SLURM_SUCCESS;
}

/*
 * Handle the a step value if present on the line.
 * Advance string pointer to after the step value.
 */
static int _handle_step(bitstr_t *b, int start, char **pos)
{
	char *endptr;
	long stepval;
	bitstr_t *mask;

	if (**pos != '/')
		return SLURM_SUCCESS;

	(*pos)++;
	stepval = strtol(*pos, &endptr, 10);

	if (*pos == endptr || stepval <= 1 || stepval >= bit_size(b)) {
		error("bad step value");
		return SLURM_ERROR;
	}

	*pos = endptr;
	mask = bit_alloc(bit_size(b));

	for (int i = start; i < bit_size(mask); i += stepval)
		bit_set(mask, i);

	bit_and(b, mask);
	bit_free(mask);

	return SLURM_SUCCESS;
}

extern cron_entry_t *cronspec_to_bitstring(char *pos)
{
	/* save initial string position for later */
	char *pos_init = pos;

	cron_entry_t *entry = new_cron_entry();

	if (*pos == '@') {
		pos++;
		if (!strncasecmp(pos, "yearly", 6) ||
		    !strncasecmp(pos, "annually", 8)) {
			/* "0 0 1 1 *" */
			bit_set(entry->minute, 0);
			bit_set(entry->hour, 0);
			bit_set(entry->day_of_month, 1);
			bit_set(entry->month, 1);
			bit_set_all(entry->day_of_week);
			entry->flags |= CRON_WILD_DOW;
			if (!strncasecmp(pos, "yearly", 6))
				pos += 6;
			else
				pos += 8;
		} else if (!strncasecmp(pos, "monthly", 7)) {
			/* "0 0 1 * *" */
			bit_set(entry->minute, 0);
			bit_set(entry->hour, 0);
			bit_set(entry->day_of_month, 1);
			bit_set_all(entry->month);
			entry->flags |= CRON_WILD_MONTH;
			bit_set_all(entry->day_of_week);
			entry->flags |= CRON_WILD_DOW;
			pos += 7;
		} else if (!strncasecmp(pos, "weekly", 6)) {
			/* "0 0 * * 0" */
			bit_set(entry->minute, 0);
			bit_set(entry->hour, 0);
			bit_set_all(entry->day_of_month);
			entry->flags |= CRON_WILD_DOM;
			bit_set_all(entry->month);
			entry->flags |= CRON_WILD_MONTH;
			bit_set(entry->day_of_week, 0);
			pos += 6;
		} else if (!strncasecmp(pos, "daily", 5) ||
			   !strncasecmp(pos, "midnight", 8)) {
			/* "0 0 * * *" */
			bit_set(entry->minute, 0);
			bit_set(entry->hour, 0);
			bit_set_all(entry->day_of_month);
			entry->flags |= CRON_WILD_DOM;
			bit_set_all(entry->month);
			entry->flags |= CRON_WILD_MONTH;
			bit_set_all(entry->day_of_week);
			entry->flags |= CRON_WILD_DOW;
			if (!strncasecmp(pos, "daily", 5))
				pos += 5;
			else
				pos += 8;
		} else if (!strncasecmp(pos, "hourly", 6)) {
			/* "0 * * * *" */
			bit_set(entry->minute, 0);
			bit_set_all(entry->hour);
			bit_set_all(entry->day_of_month);
			entry->flags |= CRON_WILD_DOM;
			bit_set_all(entry->month);
			entry->flags |= CRON_WILD_MONTH;
			bit_set_all(entry->day_of_week);
			entry->flags |= CRON_WILD_DOW;
			pos += 6;
		} else {
			error("invalid @ line");
			goto fail;
		}
		goto command;
	}

	/* minute */
	if (*pos == '\0' || *pos == '\n') {
		error("%s: unexpected end of line", __func__);
		goto fail;
	} else if (*pos == '*') {
		bit_set_all(entry->minute);
		pos++;
		if (*pos != '/')
			entry->flags |= CRON_WILD_MINUTE;
	} else {
		if (_parse_range(entry->minute, &pos))
			goto fail;
	}
	if (_handle_step(entry->minute, 0, &pos))
		goto fail;

	if (bit_test(entry->minute, 60))
		bit_set(entry->minute, 0);
	bit_clear(entry->minute, 60);

	if (*pos != ' ' && *pos != '\t')
		goto fail;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	/* hour */
	if (*pos == '\0' || *pos == '\n') {
		error("%s: unexpected end of line", __func__);
		goto fail;
	} else if (*pos == '*') {
		bit_set_all(entry->hour);
		pos++;
		if (*pos != '/')
			entry->flags |= CRON_WILD_HOUR;
	} else {
		if (_parse_range(entry->hour, &pos))
			goto fail;
	}
	if (_handle_step(entry->hour, 0, &pos))
		goto fail;

	if (bit_test(entry->hour, 24))
		bit_set(entry->hour, 0);
	bit_clear(entry->hour, 24);

	if (*pos != ' ' && *pos != '\t')
		goto fail;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	/* day of month */
	if (*pos == '\0' || *pos == '\n') {
		error("%s: unexpected end of line", __func__);
		goto fail;
	} else if (*pos == '*') {
		bit_set_all(entry->day_of_month);
		pos++;
		if (*pos != '/')
			entry->flags |= CRON_WILD_DOM;
	} else {
		if (_parse_range(entry->day_of_month, &pos))
			goto fail;
	}
	if (_handle_step(entry->day_of_month, 1, &pos))
		goto fail;

	if (*pos != ' ' && *pos != '\t')
		goto fail;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	/* month */
	if (*pos == '\0' || *pos == '\n') {
		error("%s: unexpected end of line", __func__);
		goto fail;
	} else if (*pos == '*') {
		bit_set_all(entry->month);
		pos++;
		if (*pos != '/')
			entry->flags |= CRON_WILD_MONTH;
	} else if (isalpha((int) *pos)) {
		if (!strncasecmp(pos, "jan", 3))
			bit_set(entry->month, 1);
		else if (!strncasecmp(pos, "feb", 3))
			bit_set(entry->month, 2);
		else if (!strncasecmp(pos, "mar", 3))
			bit_set(entry->month, 3);
		else if (!strncasecmp(pos, "apr", 3))
			bit_set(entry->month, 4);
		else if (!strncasecmp(pos, "may", 3))
			bit_set(entry->month, 5);
		else if (!strncasecmp(pos, "jun", 3))
			bit_set(entry->month, 6);
		else if (!strncasecmp(pos, "jul", 3))
			bit_set(entry->month, 7);
		else if (!strncasecmp(pos, "aug", 3))
			bit_set(entry->month, 8);
		else if (!strncasecmp(pos, "sep", 3))
			bit_set(entry->month, 9);
		else if (!strncasecmp(pos, "oct", 3))
			bit_set(entry->month, 10);
		else if (!strncasecmp(pos, "nov", 3))
			bit_set(entry->month, 11);
		else if (!strncasecmp(pos, "dec", 3))
			bit_set(entry->month, 12);
		else {
			error("bad month specification");
			goto fail;
		}
		pos += 3;
	} else {
		if (_parse_range(entry->month, &pos))
			goto fail;
	}
	if (_handle_step(entry->month, 1, &pos))
		goto fail;

	if (*pos != ' ' && *pos != '\t')
		goto fail;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	/* day of week */
	if (*pos == '\0' || *pos == '\n') {
		error("%s: unexpected end of line", __func__);
		goto fail;
	} else if (*pos == '*') {
		bit_set_all(entry->day_of_week);
		pos++;
		if (*pos != '/')
			entry->flags |= CRON_WILD_DOW;
	} else if (isalpha((int) *pos)) {
		if (!strncasecmp(pos, "mon", 3))
			bit_set(entry->day_of_week, 1);
		else if (!strncasecmp(pos, "tue", 3))
			bit_set(entry->day_of_week, 2);
		else if (!strncasecmp(pos, "wed", 3))
			bit_set(entry->day_of_week, 3);
		else if (!strncasecmp(pos, "thu", 3))
			bit_set(entry->day_of_week, 4);
		else if (!strncasecmp(pos, "fri", 3))
			bit_set(entry->day_of_week, 5);
		else if (!strncasecmp(pos, "sat", 3))
			bit_set(entry->day_of_week, 6);
		else if (!strncasecmp(pos, "sun", 3))
			bit_set(entry->day_of_week, 7);
		else {
			error("bad day specification");
			goto fail;
		}
		pos += 3;
	} else {
		if (_parse_range(entry->day_of_week, &pos))
			goto fail;
	}
	if (_handle_step(entry->day_of_week, 1, &pos))
		goto fail;

	if (bit_test(entry->day_of_week, 7))
		bit_set(entry->day_of_week, 0);
	bit_clear(entry->day_of_week, 7);

command:
	/* set initial cronspec */
	entry->cronspec = xstrndup(pos_init, pos - pos_init);
	if (*pos != ' ' && *pos != '\t')
		goto fail;
	while (*pos == ' ' || *pos == '\t')
		pos++;

	entry->command = xstrdup(pos);

	if (!valid_cron_entry(entry))
		goto fail;

	return entry;

fail:
	error("something is broken");
	free_cron_entry(entry);
	return NULL;
}

/*
 * Return an array pointing at each individual line in file. Function will
 * replace all newlines in file with NUL to avoid copying the entire buffer.
 *
 * Array is NULL terminated.
 */
extern char **convert_file_to_line_array(char *file, int *line_count)
{
	int lines = 1;
	char **line_array = xcalloc(3, sizeof(char *));
	char *ptr = file;

	line_array[0] = "\0";
	line_array[1] = ptr;
	while (*ptr != '\0') {
		if (*ptr == '\n') {
			*ptr = '\0';
			ptr++;
			lines++;
			if (*ptr == '\0')
				break;
			xrecalloc(line_array, lines + 2, sizeof(char *));
			line_array[lines] = ptr;
		} else
			ptr++;
	}

	*line_count = lines;
	return line_array;
}

extern char *next_line(const void *buf, int size, void **state)
{
	char *line;
	char *current, *ptr;

	if (!*state) /* initial state */
		*state = (void *)buf;

	if ((*state - buf) >= size) /* final state */
		return NULL;

	ptr = current = (char *) *state;
	while ((*ptr != '\n') && (ptr < ((char *) buf+size)))
		ptr++;

	line = xstrndup(current, ptr-current);

	/*
	 *  Advance state past newline
	 */
	*state = (ptr < ((char *) buf + size)) ? ptr+1 : ptr;
	return line;
}

/*
 * fixme parsing that can handle escapes, comments, quote marks
 * see get_argument() in sbatch/opt.c
 */
char *get_argument(char **pos)
{
	char *start = *pos, *end;

	while (*start == ' ' || *start == '\t')
		start++;

	if (*start == '\0')
		return NULL;

	end = start;
	while (*end != '\0' && *end != ' ' && *end != '\t')
		end++;

	*pos = end;
	if (**pos != '\0')
		(*pos)++;

	return xstrndup(start, end - start);
}


static int _set_options(int argc, char **argv)
{
	int opt_char;
	char *opt_string = NULL;
	struct option *optz = slurm_option_table_create(&opt, &opt_string);

	optind = 0;
	while ((opt_char = getopt_long(argc, argv, opt_string,
				       optz, NULL)) != -1) {
		if (slurm_process_option(&opt, opt_char, optarg, false, false))
		{
			slurm_option_table_destroy(optz);
			xfree(opt_string);
			return optind - 1;
		}
	}

	slurm_option_table_destroy(optz);
	xfree(opt_string);
	return optind;
}

extern int parse_scron_line(char *line, int lineno)
{
	char *option;
	int i;
	int argc = 1;
	char **argv = xmalloc(sizeof(char *));
	argv[0] = "scrontab";

	while ((option = get_argument(&line))) {
		argc++;
		xrecalloc(argv, argc, sizeof(char *));
		argv[argc - 1] = option;
	}

	if (argc > 1 && (i = _set_options(argc, argv)) < argc) {
		error("Invalid option found in #SCRON line: %s", argv[i]);
		for (i = 1; i < argc; i++)
			xfree(argv[i]);
		xfree(argv);
		return SLURM_ERROR;
	}

	for (i = 1; i < argc; i++)
		xfree(argv[i]);
	xfree(argv);

	return SLURM_SUCCESS;
}
