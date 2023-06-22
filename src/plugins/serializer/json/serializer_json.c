/*****************************************************************************\
 *  serializer_json.c - Serializer for JSON.
 *****************************************************************************
 *  Copyright (C) 2021 SchedMD LLC
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

#include "config.h"

#if HAVE_JSON_C_INC
#include <json-c/json.h>
#else
#include <json/json.h>
#endif

#include "slurm/slurm.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/interfaces/serializer.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - A string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - A string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  Slurm uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "auth" for Slurm authentication) and <method> is a
 * description of how this plugin satisfies that application.  Slurm will
 * only load authentication plugins if the plugin_type string has a prefix
 * of "auth/".
 *
 * plugin_version - an unsigned 32-bit integer containing the Slurm version
 * (major.minor.micro combined into a single number).
 */
const char plugin_name[] = "Serializer JSON plugin";
const char plugin_type[] = "serializer/json";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
const char *mime_types[] = {
	"application/json",
	"application/jsonrequest",
	NULL
};

static json_object *_data_to_json(const data_t *d);

extern int serializer_p_init(void)
{
	log_flag(DATA, "loaded");

	return SLURM_SUCCESS;
}

extern int serializer_p_fini(void)
{
	log_flag(DATA, "unloaded");

	return SLURM_SUCCESS;
}


static json_object *_try_parse(const char *src, size_t stringlen,
			       struct json_tokener *tok)
{
	json_object *jobj = json_tokener_parse_ex(tok, src, stringlen);

	if (jobj == NULL) {
		enum json_tokener_error jerr = json_tokener_get_error(tok);
		error("%s: JSON parsing error %zu bytes: %s",
		      __func__, stringlen, json_tokener_error_desc(jerr));
		return NULL;
	}
	if (tok->char_offset < stringlen)
		log_flag(DATA, "%s: Extra %zu characters after JSON string detected",
		     __func__, (stringlen - tok->char_offset));

	return jobj;
}

static data_t *_json_to_data(json_object *jobj, data_t *d)
{
	size_t arraylen = 0;

	if (!d)
		d = data_new();

	switch (json_object_get_type(jobj)) {
	case json_type_null:
		data_set_null(d);
		break;
	case json_type_boolean:
		data_set_bool(d, json_object_get_boolean(jobj));
		break;
	case json_type_double:
		data_set_float(d, json_object_get_double(jobj));
		break;
	case json_type_int:
		data_set_int(d, json_object_get_int64(jobj));
		break;
	case json_type_object:
		data_set_dict(d);
		/* warning: json_object_object_foreach is an evil macro */
		json_object_object_foreach(jobj, key, val) {
			_json_to_data(val, data_key_set(d, key));
		}
		break;
	case json_type_array:
		arraylen = json_object_array_length(jobj);
		data_set_list(d);
		for (size_t i = 0; i < arraylen; i++)
			_json_to_data(json_object_array_get_idx(jobj, i),
				      data_list_append(d));
		break;
	case json_type_string:
		data_set_string(d, json_object_get_string(jobj));
		break;
	default:
		fatal_abort("%s: unknown JSON type", __func__);
	};

	return d;
}

static data_for_each_cmd_t _convert_dict_json(const char *key,
					      const data_t *data,
					      void *arg)
{
	json_object *jobj = arg;
	json_object *jobject = _data_to_json(data);

	json_object_object_add(jobj, key, jobject);
	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _convert_list_json(const data_t *data, void *arg)
{
	json_object *jobj = arg;
	json_object *jarray = _data_to_json(data);

	json_object_array_add(jobj, jarray);
	return DATA_FOR_EACH_CONT;
}

static json_object *_data_to_json(const data_t *d)
{
	if (!d)
		return NULL;

	switch (data_get_type(d)) {
	case DATA_TYPE_NULL:
		return NULL;
		break;
	case DATA_TYPE_BOOL:
		return json_object_new_boolean(data_get_bool(d));
		break;
	case DATA_TYPE_FLOAT:
		return json_object_new_double(data_get_float(d));
		break;
	case DATA_TYPE_INT_64:
		return json_object_new_int64(data_get_int(d));
		break;
	case DATA_TYPE_DICT:
	{
		json_object *jobj = json_object_new_object();
		if (data_dict_for_each_const(d, _convert_dict_json, jobj) < 0)
			error("%s: unexpected error calling _convert_dict_json()",
			      __func__);
		return jobj;
	}
	case DATA_TYPE_LIST:
	{
		json_object *jobj = json_object_new_array();
		if (data_list_for_each_const(d, _convert_list_json, jobj) < 0)
			error("%s: unexpected error calling _convert_list_json()",
			      __func__);
		return jobj;
	}
	case DATA_TYPE_STRING:
	{
		const char *str = data_get_string_const(d);
		if (str)
			return json_object_new_string(str);
		else
			return json_object_new_string("");
		break;
	}
	default:
		fatal_abort("%s: unknown type", __func__);
	};
}

extern int serialize_p_data_to_string(char **dest, size_t *length,
				      const data_t *src,
				      serializer_flags_t flags)
{
	struct json_object *jobj = _data_to_json(src);
	int jflags = 0;

	/* can't be pretty and compact at the same time! */
	xassert((flags & (SER_FLAGS_PRETTY | SER_FLAGS_COMPACT)) !=
		(SER_FLAGS_PRETTY | SER_FLAGS_COMPACT));

	switch (flags) {
	case SER_FLAGS_PRETTY:
		jflags = JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY;
		break;
	case SER_FLAGS_COMPACT: /* fallthrough */
	default:
		jflags = JSON_C_TO_STRING_PLAIN;
	}

	/* string will die with jobj */
	*dest = xstrdup(json_object_to_json_string_ext(jobj, jflags));
	if (length) {
		/* add 1 for \0 */
		*length = strlen(*dest) + 1;
	}

	/* put is equiv to free() */
	json_object_put(jobj);

	return SLURM_SUCCESS;
}

extern int serialize_p_string_to_data(data_t **dest, const char *src,
				      size_t length)
{
	json_object *jobj = NULL;
	data_t *data = NULL;
	struct json_tokener *tok = json_tokener_new();

	if (!tok)
		return ENOMEM;

	if (!src)
		return ESLURM_DATA_PTR_NULL;

	/* json-c has hard limit of 32 bits */
	if (length >= INT32_MAX) {
		error("%s: unable to parse JSON: too large",
		      __func__);
		return ESLURM_DATA_TOO_LARGE;
	}

	jobj = _try_parse(src, length, tok);
	if (jobj) {
		data = _json_to_data(jobj, NULL);
		json_object_put(jobj);
	}

	json_tokener_free(tok);

	*dest = data;
	return SLURM_SUCCESS;
}
