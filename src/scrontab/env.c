/*****************************************************************************\
 *  parse.c
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC.
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

#include "src/common/xstring.h"

/*
 * Attempt to parse a line assuming it's an "environment setting".
 * Work off the syntax as outlined in crontab(5).
 * Return true if this parsed successfully, false if it did not.
 */
extern bool load_env(char *line, char **key, char **value)
{
	int key_start = 0, key_end, equal, value_start, value_end;

	/* skip leading whitespace */
	while (line[key_start] && isblank((int) line[key_start])) {
		if (!line[key_start + 1])
			return false;
		key_start++;
	}

	/* read characters until next whitespace or = */
	key_end = key_start;
	while (line[key_end] && isalnum((int) line[key_end])) {
		if (!line[key_end + 1])
			return false;
		if (line[key_end] == '=')
			break;
		key_end++;
	}

	/* skip more whitespace until we find = */
	equal = key_end;
	while (line[equal] && isblank((int) line[equal])) {
		if (!line[equal + 1])
			return false;
		equal++;
	}

	if (line[equal] != '=')
		return false;

	/* skip whitespace after = */
	value_start = equal + 1;
	while (line[value_start] && isblank((int) line[value_start])) {
		if (!line[value_start + 1])
			return false;
		value_start++;
	}

	/* if quote mark, everything until the next quote mark is the value */
	if (line[value_start] == '\'' || line[value_start] == '"') {
		int quote = value_start, end;

		value_start++;

		/* ensure trailing quote is found */
		value_end = value_start;
		while (line[value_end]) {
			if (line[value_end + 1] == line[quote])
				break;
			value_end++;
		}

		end = value_end + 1;
		if (!line[end] || (line[end] != line[quote])) {
			error("not match");
			return false;
		}
		end++;

		/* anything after the matched quote needs to be whitespace */
		while (line[end]) {
			if (!isblank((int) line[end]))
				return false;
			end++;
		}
	} else {
		value_end = value_start;
		while (line[value_end])
			value_end++;
	}

	*key = xstrndup(line + key_start, key_end - key_start);
	*value = xstrndup(line + value_start, value_end - value_start);

	return true;
}
