/*****************************************************************************\
 *  openapi.c - OpenAPI helpers
 *****************************************************************************
 *  Copyright (C) 2023 SchedMD LLC.
 *  Written by Nathan Rini <nate@schedmd.com>
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

#include "src/common/data.h"
#include "src/common/openapi.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#define MAGIC_FOREACH_PATH 0xaba1aaab

/*
 * Based on
 * https://github.com/OAI/OpenAPI-Specification/blob/main/versions/3.1.0.md#data-types
 */
static const struct {
	openapi_type_t type;
	openapi_type_format_t format;
	char *str_type;
	char *str_format;
	data_type_t data_type; /* compatible data type for this field */
} openapi_types[] = {
	{ OPENAPI_TYPE_INTEGER, OPENAPI_FORMAT_INT, "integer", NULL,
	  DATA_TYPE_INT_64 },
	{ OPENAPI_TYPE_INTEGER, OPENAPI_FORMAT_INT32, "integer", "int32",
	  DATA_TYPE_INT_64 },
	{ OPENAPI_TYPE_INTEGER, OPENAPI_FORMAT_INT64, "integer", "int64",
	  DATA_TYPE_INT_64 },
	{ OPENAPI_TYPE_NUMBER, OPENAPI_FORMAT_NUMBER, "number", NULL,
	  DATA_TYPE_FLOAT },
	{ OPENAPI_TYPE_NUMBER, OPENAPI_FORMAT_FLOAT, "number", "float",
	  DATA_TYPE_FLOAT },
	{ OPENAPI_TYPE_NUMBER, OPENAPI_FORMAT_DOUBLE, "number", "double",
	  DATA_TYPE_FLOAT },
	{ OPENAPI_TYPE_STRING, OPENAPI_FORMAT_STRING, "string", NULL,
	  DATA_TYPE_STRING },
	{ OPENAPI_TYPE_STRING, OPENAPI_FORMAT_PASSWORD, "string", "password",
	  DATA_TYPE_STRING },
	{ OPENAPI_TYPE_BOOL, OPENAPI_FORMAT_BOOL, "boolean", NULL,
	  DATA_TYPE_BOOL },
	{ OPENAPI_TYPE_OBJECT, OPENAPI_FORMAT_OBJECT, "object", NULL,
	  DATA_TYPE_DICT },
	{ OPENAPI_TYPE_ARRAY, OPENAPI_FORMAT_ARRAY, "array", NULL,
	  DATA_TYPE_LIST },
};

typedef struct {
	int magic; /* MAGIC_FOREACH_PATH */
	char *path;
	char *at;
} merge_path_strings_t;

extern const char *openapi_type_format_to_format_string(
	openapi_type_format_t format)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (openapi_types[i].format == format)
			return openapi_types[i].str_format;

	return NULL;
}

extern const char *openapi_type_format_to_type_string(
	openapi_type_format_t format)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (openapi_types[i].format == format)
			return openapi_types[i].str_type;

	return NULL;
}

extern const char *openapi_type_to_string(openapi_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (openapi_types[i].type == type)
			return openapi_types[i].str_type;

	return NULL;
}

extern openapi_type_t openapi_string_to_type(const char *str)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (!xstrcasecmp(openapi_types[i].str_type, str))
			return openapi_types[i].type;

	return OPENAPI_TYPE_INVALID;
}

extern openapi_type_format_t openapi_string_to_type_format(const char *str)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (!xstrcasecmp(openapi_types[i].str_format, str))
			return openapi_types[i].format;

	return OPENAPI_FORMAT_INVALID;
}

extern data_type_t openapi_type_format_to_data_type(
	openapi_type_format_t format)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (openapi_types[i].format == format)
			return openapi_types[i].data_type;

	return DATA_TYPE_NONE;
}

extern openapi_type_format_t openapi_data_type_to_type_format(data_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(openapi_types); i++)
		if (openapi_types[i].data_type == type)
			return openapi_types[i].format;

	return OPENAPI_FORMAT_INVALID;
}

static data_for_each_cmd_t _foreach_join_path_str(data_t *data, void *arg)
{
	merge_path_strings_t *args = arg;

	xassert(args->magic == MAGIC_FOREACH_PATH);

	if (data_convert_type(data, DATA_TYPE_STRING) != DATA_TYPE_STRING)
		fatal_abort("%s: path must be a string", __func__);

	/* path entry must not contain any of the seperators */
	xassert(!xstrstr(data_get_string(data), OPENAPI_PATH_SEP));
	xassert(!xstrstr(data_get_string(data), OPENAPI_PATH_REL));

	xstrfmtcatat(args->path, &args->at, "%s%s",
		     data_get_string(data), OPENAPI_PATH_SEP);

	return DATA_FOR_EACH_CONT;
}

extern char *openapi_fmt_rel_path_str(char **str_ptr, data_t *relative_path)
{
	merge_path_strings_t args = {
		.magic = MAGIC_FOREACH_PATH,
	};

	xassert(data_get_type(relative_path) == DATA_TYPE_LIST);
	if (data_get_type(relative_path) != DATA_TYPE_LIST)
		return NULL;

	/* path always starts with "#/" */
	xstrfmtcatat(args.path, &args.at, "%s%s",
		     OPENAPI_PATH_REL, OPENAPI_PATH_SEP);

	(void) data_list_for_each(relative_path, _foreach_join_path_str, &args);

	if (*str_ptr)
		xfree(*str_ptr);
	*str_ptr = args.path;

	return args.path;
}

extern data_t *openapi_fork_rel_path_list(data_t *relative_path, int index)
{
	data_t *ppath, *ppath_last;

	ppath = data_copy(NULL, relative_path);
	ppath_last = data_get_list_last(ppath);

	/* Use jq style array zero based array notation */
	data_set_string_fmt(ppath_last, "%s[%d]",
			    data_get_string(ppath_last), index);

	return ppath;
}

extern int openapi_append_rel_path(data_t *relative_path, const char *sub_path)
{
	if (data_get_type(relative_path) != DATA_TYPE_LIST)
		return ESLURM_DATA_EXPECTED_LIST;

	/* ignore empty sub paths */
	if (!sub_path || !sub_path[0])
		return SLURM_SUCCESS;

	/* If string starts with # then just ignore it */
	if (sub_path[0] == OPENAPI_PATH_REL[0])
		sub_path = &sub_path[1];

	return data_list_split_str(relative_path, sub_path, OPENAPI_PATH_SEP);
}

extern void free_openapi_resp_meta(void *obj)
{
	openapi_resp_meta_t *x = obj;

	if (!obj)
		return;

	xfree(x->command);
	xfree(x->plugin.type);
	xfree(x->plugin.name);
	xfree(x->plugin.data_parser);
	xfree(x->client.source);
	xfree(x->slurm.version.major);
	xfree(x->slurm.version.micro);
	xfree(x->slurm.version.minor);
	xfree(x->slurm.release);
	xfree(x);
}

extern void free_openapi_resp_error(void *obj)
{
	openapi_resp_error_t *x = obj;

	if (!obj)
		return;

	xfree(x->description);
	xfree(x->source);
	xfree(x);
}

extern void free_openapi_resp_warning(void *obj)
{
	openapi_resp_warning_t *x = obj;

	if (!obj)
		return;

	xfree(x->description);
	xfree(x->source);
	xfree(x);
}
