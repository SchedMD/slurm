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

#include <yaml.h>

#include "slurm/slurm.h"
#include "src/common/slurm_xlator.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/pack.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
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
const char plugin_name[] = "Serializer YAML plugin";
const char plugin_type[] = "serializer/yaml";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

#define YAML_MAX_DEPTH 64
#define LOG_LENGTH 16

/*
 * YAML doesn't have an IANA registered mime type yet
 * so we are gonna match ruby on rails.
 */
const char *mime_types[] = {
	"application/x-yaml",
	"text/yaml",
	NULL
};

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

/* Map of suffix to local data_t type */
static const struct {
	data_type_t type;
	char *suffix;
	char *tag;
} tags[] = {
	{
		.type = DATA_TYPE_NULL,
		.tag = "tag:yaml.org,2002:null",
		.suffix = "null"
	},
	{
		.type = DATA_TYPE_LIST,
		.tag = "tag:yaml.org,2002:seq",
		.suffix = "seq",
	},
	{
		.type = DATA_TYPE_DICT,
		.tag = "tag:yaml.org,2002:map",
		.suffix = "map",
	},
	{
		.type = DATA_TYPE_INT_64,
		.tag = "tag:yaml.org,2002:int",
		.suffix = "int",
	},
	{
		.type = DATA_TYPE_STRING,
		.tag = "tag:yaml.org,2002:str",
		.suffix = "str",
	},
	{
		.type = DATA_TYPE_FLOAT,
		.tag = "tag:yaml.org,2002:float",
		.suffix = "float",
	},
	{
		.type = DATA_TYPE_BOOL,
		.tag = "tag:yaml.org,2002:bool",
		.suffix = "bool",
	}
};

typedef enum {
	PARSE_INVALID = 0,
	/* inital state before parsing started */
	PARSE_NOT_STARTED,

	/* parsing states */
	PARSE_CONTINUE,
	PARSE_POP,

	/* completion states */
	PARSE_DONE,
	PARSE_FAIL,

	PARSE_INVALID_MAX
} parse_state_t;

#define T(X) { X, XSTRINGIFY(X) }
static const struct {
	yaml_event_type_t type;
	const char *string;
} event_types[] = {
	T(YAML_NO_EVENT),
	T(YAML_DOCUMENT_START_EVENT),
	T(YAML_STREAM_START_EVENT),
	T(YAML_DOCUMENT_END_EVENT),
	T(YAML_STREAM_END_EVENT),
	T(YAML_ALIAS_EVENT),
	T(YAML_SCALAR_EVENT),
	T(YAML_SEQUENCE_START_EVENT),
	T(YAML_SEQUENCE_END_EVENT),
	T(YAML_MAPPING_START_EVENT),
	T(YAML_MAPPING_END_EVENT),
};
#undef T

static int _data_to_yaml(const data_t *d, yaml_emitter_t *emitter);
static parse_state_t _yaml_to_data(int depth, yaml_parser_t *parser,
				   data_t *dst, int *rc);
static parse_state_t _on_parse_event(int depth, yaml_parser_t *parser,
				     yaml_event_t *event, data_t *dst, int *rc,
				     parse_state_t state);

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

static const char *_yaml_event_type_string(yaml_event_type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(event_types); i++)
		if (event_types[i].type == type)
			return event_types[i].string;

	fatal_abort("invalid type");
}

static data_type_t _yaml_tag_to_type(yaml_event_t *event, const char *source)
{
	const char *tag = (const char *) event->data.scalar.tag;

	if (!tag || !tag[0])
		return DATA_TYPE_NONE;

	log_flag_hex(DATA, tag, strlen(tag), "%s: scalar tag", source);

	for (int i = 0; i < ARRAY_SIZE(tags); ++i)
		if (!xstrcmp(tags[i].tag, tag))
			return tags[i].type;

	return DATA_TYPE_NONE;
}

static parse_state_t _on_parse_scalar(int depth, yaml_parser_t *parser,
				      yaml_event_t *event, data_t *dst, int *rc,
				      parse_state_t state)
{
	data_type_t tag;
	const char *value = (const char *) event->data.scalar.value;

	if (data_get_type(dst) == DATA_TYPE_DICT) {
		data_t *child = data_key_set(dst, value);
		log_flag(DATA, "PUSH %pD[%s]=%pD", dst, value, child);
		return _yaml_to_data((depth + 1), parser, child, rc);
	}

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	tag = _yaml_tag_to_type(event, __func__);
	data_set_string(dst, value);

	if ((tag != DATA_TYPE_NONE) &&
	    (data_convert_type(dst, tag) != tag)) {
		*rc = ESLURM_DATA_CONV_FAILED;
		return PARSE_FAIL;
	}

	return PARSE_POP;
}

static parse_state_t _on_parse_event(int depth, yaml_parser_t *parser,
				     yaml_event_t *event, data_t *dst, int *rc,
				     parse_state_t state)
{
	if ((data_get_type(dst) == DATA_TYPE_LIST) &&
	    ((event->type == YAML_SCALAR_EVENT) ||
	     (event->type == YAML_SEQUENCE_START_EVENT) ||
	     (event->type == YAML_MAPPING_START_EVENT))) {
		data_t *child = data_list_append(dst);
		log_flag(DATA, "PUSH %pD[]=%pD", dst, child);
		state = _on_parse_event((depth + 1), parser, event, child, rc,
					 state);
		return (state == PARSE_POP) ? PARSE_CONTINUE : state;
	}

	switch (event->type) {
	case YAML_NO_EVENT:
		xassert(state == PARSE_CONTINUE);
		return PARSE_DONE;

	case YAML_DOCUMENT_START_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		xassert(state == PARSE_CONTINUE);
		return PARSE_CONTINUE;
	case YAML_STREAM_START_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		xassert(state == PARSE_NOT_STARTED);
		return PARSE_CONTINUE;
	case YAML_DOCUMENT_END_EVENT:
		xassert(state == PARSE_CONTINUE);
		return PARSE_CONTINUE;
	case YAML_STREAM_END_EVENT:
		xassert(state == PARSE_CONTINUE);
		return PARSE_DONE;

	case YAML_ALIAS_EVENT:
		error("%s: YAML parser does not support aliases", __func__);
		*rc = ESLURM_NOT_SUPPORTED;
		return PARSE_FAIL;

	case YAML_SCALAR_EVENT:
		return _on_parse_scalar(depth, parser, event, dst, rc, state);

	case YAML_SEQUENCE_START_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		data_set_list(dst);
		state = _yaml_to_data((depth + 1), parser, dst, rc);
		return (state == PARSE_CONTINUE) ? PARSE_POP : state;
	case YAML_SEQUENCE_END_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_LIST);
		return PARSE_POP;

	case YAML_MAPPING_START_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_NULL);
		data_set_dict(dst);
		state = _yaml_to_data((depth + 1), parser, dst, rc);
		return (state == PARSE_CONTINUE) ? PARSE_POP : state;
	case YAML_MAPPING_END_EVENT:
		xassert(data_get_type(dst) == DATA_TYPE_DICT);
		return PARSE_POP;
	}

	fatal_abort("should never execute");
}

/*
 * parse yaml stream into data_t recursively
 * IN depth current parsing depth
 * IN parser yaml stream parser
 * IN dst data object to populate
 * IN rc ptr to return code
 * RET parsing state
 */
static parse_state_t _yaml_to_data(int depth, yaml_parser_t *parser,
				   data_t *dst, int *rc)
{
	parse_state_t state = PARSE_NOT_STARTED;

	/* sanity check nesting depth */
	if (depth > YAML_MAX_DEPTH) {
		error("%s: YAML nested too deep (%d layers) at %pD",
		      __func__, depth, dst);
		return ESLURM_DATA_PARSING_DEPTH;
	}

	while (state < PARSE_DONE) {
		yaml_event_t event;

		xassert(state > PARSE_INVALID);
		xassert(state < PARSE_INVALID_MAX);

		if (!yaml_parser_parse(parser, &event)) {
			yaml_event_delete(&event);
			error("%s: YAML parser error: %s",
			      __func__, (char *) parser->problem);
			*rc = ESLURM_DATA_PARSER_INVALID_STATE;
			return PARSE_FAIL;
		}

		log_flag_hex_range(DATA, parser->buffer.start,
				   (parser->buffer.last - parser->buffer.start),
				   event.start_mark.index,
				   (event.start_mark.index + LOG_LENGTH),
				   "%s: %pD{%d} -> %s", __func__, dst, depth,
				   _yaml_event_type_string(event.type));

		state = _on_parse_event(depth, parser, &event, dst, rc, state);

		if (state == PARSE_POP) {
			log_flag(DATA, "%pD{%d} -> POP", dst, depth);
			state = PARSE_CONTINUE;
			break;
		}
	}

	return PARSE_CONTINUE;
}

static int _parse_yaml(const char *buffer, yaml_parser_t *parser, data_t *data)
{
	const unsigned char *buf = (const unsigned char *) buffer;
	int rc = SLURM_SUCCESS;

	xassert(data);
	if (!data)
		return SLURM_ERROR;

	if (!yaml_parser_initialize(parser)) {
		error("%s:%d %s: YAML parser error: %s",
		      __FILE__, __LINE__, __func__, (char *)parser->problem);
		return SLURM_ERROR;
	}

	yaml_parser_set_input_string(parser, buf, strlen(buffer));

	(void) _yaml_to_data(0, parser, data, &rc);

	return rc;
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
	int rc;
	buf_t *buf = data;
	xassert(buf->magic == BUF_MAGIC);

	/*
	 * If the remaining buffer size equals the required argument size, we
	 * still want to grow to allocate space for an extra '\0'. That's why in
	 * this case we compare with '<=' instead of '<'.
	 */
	if ((rc = try_grow_buf_remaining(buf, (size + 1))))
		return rc;

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

static int _dump_yaml(const data_t *data, yaml_emitter_t *emitter, buf_t *buf,
		      serializer_flags_t flags)
{
	yaml_event_t event;

	//TODO: only version 1.1 is currently supported by libyaml
	yaml_version_directive_t ver = {
		.major = 1,
		.minor = 1,
	};

	if (!yaml_emitter_initialize(emitter))
		_yaml_emitter_error;

	if (flags == SER_FLAGS_COMPACT) {
		yaml_emitter_set_indent(emitter, 0);
		yaml_emitter_set_width(emitter, -1);
		yaml_emitter_set_break(emitter, YAML_ANY_BREAK);
	}

	yaml_emitter_set_output(emitter, _yaml_write_handler, buf);

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

extern int serialize_p_data_to_string(char **dest, size_t *length,
				      const data_t *src,
				      serializer_flags_t flags)
{
	yaml_emitter_t emitter;
	buf_t *buf = init_buf(0);

	if (_dump_yaml(src, &emitter, buf, flags)) {
		error("%s: dump yaml failed", __func__);

		FREE_NULL_BUFFER(buf);
		return ESLURM_DATA_CONV_FAILED;
	}

	yaml_emitter_delete(&emitter);

	if (length)
		*length = get_buf_offset(buf);
	*dest = xfer_buf_data(buf);
	buf = NULL;

	if (*dest)
		return SLURM_SUCCESS;
	else
		return SLURM_ERROR;
}

extern int serialize_p_string_to_data(data_t **dest, const char *src,
				      size_t length)
{
	data_t *data;
	yaml_parser_t parser;

	/* string must be NULL terminated */
	if (!length || (src[length] && (strnlen(src, length) >= length)))
		return EINVAL;

	data = data_new();

	if (_parse_yaml(src, &parser, data)) {
		FREE_NULL_DATA(data);
		return ESLURM_DATA_CONV_FAILED;
	}

	yaml_parser_delete(&parser);

	*dest = data;
	return SLURM_SUCCESS;
}
