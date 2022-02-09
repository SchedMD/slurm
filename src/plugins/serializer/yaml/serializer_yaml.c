/*****************************************************************************\
 *  serializer_yaml.c - Serializer for YAML.
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

#include "slurm/slurm.h"
#include "src/common/slurm_xlator.h"

#include "src/common/log.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include <yaml.h>

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
const char plugin_name[] = "Serializer YAML plugin";
const char plugin_type[] = "serializer/yaml";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;
/*
 * YAML doesn't have an IANA registered mime type yet
 * so we are gonna match ruby on rails.
 */
const char *mime_types[] = {
	"application/x-yaml",
	"text/yaml",
	NULL
};

/* Default to about 1MB */
static const size_t yaml_buffer_size = 4096 * 256;

/* YAML parser doesn't give constants for the well defined scalars */
#define YAML_NULL "null"
#define YAML_TRUE "true"
#define YAML_FALSE "false"

typedef enum {
	EXPECTING_NONE = 0,
	EXPECTING_KEY,
	EXPECTING_VALUE
} yaml_parse_expect_t;

typedef enum {
	YAML_PARSE_NONE = 0,
	YAML_PARSE_DICT,
	YAML_PARSE_LIST,
} yaml_parse_mode_t;

typedef struct {
	data_type_t type;
	char *suffix;
} yaml_tag_types_t;

/* Map of suffix to local data_t type */
static const yaml_tag_types_t tags[] = {
	{ .type = DATA_TYPE_NULL, .suffix = "null" },
	{ .type = DATA_TYPE_LIST, .suffix = "seq" },
	{ .type = DATA_TYPE_DICT, .suffix = "map" },
	{ .type = DATA_TYPE_INT_64, .suffix = "int" },
	{ .type = DATA_TYPE_STRING, .suffix = "str" },
	{ .type = DATA_TYPE_FLOAT, .suffix = "float" },
	{ .type = DATA_TYPE_BOOL, .suffix = "bool" }
};

static int _data_to_yaml(const data_t *d, yaml_emitter_t *emitter);
static int _yaml_to_data(int depth, yaml_parser_t *parser, data_t *d,
			 yaml_parse_mode_t mode);

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

static char *_yaml_scalar_to_string(yaml_parser_t *parser, yaml_token_t *token)
{
	char *buffer = NULL;

	xassert(token->type == YAML_SCALAR_TOKEN);
	if (token->type != YAML_SCALAR_TOKEN)
		return buffer;

	buffer = xstrndup((char *)token->data.scalar.value,
			  token->data.scalar.length);

	debug5("%s: read scalar string: %s", __func__, buffer);

	return buffer;
}

static data_type_t _yaml_tag_to_type(yaml_token_t *token)
{
	data_type_t type = DATA_TYPE_NONE;

	for (int i = 0; i < (sizeof(tags) / sizeof(yaml_tag_types_t)); ++i) {
		const yaml_tag_types_t *tag = tags + i;

		if (!xstrcmp(tag->suffix,
			     (const char *)token->data.tag.suffix)) {
			type = tag->type;
			break;
		}
	}

	return type;
}

/*
 * Parse yaml scalar value and populate data
 * YAML didn't make reading the scalars easy and you're basically expected to
 * use regex and guess the types unless there is a tag
 * IN parser yaml parser
 * IN d data object to populate
 * IN token current scalar token to read
 * RET SLURM_SUCCESS or error
 */
static int _yaml_scalar_to_data(yaml_parser_t *parser, data_t *d,
				yaml_token_t *token, data_type_t type)
{
	int rc = SLURM_SUCCESS;
	char *str = _yaml_scalar_to_string(parser, token);
	data_type_t ctype;

	if (!str) {
		error("%s: unable to read token scalar for token (0x%"PRIXPTR")",
		      __func__, (uintptr_t) token);
		return SLURM_ERROR;
	}

	debug5("%s: read token scalar: %s", __func__, str);
	data_set_string(d, str);

	/*
	 * Use suggested type from token if known
	 * otherwise assume value is a string
	 */
	if (type != DATA_TYPE_NONE) {
		ctype = data_convert_type(d, type);
		if (ctype == type)
			debug5("%s: successfully converted %s to type %s",
			       __func__, str, data_type_to_string(type));
		else {
			error("%s: unable to convert %s to type %s",
			      __func__, str, data_type_to_string(type));
			rc = SLURM_ERROR;
		}
	}

	xfree(str);
	return rc;
}

static const char *_yaml_parse_mode_string(yaml_parse_mode_t mode)
{
	switch (mode) {
	case YAML_PARSE_DICT:
		return "YAML_PARSE_DICT";
	case YAML_PARSE_LIST:
		return "YAML_PARSE_LIST";
	case YAML_PARSE_NONE:
		return "YAML_PARSE_NONE";
	}
	return "UNKNOWN";
}

static int _yaml_parse_scalar(int depth, yaml_parser_t *parser, data_t *d,
			      yaml_parse_mode_t mode, char **key,
			      yaml_token_t *token, data_type_t type)

{
	debug5("%s: depth=%u parsing token mode=%s key=%s type=%s",
	       __func__, depth, _yaml_parse_mode_string(mode), *key,
	       data_type_to_string(type));

	switch (mode) {
	case YAML_PARSE_DICT:
		if (!*key) {
			*key = _yaml_scalar_to_string(parser, token);
			debug5("%s: data (0x%"PRIXPTR") depth:%d read key: %s",
			       __func__, (uintptr_t) d, depth, *key);
			return *key ? SLURM_SUCCESS : SLURM_ERROR;
		} else {
			int rc = _yaml_scalar_to_data(
				parser, data_key_set(d, *key), token, type);
			xfree(*key);
			return rc;
		}
	case YAML_PARSE_LIST:
		xassert(!*key);
		return _yaml_scalar_to_data(parser, data_list_append(d), token,
					    type);
	default:
		/* case YAML_PARSE_NONE: */
		fatal_abort("%s: should never get here", __func__);
	}
}

static int _yaml_parse_block(int depth, yaml_parser_t *parser, data_t *d,
			     yaml_parse_mode_t mode, char **key,
			     yaml_parse_mode_t child_mode)
{
	data_t *child = NULL;

	switch (mode) {
	case YAML_PARSE_DICT:
	{
		if (*key) {
			if (!(*key[0])) {
				error("%s: invalid dictionary key of zero length string",
				      __func__);
				return SLURM_ERROR;
			}
			child = data_key_set(d, *key);
			xfree(*key);
		} else {
			xassert(false);
			error("%s: starting yaml sequence inside of dictionary without key",
			      __func__);
			return SLURM_ERROR;
		}
		break;
	}
	case YAML_PARSE_LIST:
		child = data_list_append(d);
		break;
	case YAML_PARSE_NONE:
		/* parsing directly instead of child */
		child = d;
		break;
	}

	xassert(child);

	switch (child_mode) {
	case YAML_PARSE_DICT:
		data_set_dict(child);
		break;
	case YAML_PARSE_LIST:
		data_set_list(child);
		break;
	case YAML_PARSE_NONE:
		fatal_abort("%s: invalid child mode", __func__);
	}

	return _yaml_to_data(depth + 1, parser, child, child_mode);
}

/*
 * parse yaml stream into data_t recursively
 * IN parser yaml stream parser
 * IN d data object to populate
 * IN mode mode tells it if its trying to work with a list, dict or neither
 * RET SLURM_SUCCESS or errror
 */
static int _yaml_to_data(int depth, yaml_parser_t *parser, data_t *d,
			 yaml_parse_mode_t mode)
{
	int rc = SLURM_SUCCESS;
	yaml_token_t token;
	bool done = false;
	char *key = NULL;
	data_type_t type = DATA_TYPE_NONE;

	debug5("%s: parse yaml for data (0x%"PRIXPTR") depth=%d in mode: %s",
	       __func__, (uintptr_t) d, depth, _yaml_parse_mode_string(mode));

	/* sanity check nesting depth */
	if (depth > 124) {
		error("%s: YAML nested too deep (%d layers) for data (0x%"PRIXPTR")",
		      __func__, depth, (uintptr_t)d);
		return SLURM_ERROR;
	}

	while (rc == SLURM_SUCCESS) {
		if (!yaml_parser_scan(parser, &token)) {
			yaml_token_delete(&token);
			error("%s:%d %s: YAML parser error: %s",
			      __FILE__, __LINE__, __func__,
			      (char *) parser->problem);
			return SLURM_ERROR;
		}

		switch (token.type) {
		case YAML_STREAM_END_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_STREAM_END_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		case YAML_STREAM_START_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_STREAM_START_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_DOCUMENT_START_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_DOCUMENT_START_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_DOCUMENT_END_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_DOCUMENT_END_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		case YAML_BLOCK_MAPPING_START_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_BLOCK_MAPPING_START_TOKEN",
			       __func__, (uintptr_t)d, depth);
			if (type != DATA_TYPE_NONE && type != DATA_TYPE_DICT) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)));
				rc = SLURM_ERROR;
			} else {
				rc = _yaml_parse_block(depth, parser, d, mode,
						       &key, YAML_PARSE_DICT);
				type = DATA_TYPE_NONE;
			}
			break;
		case YAML_BLOCK_ENTRY_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_BLOCK_ENTRY_TOKEN",
			       __func__, (uintptr_t)d, depth);

			if (mode == YAML_PARSE_DICT) {
				xassert(data_get_type(d) == DATA_TYPE_DICT);

				if (!key) {
					error("%s: data (0x%"PRIXPTR") depth=%d unexpected block entry type:%s for data_type:%s without key",
					      __func__, (uintptr_t) d, depth,
					      data_type_to_string(type),
					      data_type_to_string(
						      data_get_type(d)));
					rc = SLURM_ERROR;
				} else {
					rc = _yaml_parse_block(depth, parser, d,
							       YAML_PARSE_DICT,
							       &key,
							       YAML_PARSE_LIST);
					type = DATA_TYPE_NONE;
				}
			} else if (mode == YAML_PARSE_LIST) {
				xassert(data_get_type(d) == DATA_TYPE_LIST);

				if (key) {
					error("%s: data (0x%"PRIXPTR") depth=%d unexpected block entry type:%s for data_type:%s without key",
					      __func__, (uintptr_t)d, depth,
					      data_type_to_string(type),
					      data_type_to_string(
						      data_get_type(d)));
					rc = SLURM_ERROR;
				} else
					debug5("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_BLOCK_ENTRY_TOKEN (already in list)",
					       __func__, (uintptr_t)d, depth);
			} else {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected block entry mode:%s type:%s for data_type:%s key:%s",
				      __func__, (uintptr_t)d, depth,
				      _yaml_parse_mode_string(mode),
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)),
				      key);
				rc = SLURM_ERROR;
			}
			break;
		case YAML_KEY_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_KEY_TOKEN",
			       __func__, (uintptr_t)d, depth);

			if (mode == YAML_PARSE_LIST) {
				debug5("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_KEY_TOKEN in list",
				       __func__, (uintptr_t)d, depth);
				/* libYAML most likely ended this block silently */
				done = true;
			} else if (type != DATA_TYPE_NONE &&
				   type != DATA_TYPE_DICT) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s mode:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)),
				      _yaml_parse_mode_string(mode));
				rc = SLURM_ERROR;
			} else {
				xassert(data_get_type(d) == DATA_TYPE_DICT);
				xassert(mode == YAML_PARSE_DICT);
				xassert(key == NULL);
			}
			break;
		case YAML_VALUE_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_VALUE_TOKEN",
			       __func__, (uintptr_t)d, depth);

			if (type != DATA_TYPE_NONE && type != DATA_TYPE_DICT) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)));
				rc = SLURM_ERROR;
			} else {
				xassert(key);
				xassert(data_get_type(d) == DATA_TYPE_DICT);
				xassert(mode == YAML_PARSE_DICT);
			}
			break;
		case YAML_BLOCK_SEQUENCE_START_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_BLOCK_SEQUENCE_START_TOKEN",
			       __func__, (uintptr_t)d, depth);

			if (type != DATA_TYPE_NONE && type != DATA_TYPE_LIST) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)));
				rc = SLURM_ERROR;
			} else {
				rc = _yaml_parse_block(depth, parser, d, mode,
						       &key, YAML_PARSE_LIST);
				type = DATA_TYPE_NONE;
			}
			break;
		case YAML_BLOCK_END_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_BLOCK_END_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		case YAML_SCALAR_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_SCALAR_TOKEN",
			       __func__, (uintptr_t)d, depth);
			rc = _yaml_parse_scalar(depth, parser, d, mode, &key,
						&token, type);
			type = DATA_TYPE_NONE;
			break;
		case YAML_TAG_DIRECTIVE_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_TAG_DIRECTIVE_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_TAG_TOKEN:
			type = _yaml_tag_to_type(&token);
			debug2("%s: data (0x%"PRIXPTR") depth=%d YAML_TAG_TOKEN handle=%s suffix=%s data_type=%s",
			       __func__, (uintptr_t)d, depth,
			       token.data.tag.handle, token.data.tag.suffix,
			       data_type_to_string(type));
			break;
		case YAML_ANCHOR_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_ANCHOR_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_ALIAS_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_ALIAS_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_VERSION_DIRECTIVE_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_VERSION_DIRECTIVE_TOKEN: YAML %d.%d",
			       __func__, (uintptr_t)d, depth,
			       token.data.version_directive.major,
			       token.data.version_directive.minor);
			break;
		case YAML_FLOW_SEQUENCE_START_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d YAML_FLOW_SEQUENCE_START_TOKEN",
			       __func__, (uintptr_t)d, depth);

			if (type != DATA_TYPE_NONE && type != DATA_TYPE_LIST) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)));
				rc = SLURM_ERROR;
			} else {
				rc = _yaml_parse_block(depth, parser, d, mode,
						       &key, YAML_PARSE_LIST);
				type = DATA_TYPE_NONE;
			}
			break;
		case YAML_FLOW_SEQUENCE_END_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d YAML_FLOW_SEQUENCE_END_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		case YAML_FLOW_MAPPING_START_TOKEN:
			debug5("%s: data (0x%"PRIXPTR") depth=%d YAML_FLOW_MAPPING_START_TOKEN",
			       __func__, (uintptr_t)d, depth);
			if (type != DATA_TYPE_NONE && type != DATA_TYPE_DICT) {
				error("%s: data (0x%"PRIXPTR") depth=%d unexpected suffix type:%s for data_type:%s",
				      __func__, (uintptr_t)d, depth,
				      data_type_to_string(type),
				      data_type_to_string(data_get_type(d)));
				rc = SLURM_ERROR;
			} else {
				rc = _yaml_parse_block(depth, parser, d, mode,
						       &key, YAML_PARSE_DICT);
				type = DATA_TYPE_NONE;
			}
			break;
		case YAML_FLOW_MAPPING_END_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d YAML_FLOW_MAPPING_END_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		case YAML_FLOW_ENTRY_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d ignoring YAML_FLOW_ENTRY_TOKEN",
			       __func__, (uintptr_t)d, depth);
			break;
		case YAML_NO_TOKEN:
			debug2("%s: data (0x%"PRIXPTR") depth=%d YAML_NO_TOKEN",
			       __func__, (uintptr_t)d, depth);
			done = true;
			break;
		default:
			debug2("%s: data (0x%"PRIXPTR") depth=%d unexpected YAML token: %d",
			       __func__, (uintptr_t)d, depth, token.type);
			rc = SLURM_ERROR;
			xassert(false);
		}

		yaml_token_delete(&token);

		if (done) {
			debug5("%s: done parsing yaml for data (0x%"PRIXPTR")",
			       __func__, (uintptr_t)d);
			break;
		}
	}

	return rc;
}

static int _parse_yaml(const char *buffer, yaml_parser_t *parser, data_t *data)
{
	xassert(data);
	if (!data)
		return SLURM_ERROR;

	if (!yaml_parser_initialize(parser)) {
		error("%s:%d %s: YAML parser error: %s",
		      __FILE__, __LINE__, __func__, (char *)parser->problem);
		return SLURM_ERROR;
	}

	yaml_parser_set_input_string(parser, (yaml_char_t *)buffer,
				     strlen(buffer));

	return _yaml_to_data(0, parser, data, YAML_PARSE_NONE);
}

/*
 * YAML emitter will set problem in the struct on error
 * dump what caused the error and dump the error
 *
 * Jumps to yaml_fail when done.
 */
#define _yaml_emitter_error                                                   \
	do {                                                                  \
		error("%s:%d %s: YAML emitter error: %s", __FILE__, __LINE__, \
		      __func__, (char *)emitter->problem);                    \
		goto yaml_fail;                                               \
	} while (false)

static int _emit_string(const char *str, yaml_emitter_t *emitter)
{
	yaml_event_t event;

	if (!str) {
		/* NULL string handed to emitter -> emit NULL instead */
		if (!yaml_scalar_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_NULL_TAG,
			    (yaml_char_t *)YAML_NULL, strlen(YAML_NULL), 0, 0,
			    YAML_ANY_SCALAR_STYLE))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return SLURM_SUCCESS;
	}

	if (!yaml_scalar_event_initialize(&event, NULL,
					  (yaml_char_t *)YAML_STR_TAG,
					  (yaml_char_t *)str, strlen(str), 0, 0,
					  YAML_ANY_SCALAR_STYLE))
		_yaml_emitter_error;

	if (!yaml_emitter_emit(emitter, &event))
		_yaml_emitter_error;

	return SLURM_SUCCESS;

yaml_fail:
	return SLURM_ERROR;
}

static data_for_each_cmd_t _convert_dict_yaml(const char *key,
					      const data_t *data,
					      void *arg)
{
	yaml_emitter_t *emitter = arg;

	/*
	 * Emitter doesn't have a key field
	 * it just sends it as a scalar before
	 * the value is sent
	 */
	if (_emit_string(key, emitter))
		return DATA_FOR_EACH_FAIL;

	if (_data_to_yaml(data, emitter))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static data_for_each_cmd_t _convert_list_yaml(const data_t *data, void *arg)
{
	yaml_emitter_t *emitter = arg;

	if (_data_to_yaml(data, emitter))
		return DATA_FOR_EACH_FAIL;

	return DATA_FOR_EACH_CONT;
}

static int _data_to_yaml(const data_t *d, yaml_emitter_t *emitter)
{
	yaml_event_t event;

	if (!d)
		return SLURM_ERROR;

	switch (data_get_type(d)) {
	case DATA_TYPE_NULL:
		if (!yaml_scalar_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_NULL_TAG,
			    (yaml_char_t *)YAML_NULL, strlen(YAML_NULL), 0, 0,
			    YAML_ANY_SCALAR_STYLE))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return SLURM_SUCCESS;
	case DATA_TYPE_BOOL:
		if (data_get_bool(d)) {
			if (!yaml_scalar_event_initialize(
				    &event, NULL, (yaml_char_t *)YAML_BOOL_TAG,
				    (yaml_char_t *)YAML_TRUE, strlen(YAML_TRUE),
				    0, 0, YAML_ANY_SCALAR_STYLE))
				_yaml_emitter_error;
		} else {
			if (!yaml_scalar_event_initialize(
				    &event, NULL, (yaml_char_t *)YAML_BOOL_TAG,
				    (yaml_char_t *)YAML_FALSE,
				    strlen(YAML_FALSE), 0, 0,
				    YAML_ANY_SCALAR_STYLE))
				_yaml_emitter_error;
		}

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return SLURM_SUCCESS;
	case DATA_TYPE_FLOAT:
	{
		char *buffer = xstrdup_printf("%lf", data_get_float(d));
		if (buffer == NULL) {
			error("%s: unable to print double to string: %m",
			      __func__);
			return SLURM_ERROR;
		}

		if (!yaml_scalar_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_FLOAT_TAG,
			    (yaml_char_t *)buffer, strlen(buffer), 0, 0,
			    YAML_ANY_SCALAR_STYLE)) {
			xfree(buffer);
			_yaml_emitter_error;
		}

		xfree(buffer);

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return SLURM_SUCCESS;
	}
	case DATA_TYPE_INT_64:
	{
		char *buffer = xstrdup_printf("%"PRId64, data_get_int(d));
		if (buffer == NULL) {
			error("%s: unable to print int to string: %m",
			      __func__);
			return SLURM_ERROR;
		}

		if (!yaml_scalar_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_INT_TAG,
			    (yaml_char_t *)buffer, strlen(buffer), 0, 0,
			    YAML_ANY_SCALAR_STYLE)) {
			xfree(buffer);
			_yaml_emitter_error;
		}

		xfree(buffer);

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return SLURM_SUCCESS;
	}
	case DATA_TYPE_DICT:
	{
		int count;

		if (!yaml_mapping_start_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_MAP_TAG, 0,
			    YAML_ANY_MAPPING_STYLE))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		count = data_dict_for_each_const(d, _convert_dict_yaml,
						 emitter);
		xassert(count >= 0);

		if (!yaml_mapping_end_event_initialize(&event))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return count >= 0 ? SLURM_SUCCESS : SLURM_ERROR;
	}
	case DATA_TYPE_LIST:
	{
		int count;

		if (!yaml_sequence_start_event_initialize(
			    &event, NULL, (yaml_char_t *)YAML_SEQ_TAG, 0,
			    YAML_ANY_SEQUENCE_STYLE))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		count = data_list_for_each_const(d, _convert_list_yaml,
						 emitter);
		xassert(count >= 0);

		if (!yaml_sequence_end_event_initialize(&event))
			_yaml_emitter_error;

		if (!yaml_emitter_emit(emitter, &event))
			_yaml_emitter_error;

		return count >= 0 ? SLURM_SUCCESS : SLURM_ERROR;
	}
	case DATA_TYPE_STRING:
		return _emit_string(data_get_string_const(d), emitter);
	default:
		xassert(false);
	};

yaml_fail:
	return SLURM_ERROR;
}

static int _yaml_write_handler(void *data, unsigned char *buffer, size_t size)
{
	buf_t *buf = data;
	xassert(buf->magic == BUF_MAGIC);

	/*
	 * If the remaining buffer size equals the required argument size, we
	 * still want to grow to allocate space for an extra '\0'. That's why in
	 * this case we compare with '<=' instead of '<'.
	 */
	if (remaining_buf(buf) <= size) {
		if ((size + size_buf(buf)) >= MAX_BUF_SIZE) {
			error("%s: attempting to write too large of YAML output",
			      __func__);
			return 0;
		}

		grow_buf(buf, size);
	}

	memcpy(buf->head + buf->processed, buffer, size);

	buf->processed += size;
	/*
	 * buf->processed points to one position after the memcpy'd payload,
	 * so we can set the '\0' there. This position will be overridden by
	 * the first character of the next chunk except for the last handler
	 * call, effectively resulting in a NULL-terminated buffer.
	 */
	buf->head[buf->processed] = '\0';

	return 1;
}

static int _dump_yaml(const data_t *data, yaml_emitter_t *emitter, buf_t *buf)
{
	yaml_event_t event;

	//TODO: only version 1.1 is currently supported by libyaml
	yaml_version_directive_t ver = {
		.major = 1,
		.minor = 1,
	};

	if (!yaml_emitter_initialize(emitter))
		_yaml_emitter_error;

	yaml_emitter_set_output(emitter, _yaml_write_handler, buf);

	//TODO defaulted to UTF8 but maybe this should be a flag?
	if (!yaml_stream_start_event_initialize(&event, YAML_UTF8_ENCODING))
		_yaml_emitter_error;

	if (!yaml_emitter_emit(emitter, &event))
		_yaml_emitter_error;

	if (!yaml_document_start_event_initialize(&event, &ver, NULL, NULL, 0))
		_yaml_emitter_error;

	if (!yaml_emitter_emit(emitter, &event))
		_yaml_emitter_error;

	if (_data_to_yaml(data, emitter))
		goto yaml_fail;

	if (!yaml_document_end_event_initialize(&event, 0))
		_yaml_emitter_error;

	if (!yaml_emitter_emit(emitter, &event))
		_yaml_emitter_error;

	if (!yaml_stream_end_event_initialize(&event))
		_yaml_emitter_error;

	if (!yaml_emitter_emit(emitter, &event))
		_yaml_emitter_error;

	return SLURM_SUCCESS;

yaml_fail:
	return SLURM_ERROR;
}

#undef _yaml_emitter_error

extern int serializer_p_serialize(char **dest, const data_t *data,
				  data_serializer_flags_t flags)
{
	yaml_emitter_t emitter;
	buf_t *buf = init_buf(yaml_buffer_size);

	if (_dump_yaml(data, &emitter, buf)) {
		error("%s: dump yaml failed", __func__);

		FREE_NULL_BUFFER(buf);
		return ESLURM_DATA_CONV_FAILED;
	}

	yaml_emitter_delete(&emitter);

	*dest = xfer_buf_data(buf);
	buf = NULL;

	if (*dest)
		return SLURM_SUCCESS;
	else
		return SLURM_ERROR;
}

extern int serializer_p_deserialize(data_t **dest, const char *src,
				    size_t len)
{
	data_t *data = data_new();
	yaml_parser_t parser;

	xassert(len < strlen(src));

	if (_parse_yaml(src, &parser, data)) {
		FREE_NULL_DATA(data);
		return ESLURM_DATA_CONV_FAILED;
	}

	yaml_parser_delete(&parser);

	*dest = data;
	return SLURM_SUCCESS;
}
