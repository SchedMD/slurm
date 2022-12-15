/*****************************************************************************\
 *  parsers.c - Slurm data parsers
 *****************************************************************************
 *  Copyright (C) 2022 SchedMD LLC.
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

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>

#include "slurm/slurm.h"

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/select.h"

#include "alloc.h"
#include "api.h"
#include "events.h"
#include "parsers.h"
#include "parsing.h"
#include "slurmdb_helpers.h"

#define MAGIC_FOREACH_LIST 0xaefa2af3
#define MAGIC_FOREACH_LIST_FLAG 0xa1d4acd2
#define MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST 0x31b8aad2
#define MAGIC_FOREACH_STEP 0x7e2eaef1
#define MAGIC_FOREACH_STRING_ID 0x2ea1be2b
#define MAGIC_LIST_PER_TRES_TYPE_NCT 0xb1d8acd2

#define PARSER_ARRAY(type) _parser_array_##type
#define PARSE_FUNC(type) _parse_##type
#define DUMP_FUNC(type) _dump_##type
#define PARSE_DISABLED(type)                                                 \
	static int PARSE_FUNC(type)(const parser_t *const parser, void *src, \
				    data_t *dst, args_t *args,               \
				    data_t *parent_path)                     \
	{                                                                    \
		fatal_abort("parsing of DATA_PARSER_%s is not implemented",  \
			    XSTRINGIFY(type));                               \
	}

/*
 * Modify request for QOS will ignore an empty List. This allows slurmdbd to
 * know we want this field to be empty.
 */
#define EMPTY_QOS_ID_ENTRY "\'\'"

/* based on slurmdb_tres_rec_t but includes node and task */
typedef struct {
	uint64_t count;
	char *node;
	uint64_t task;
	uint32_t id;
	char *name;
	char *type;
} slurmdb_tres_nct_rec_t;

typedef enum {
	TRES_EXPLODE_COUNT = 1,
	TRES_EXPLODE_NODE,
	TRES_EXPLODE_TASK,
} tres_explode_type_t;

typedef args_t parser_env_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_LIST */
	ssize_t index;
	args_t *args;
	const parser_t *const parser;
	List list;
	data_t *dlist;
	data_t *parent_path;
} foreach_list_t;

typedef struct {
	int magic; /* MAGIC_LIST_PER_TRES_TYPE_NCT */
	tres_explode_type_t type;
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	hostlist_t host_list;
	args_t *args;
	const parser_t *const parser;
} foreach_list_per_tres_type_nct_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST */
	slurmdb_tres_nct_rec_t *tres_nct;
	int tres_nct_count;
	int offset;
} foreach_populate_g_tres_list;

typedef struct {
	int magic; /* MAGIC_FOREACH_STEP */
	data_t *steps;
	args_t *args;
	const parser_t *const parser;
} foreach_step_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_LIST_FLAG */
	args_t *args;
	const parser_t *const parser;
	void *dst; /* already has offset applied */
	data_t *parent_path;
	ssize_t index;
} foreach_flag_parser_args_t;

typedef struct {
	int magic; /* MAGIC_FOREACH_STRING_ID */
	const parser_t *const parser;
	data_t *ddst;
	data_t *parent_path;
	const char *caller;
	ssize_t index;
	List qos_list;
	args_t *args;
} foreach_qos_string_id_t;

#ifndef NDEBUG
extern void check_parser_funcname(const parser_t *const parser,
				  const char *func_name)
{
	xassert(parser->magic == MAGIC_PARSER);

	if (parser->skip) {
		/* ignore values in skipped parsers for now */
		return;
	}

	xassert(parser->type > DATA_PARSER_TYPE_INVALID);
	xassert(parser->type < DATA_PARSER_TYPE_MAX);
	xassert(parser->type_string && parser->type_string[0]);
	xassert(parser->obj_type_string && parser->obj_type_string[0]);

	xassert((parser->ptr_offset == NO_VAL) ||
		((parser->ptr_offset >= 0) && (parser->ptr_offset < NO_VAL)));
	xassert((parser->size == NO_VAL) ||
		((parser->size >= 0) && (parser->size < NO_VAL)));

	if (parser->flag != FLAG_TYPE_NONE) {
		/* parser of a specific flag */
		xassert(parser->flag > FLAG_TYPE_INVALID);
		xassert(parser->flag < FLAG_TYPE_MAX);
		/* atleast 1 bit must be set */
		xassert(parser->flag_mask);
		xassert(parser->flag_name && parser->flag_name[0]);

		/* make sure this is not a list or array type */
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->size > 0);
	} else if (parser->list_type != DATA_PARSER_TYPE_INVALID) {
		/* parser of a List */
		xassert(parser->list_type > DATA_PARSER_TYPE_INVALID);
		xassert(parser->list_type < DATA_PARSER_TYPE_MAX);
		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->size == sizeof(List));
	} else if (parser->fields) {
		/* parser of a parser Array */
		xassert(parser->field_count > 0);

		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert(parser->size > 0);
		xassert(parser->obj_type_string[0]);

		/* recursively check the child parsers */
		for (int i = 0; i < parser->field_count; i++)
			check_parser(&parser->fields[i]);
	} else if (!parser->dump) {
		/* reference to a real parser in an array */

		/* real parser must exist */
		xassert(find_parser_by_type(parser->type));

		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(!parser->parse);
		xassert(!parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
		xassert(!parser->list_new_func);
		xassert((parser->size == NO_VAL) || (parser->size > 0));
	} else {
		/* parser of simple or complex type */
		if (parser->ptr_offset == NO_VAL) {
			/* complex type */
			xassert((parser->size == NO_VAL) || (parser->size > 0));
			xassert(!parser->field_name);
		} else {
			/* simple type */
			xassert(parser->size > 0);
			xassert((parser->ptr_offset < NO_VAL) ||
				(parser->ptr_offset >= 0));
			if (parser->key) {
				/* this parser is of struct->field */
				xassert(parser->key[0]);
				xassert(parser->field_name &&
					parser->field_name[0]);
			} else {
				/* not a field in struct */
				xassert(!parser->field_name);
			}
		}

		xassert(parser->flag == FLAG_TYPE_NONE);
		xassert(!parser->fields);
		xassert(!parser->field_count);
		xassert(parser->parse);
		xassert(parser->dump);
		xassert(parser->list_type == DATA_PARSER_TYPE_INVALID);
		xassert(!parser->list_del_func);
	}
}
#endif /* !NDEBUG */

static int PARSE_FUNC(QOS_ID)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = NULL;
	uint32_t *qos_id = obj;

	xassert(args->magic == MAGIC_ARGS);

	if ((rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			      __func__, false)))
		return rc;

	*qos_id = qos->id;
	return rc;
}

static int PARSE_FUNC(QOS_NAME)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = NULL;
	char **qos_name = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (!(rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			       __func__, true))) {
		*qos_name = xstrdup(qos->name);
		return rc;
	}

	/*
	 * QOS names may not always be resolvable to a known QOS such as in the
	 * case of creating a new QOS which references a new QOS in the same QOS
	 * list. To ignore this chicken and the egg problem, we just blindly
	 * send the QOS name to slurmdbd if we can stringifiy it.
	 */
	if (data_get_type(src) == DATA_TYPE_DICT) {
		data_t *n = data_key_get(src, "name");

		if (n && !data_get_string_converted(n, qos_name))
			return SLURM_SUCCESS;

		rc = ESLURM_REST_FAIL_PARSING;
	} else if (!data_get_string_converted(src, qos_name))
		return SLURM_SUCCESS;

	if (rc) {
		char *name = NULL, *path = NULL;
		if (data_get_string_converted(src, &name))
			name = xstrdup_printf(
				"of type %s",
				data_type_to_string(data_get_type(src)));
		(void) data_list_join_str(&path, parent_path, "/");
		(void) on_error(PARSING, parser->type, args, rc, path, __func__,
				"Unable to resolve QOS %s", name);
		xfree(name);
		xfree(path);
	}

	return rc;
}

static int DUMP_FUNC(QOS_NAME)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char **name = obj;

	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_string(dst, *name);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_ID)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint32_t *qos_id = obj;
	slurmdb_qos_rec_t *qos = NULL;

	xassert(args->magic == MAGIC_ARGS);

	if (*qos_id == 0) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	/* find qos by id from global list */
	xassert(args->qos_list);
	if (!args->qos_list || list_is_empty(args->qos_list)) {
		/* no known QOS to search */
		return SLURM_SUCCESS;
	}

	qos = list_find_first(args->qos_list, slurmdb_find_qos_in_list, qos_id);
	if (!qos) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_REST_EMPTY_RESULT,
				"list_find_first()->slurmdb_find_qos_in_list()",
				__func__, "Unable to find QOS with id#%d",
				*qos_id);
	}

	/*
	 * Client is only ever provided the QOS name and not the ID as the is a
	 * Slurm internal that no user should have to track.
	 */
	(void) data_set_string(dst, qos->name);

	return SLURM_SUCCESS;
}

static int _foreach_dump_qos_string_id(void *x, void *arg)
{
	char *string_id = x;
	foreach_qos_string_id_t *argstruct = arg;
	const parser_t *const parser = argstruct->parser;
	data_t *dst = argstruct->ddst;
	args_t *args = argstruct->args;
	data_t *dstring_id = data_set_string(data_new(), string_id);
	data_t *parent_path = data_set_list(data_new());
	slurmdb_qos_rec_t *qos = NULL;

	data_set_string_fmt(data_list_append(parent_path), "QOS[%s]",
			    string_id);

	xassert(argstruct->magic == MAGIC_FOREACH_STRING_ID);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_LIST);

	if (resolve_qos(DUMPING, parser, &qos, dstring_id, args, dstring_id,
			__func__, false)) {
		FREE_NULL_DATA(dstring_id);
		FREE_NULL_DATA(parent_path);
		return ESLURM_INVALID_QOS;
	}
	FREE_NULL_DATA(dstring_id);
	FREE_NULL_DATA(parent_path);

	(void) data_set_string(data_list_append(dst), qos->name);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_STRING_ID_LIST)(const parser_t *const parser,
					 void *obj, data_t *dst, args_t *args)
{
	/*
	 * QOS string ID list is special because the contents have dynamic sizes
	 * which must be accounted for while dumping and parsing
	 */
	List *qos_list_ptr = obj;
	List qos_list = *qos_list_ptr;
	foreach_qos_string_id_t argstruct = { .magic = MAGIC_FOREACH_STRING_ID,
					      .parser = parser,
					      .args = args,
					      .ddst = dst };

	if (!qos_list)
		return SLURM_SUCCESS;

	xassert(list_count(qos_list) >= 0);
	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(list_count(args->qos_list) >= 0);

	data_set_list(dst);

	if (list_for_each(qos_list, _foreach_dump_qos_string_id, &argstruct) <
	    0)
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static data_for_each_cmd_t _foreach_parse_qos_string_id(data_t *src, void *arg)
{
	foreach_qos_string_id_t *argstruct = arg;
	const parser_t *const parser = argstruct->parser;
	List qos_list = argstruct->qos_list;
	data_t *parent_path = argstruct->parent_path;
	args_t *args = argstruct->args;
	const char *caller = argstruct->caller;
	slurmdb_qos_rec_t *qos = NULL;
	data_t *ppath = data_copy(NULL, parent_path);
	data_t *ppath_last = data_get_list_last(ppath);
	int rc;

	if (argstruct->index < 0)
		argstruct->index = 0;

	/* Use jq style array zero based array notation */
	data_set_string_fmt(ppath_last, "%s[%zu]", data_get_string(ppath_last),
			    argstruct->index);

	if ((rc = resolve_qos(PARSING, parser, &qos, src, args, parent_path,
			      caller, false)))
		return DATA_FOR_EACH_FAIL;

	(void) list_append(qos_list, xstrdup_printf("%u", qos->id));
	return DATA_FOR_EACH_CONT;
}

static int PARSE_FUNC(QOS_STRING_ID_LIST)(const parser_t *const parser,
					  void *obj, data_t *src, args_t *args,
					  data_t *parent_path)
{
	List *qos_list_ptr = obj;
	foreach_qos_string_id_t argstruct = {
		.magic = MAGIC_FOREACH_STRING_ID,
		.parser = parser,
		.args = args,
		.qos_list = list_create(xfree_ptr),
		.parent_path = parent_path,
		.caller = __func__,
		.index = -1,
	};

	xassert(args->magic == MAGIC_ARGS);

	if (data_list_for_each(src, _foreach_parse_qos_string_id, &argstruct) <
	    0) {
		FREE_NULL_LIST(argstruct.qos_list);
		return ESLURM_REST_FAIL_PARSING;
	}

	*qos_list_ptr = argstruct.qos_list;
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(QOS_PREEMPT_LIST)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int rc;
	slurmdb_qos_rec_t *qos = obj;

	xassert(!qos->preempt_list);

	if ((rc = PARSE(QOS_STRING_ID_LIST, qos->preempt_list, src, parent_path,
			args)))
		return rc;

	if (list_is_empty(qos->preempt_list)) {
		/*
		 * If the QOS list is empty, then we need to set this special
		 * entry to notify slurmdbd that this is explicilty empty and
		 * not a no change request
		 */
		list_append(qos->preempt_list, EMPTY_QOS_ID_ENTRY);
	}

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(QOS_PREEMPT_LIST)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	slurmdb_qos_rec_t *qos = obj;

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);
	xassert(args->qos_list);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(!qos->preempt_list);

	data_set_list(dst);

	if (!args->qos_list)
		return ESLURM_NOT_SUPPORTED;

	/* skip empty lists */
	if (!qos->preempt_bitstr || (bit_ffs(qos->preempt_bitstr) == -1))
		return SLURM_SUCCESS;

	/* based on get_qos_complete_str_bitstr() */
	for (int i = 1; (i < bit_size(qos->preempt_bitstr)); i++) {
		slurmdb_qos_rec_t *ptr_qos;

		if (!bit_test(qos->preempt_bitstr, i))
			continue;

		if (!(ptr_qos =
			      list_find_first(args->qos_list,
					      slurmdb_find_qos_in_list, &i))) {
			int rc;
			char *bits = bit_fmt_full(qos->preempt_bitstr);

			/*
			 * There is a race condition here where the global
			 * QOS list could have changed betwen the query of the
			 * list and the bitstrs. Just error and have the user
			 * try again if they want.
			 */
			rc = on_error(
				DUMPING, parser->type, args, ESLURM_INVALID_QOS,
				"list_find_first()->slurmdb_find_qos_in_list()",
				__func__,
				"Unable to resolve Preempt QOS (bit %u/%zu[%s]) in QOS %s(%u)",
				i, bit_size(qos->preempt_bitstr), bits,
				qos->name, qos->id);

			xfree(bits);

			if (rc)
				return rc;
		} else {
			data_set_string(data_list_append(dst), ptr_qos->name);
		}
	}

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(ASSOC_ID)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	uint32_t *associd = obj;
	slurmdb_assoc_rec_t *assoc = xmalloc(sizeof(*assoc));
	slurmdb_init_assoc_rec(assoc, false);

	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	rc = PARSE(ASSOC_SHORT, assoc, src, parent_path, args);

	if (!rc) {
		slurmdb_assoc_rec_t *match =
			list_find_first(args->assoc_list,
					(ListFindF) compare_assoc, assoc);

		if (match)
			*associd = match->id;
		else
			rc = ESLURM_REST_EMPTY_RESULT;
	}

	slurmdb_destroy_assoc_rec(assoc);

	return rc;
}

static int DUMP_FUNC(ASSOC_ID)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	uint32_t *associd = obj;
	slurmdb_assoc_rec_t *assoc = NULL;

	if (!*associd || (*associd == NO_VAL))
		return SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->assoc_list);

	if (!(assoc = list_find_first(args->assoc_list,
				      slurmdb_find_assoc_in_list, associd))) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_DATA_CONV_FAILED,
				"list_find_first()->slurmdb_find_assoc_in_list()",
				__func__, "dumping association id#%u failed",
				*associd);
	}

	return DUMP(ASSOC_SHORT_PTR, assoc, dst, args);
}

static int _foreach_resolve_tres_id(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *) x;
	args_t *args = arg;
	slurmdb_tres_rec_t *ftres;

	xassert(args->magic == MAGIC_ARGS);

	if (!tres->type && tres->id) {
		/* resolve type/name if only id provided */
		slurmdb_tres_rec_t *c =
			list_find_first_ro(args->tres_list,
					   slurmdb_find_tres_in_list,
					   &tres->id);

		if (c) {
			tres->type = xstrdup(c->type);
			tres->name = xstrdup(c->name);
		}
	}

	/*
	 * This may be a new TRES being created so there won't be an
	 * existing TRES to compare against.
	 */
	if (!(ftres = list_find_first_ro(args->tres_list,
					 (ListFindF) fuzzy_match_tres, tres)))
		return SLURM_SUCCESS;

	/* verify ID if possible */

	if ((tres->id > 0) && (tres->id != ftres->id))
		return ESLURM_INVALID_TRES;

	if (!tres->id)
		tres->id = ftres->id;

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(TRES_STR)(const parser_t *const parser, void *obj,
				data_t *src, args_t *args, data_t *parent_path)
{
	char **tres = obj;
	int rc;
	List tres_list = NULL;

	xassert(!*tres);
	xassert(args->magic == MAGIC_ARGS);

	if (!args->tres_list) {
		/* should not happen */
		xassert(args->tres_list);
		rc = ESLURM_NOT_SUPPORTED;
		goto cleanup;
	}

	if (data_get_type(src) != DATA_TYPE_LIST) {
		char *path = NULL;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_REST_FAIL_PARSING, path, __func__,
			      "TRES should be LIST but is type %s",
			      data_type_to_string(data_get_type(src)));
		xfree(path);
		goto cleanup;
	}

	if ((rc = PARSE(TRES_LIST, tres_list, src, parent_path, args)))
		goto cleanup;

	(void) list_for_each(tres_list, _foreach_resolve_tres_id, &args);

	if ((*tres = slurmdb_make_tres_string(tres_list,
					      TRES_STR_FLAG_SIMPLE))) {
		rc = SLURM_SUCCESS;
	} else {
		char *path = NULL;
		xassert(false); /* should not have failed */
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(PARSING, parser->type, args,
			      ESLURM_REST_FAIL_PARSING, path, __func__,
			      "Unable to convert TRES to string");
		xfree(path);
	}

cleanup:
	FREE_NULL_LIST(tres_list);
	return rc;
}

static int DUMP_FUNC(TRES_STR)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	int rc;
	char **tres = obj;
	List tres_list = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(args->tres_list && (list_count(args->tres_list) >= 0));

	if (!args->tres_list) {
		xassert(false);
		return on_error(DUMPING, parser->type, args,
				ESLURM_NOT_SUPPORTED, "TRES list not available",
				__func__, "TRES conversion requires TRES list");
	}

	if (!*tres || !*tres[0])
		/* ignore empty TRES strings */
		return SLURM_SUCCESS;

	slurmdb_tres_list_from_string(&tres_list, *tres, TRES_STR_FLAG_BYTES);

	if (!tres_list) {
		rc = on_error(DUMPING, parser->type, args,
			      ESLURM_DATA_CONV_FAILED,
			      "slurmdb_tres_list_from_string", __func__,
			      "Unable to convert TRES from string");
	}

	if (list_is_empty(tres_list))
		goto cleanup;

	list_for_each(tres_list, _foreach_resolve_tres_id, args);

	if ((rc = DUMP(TRES_LIST, tres_list, dst, args)))
		return rc;
cleanup:
	FREE_NULL_LIST(tres_list);

	return SLURM_SUCCESS;
}

static int _foreach_list_per_tres_type_nct(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = (slurmdb_tres_rec_t *) x;
	foreach_list_per_tres_type_nct_t *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = NULL;

	xassert(args->magic == MAGIC_LIST_PER_TRES_TYPE_NCT);

	for (int i = 0; i < args->tres_nct_count; i++)
		if (args->tres_nct[i].id == tres->id)
			tres_nct = args->tres_nct + i;

	xassert(tres_nct);
	if (!tres_nct)
		/* out of sync?? */
		return -1;

	switch (args->parser->type) {
	case TRES_EXPLODE_NODE :
		xassert(!tres_nct->node);
		free(tres_nct->node);
		/* based on find_hostname() */
		tres_nct->node = hostlist_nth(args->host_list, tres->count);
		return 1;
	case TRES_EXPLODE_TASK :
		xassert(!tres_nct->task);
		tres_nct->task = tres->count;
		return 1;
	case TRES_EXPLODE_COUNT :
		xassert(!tres_nct->count);
		tres_nct->count = tres->count;
		return 1;
	default :
		fatal("%s: unexpected type", __func__);
	}
}

static int _foreach_populate_g_tres_list(void *x, void *arg)
{
	slurmdb_tres_rec_t *tres = x;
	foreach_populate_g_tres_list *args = arg;
	slurmdb_tres_nct_rec_t *tres_nct = args->tres_nct + args->offset;

	xassert(args->magic == MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST);

	tres_nct->id = tres->id;
	tres_nct->name = tres->name;
	tres_nct->type = tres->type;

	xassert(args->offset < args->tres_nct_count);
	args->offset += 1;
	return 0;
}

static int _dump_tres_nct(const parser_t *const parser, data_t *dst,
			  char *tres_count, char *tres_node, char *tres_task,
			  char *nodes, args_t *args)
{
	int rc = SLURM_SUCCESS;
	foreach_list_per_tres_type_nct_t fargs = {
		.magic = MAGIC_LIST_PER_TRES_TYPE_NCT,
		.args = args,
		.parser = parser,
	};
	foreach_populate_g_tres_list gtres_args = {
		.magic = MAGIC_FOREACH_POPULATE_GLOBAL_TRES_LIST,
	};
	slurmdb_tres_nct_rec_t *tres_nct = NULL;
	int tres_nct_count = 0;
	List tres_count_list = NULL;
	List tres_node_list = NULL;
	List tres_task_list = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	xassert(args->tres_list);
	if (!args->tres_list)
		goto cleanup;

	if (!tres_count && !tres_node && !tres_task)
		/* ignore empty TRES strings */
		goto cleanup;

	fargs.tres_nct_count = gtres_args.tres_nct_count = tres_nct_count =
		list_count(args->tres_list);
	fargs.tres_nct = gtres_args.tres_nct = tres_nct =
		xcalloc(list_count(args->tres_list), sizeof(*tres_nct));
	if (list_for_each_ro(args->tres_list, _foreach_populate_g_tres_list,
			     &gtres_args) < 0)
		goto cleanup;

	fargs.host_list = hostlist_create(nodes);

	slurmdb_tres_list_from_string(&tres_count_list, tres_count,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_node_list, tres_node,
				      TRES_STR_FLAG_BYTES);
	slurmdb_tres_list_from_string(&tres_task_list, tres_task,
				      TRES_STR_FLAG_BYTES);

	fargs.type = TRES_EXPLODE_COUNT;
	if (tres_count_list &&
	    (list_for_each(tres_count_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	fargs.type = TRES_EXPLODE_NODE;
	if (tres_node_list &&
	    (list_for_each(tres_node_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	fargs.type = TRES_EXPLODE_TASK;
	if (tres_task_list &&
	    (list_for_each(tres_task_list, _foreach_list_per_tres_type_nct,
			   &fargs) < 0))
		goto cleanup;
	/* clear type to catch unintended reuse */
	fargs.type = 0;

	for (int i = 0; !rc && (i < tres_nct_count); i++)
		if (tres_nct[i].count || tres_nct[i].node || tres_nct[i].task)
			rc = DUMP(TRES_NCT, tres_nct[i],
				  data_set_dict(data_list_append(dst)), args);

cleanup:
	FREE_NULL_LIST(tres_count_list);
	FREE_NULL_LIST(tres_node_list);
	FREE_NULL_LIST(tres_task_list);
	FREE_NULL_HOSTLIST(fargs.host_list);
	for (int i = 0; i < tres_nct_count; i++)
		/* hostlist_nth doesn't use xfree() */
		free(tres_nct[i].node);
	xfree(tres_nct);

	return rc;
}

static int PARSE_FUNC(ADMIN_LVL)(const parser_t *const parser, void *obj,
				 data_t *src, args_t *args, data_t *parent_path)
{
	uint16_t *admin_level = obj;

	if (data_convert_type(src, DATA_TYPE_STRING) != DATA_TYPE_STRING) {
		char *path = NULL;
		int rc;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(
			PARSING, parser->type, args, ESLURM_REST_FAIL_PARSING,
			path, __func__,
			"unable to convert administrator level to string from type %s",
			data_type_to_string(data_get_type(src)));
		xfree(path);
		return rc;
	}

	xassert(args->magic == MAGIC_ARGS);

	*admin_level = str_2_slurmdb_admin_level(data_get_string(src));

	if (*admin_level == SLURMDB_ADMIN_NOTSET) {
		char *path = NULL;
		int rc;
		(void) data_list_join_str(&path, parent_path, "/");
		rc = on_error(
			PARSING, parser->type, args, ESLURM_REST_FAIL_PARSING,
			path, __func__,
			"unable to parse %s as a known administrator level",
			data_get_string(src));
		xfree(path);
		return rc;
	}

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(ADMIN_LVL)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint16_t *admin_level = obj;

	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_string(dst, slurmdb_admin_level_str(*admin_level));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_EXIT_CODE)

static int DUMP_FUNC(JOB_EXIT_CODE)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint32_t *ec = obj;
	data_t *drc, *dsc;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	(void) data_set_dict(dst);

	dsc = data_key_set(dst, "status");
	drc = data_key_set(dst, "return_code");

	if (*ec == NO_VAL)
		data_set_string(dsc, "PENDING");
	else if (WIFEXITED(*ec)) {
		data_set_string(dsc, "SUCCESS");
		data_set_int(drc, 0);
	} else if (WIFSIGNALED(*ec)) {
		data_t *sig = data_set_dict(data_key_set(dst, "signal"));
		data_set_string(dsc, "SIGNALED");

		data_set_int(data_key_set(sig, "signal_id"), WTERMSIG(*ec));
		data_set_string(data_key_set(sig, "name"),
				strsignal(WTERMSIG(*ec)));
	} else if (WCOREDUMP(*ec)) {
		data_set_string(dsc, "CORE_DUMPED");
	} else {
		data_set_string(dsc, "ERROR");
		data_set_int(drc, WEXITSTATUS(*ec));
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_USER)

static int DUMP_FUNC(JOB_USER)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char *user;
	slurmdb_job_rec_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	/* job user may be set but fail back to resolving the uid */

	if (job->user && job->user[0]) {
		data_set_string(dst, job->user);
		return SLURM_SUCCESS;
	}

	user = uid_to_string_or_null(job->uid);

	if (user && user[0]) {
		data_set_string_own(dst, user);
		return SLURM_SUCCESS;
	}

	data_set_null(dst);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_REC_ARRAY)

static int DUMP_FUNC(STATS_REC_ARRAY)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_rollup_stats_t **ptr_stats = obj;
	slurmdb_rollup_stats_t *rollup_stats;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (!(rollup_stats = *ptr_stats)) {
		return on_error(DUMPING, parser->type, args,
				ESLURM_DATA_CONV_FAILED, "slurmctld", __func__,
				"rollup stats not provided by controller");
	}

	for (int i = 0; i < DBD_ROLLUP_COUNT; i++) {
		data_t *d;
		uint64_t roll_ave;

		if (rollup_stats->time_total[i] == 0)
			continue;

		d = data_set_dict(data_list_append(dst));

		if (i == 0)
			data_set_string(data_key_set(d, "type"), "internal");
		else if (i == 1)
			data_set_string(data_key_set(d, "type"), "user");
		else
			data_set_string(data_key_set(d, "type"), "unknown");

		data_set_int(data_key_set(d, "last_run"),
			     rollup_stats->timestamp[i]);

		roll_ave = rollup_stats->time_total[i];
		if (rollup_stats->count[i] > 1)
			roll_ave /= rollup_stats->count[i];

		data_set_int(data_key_set(d, "last_cycle"),
			     rollup_stats->time_last[i]);
		data_set_int(data_key_set(d, "max_cycle"),
			     rollup_stats->time_max[i]);
		data_set_int(data_key_set(d, "total_time"),
			     rollup_stats->time_total[i]);
		data_set_int(data_key_set(d, "total_cycles"),
			     rollup_stats->count[i]);
		data_set_int(data_key_set(d, "mean_cycles"), roll_ave);
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(RPC_ID)

static int DUMP_FUNC(RPC_ID)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	slurmdbd_msg_type_t *id = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, slurmdbd_msg_type_2_str(*id, 1));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(CLUSTER_ACCT_REC)

static int DUMP_FUNC(CLUSTER_ACCT_REC)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	slurmdb_cluster_accounting_rec_t *acct = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	data_set_list(dst);

	if (!acct)
		return ESLURM_DATA_CONV_FAILED;

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(SELECT_PLUGIN_ID)(const parser_t *const parser, void *obj,
					data_t *src, args_t *args,
					data_t *parent_path)
{
	int *id = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 (*id = select_string_to_plugin_id(data_get_string(src)) > 0))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(SELECT_PLUGIN_ID)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	int *id = obj;
	char *s = select_plugin_id_to_string(*id);

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (s) {
		data_set_string(dst, s);
	} else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(TASK_DISTRIBUTION)

static int DUMP_FUNC(TASK_DISTRIBUTION)(const parser_t *const parser, void *obj,
					data_t *dst, args_t *args)
{
	uint32_t *dist = obj;
	char *d = slurm_step_layout_type_name(*dist);

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string_own(dst, d);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STEP_ID)

static int DUMP_FUNC(STEP_ID)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	uint32_t *id = obj;

	xassert(args->magic == MAGIC_ARGS);

	// TODO rewrite after bug#9622 resolved

	switch (*id) {
	case SLURM_EXTERN_CONT :
		data_set_string(dst, "extern");
		break;
	case SLURM_BATCH_SCRIPT :
		data_set_string(dst, "batch");
		break;
	case SLURM_PENDING_STEP :
		data_set_string(dst, "pending");
		break;
	case SLURM_INTERACTIVE_STEP :
		data_set_string(dst, "interactive");
		break;
	default :
		data_set_int(dst, *id);
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(WCKEY_TAG)

static int DUMP_FUNC(WCKEY_TAG)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	char **src = obj;
	data_t *flags, *key;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!*src) {
		data_set_null(dst);
		return SLURM_SUCCESS;
	}

	key = data_key_set(data_set_dict(dst), "wckey");
	flags = data_set_list(data_key_set(dst, "flags"));

	if (*src[0] == '*') {
		data_set_string(data_list_append(flags), "ASSIGNED_DEFAULT");
		data_set_string(key, (*src + 1));
	} else
		data_set_string(key, *src);

	return SLURM_SUCCESS;
}

static int DUMP_FUNC(USER_ID)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	uid_t *uid = obj;
	char *u;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((u = uid_to_string_or_null(*uid)))
		data_set_string_own(dst, u);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(USER_ID)(const parser_t *const parser, void *obj,
			       data_t *src, args_t *args, data_t *parent_path)
{
	uid_t *uid = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL)
		return ESLURM_REST_FAIL_PARSING;
	else if (data_convert_type(src, DATA_TYPE_STRING) == DATA_TYPE_STRING &&
		 !uid_from_string(data_get_string(src), uid))
		return SLURM_SUCCESS;

	return ESLURM_REST_FAIL_PARSING;
}

PARSE_DISABLED(GROUP_ID)

static int DUMP_FUNC(GROUP_ID)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	gid_t *gid = obj;
	char *g;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((g = gid_to_string_or_null(*gid)))
		data_set_string_own(dst, g);
	else
		data_set_null(dst);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_REASON)

static int DUMP_FUNC(JOB_REASON)(const parser_t *const parser, void *obj,
				 data_t *dst, args_t *args)
{
	uint32_t *state = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, job_reason_string(*state));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_STATE)

static int DUMP_FUNC(JOB_STATE)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint32_t *state = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_string(dst, job_state_string(*state));

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(STRING)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	int rc = SLURM_SUCCESS;
	char **dst = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		xfree(*dst);
	} else if (data_convert_type(str, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		xfree(*dst);
		*dst = xstrdup(data_get_string(str));
	} else {
		rc = ESLURM_DATA_CONV_FAILED;
	}

	debug5("%s: string %s rc[%d]=%s", __func__, *dst, rc,
	       slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(STRING)(const parser_t *const parser, void *obj,
			     data_t *data, args_t *args)
{
	int rc = SLURM_SUCCESS;
	char **src = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (*src)
		data_set_string(data, *src);
	else
		data_set_null(data);

	return rc;
}

static int PARSE_FUNC(FLOAT128)(const parser_t *const parser, void *obj,
				data_t *str, args_t *args, data_t *parent_path)
{
	long double *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(sizeof(long double) * 8 == 128);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double) NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %Lf rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(FLOAT128)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	long double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* see bug#9674 */
	if (((uint32_t) *src == INFINITE) || ((uint32_t) *src == NO_VAL))
		data_set_null(dst);
	else
		(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(FLOAT64)(const parser_t *const parser, void *obj,
			       data_t *str, args_t *args, data_t *parent_path)
{
	double *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(sizeof(double) * 8 == 64);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %f rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(FLOAT64)(const parser_t *const parser, void *obj,
			      data_t *dst, args_t *args)
{
	double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(FLOAT64_NO_VAL)(const parser_t *const parser, void *obj,
				      data_t *str, args_t *args,
				      data_t *parent_path)
{
	double *dst = obj;

	xassert(sizeof(double) * 8 == 64);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = (double) NO_VAL;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(FLOAT64)(parser, obj, str, args, parent_path);
}

static int DUMP_FUNC(FLOAT64_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	double *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* see bug#9674 about double comparison and casting */
	if (((uint32_t) *src == INFINITE) || ((uint32_t) *src == NO_VAL))
		(void) data_set_null(dst);
	else
		(void) data_set_float(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(INT64)(const parser_t *const parser, void *obj,
			     data_t *str, args_t *args, data_t *parent_path)
{
	int64_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = (double) NO_VAL;
	else if (data_convert_type(str, DATA_TYPE_FLOAT) == DATA_TYPE_FLOAT)
		*dst = data_get_float(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %" PRId64 " rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(INT64)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	int64_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		(void) data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT16)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint16_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %hu rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT16)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint16_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT16_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *str, args_t *args,
				     data_t *parent_path)
{
	uint16_t *dst = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = NO_VAL16;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(UINT16)(parser, obj, str, args, parent_path);
}

static int DUMP_FUNC(UINT16_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint16_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL16) || (*src == INFINITE16))
		data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT64)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint64_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = 0;
	else if (data_convert_type(str, DATA_TYPE_INT_64) == DATA_TYPE_INT_64)
		*dst = data_get_int(str);
	else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %" PRIu64 " rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT64)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint64_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	/* Never set values of INF or NO_VAL */
	if ((*src == NO_VAL64) || (*src == INFINITE64))
		data_set_null(dst);
	else
		(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT32)(const parser_t *const parser, void *obj,
			      data_t *str, args_t *args, data_t *parent_path)
{
	uint32_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL) {
		*dst = 0;
	} else if (data_convert_type(str, DATA_TYPE_INT_64) ==
		   DATA_TYPE_INT_64) {
		/* catch -1 and set to NO_VAL instead of rolling */
		if (0xFFFFFFFF00000000 & data_get_int(str))
			*dst = NO_VAL;
		else
			*dst = data_get_int(str);
	} else
		rc = ESLURM_DATA_CONV_FAILED;

	log_flag(DATA, "%s: string %u rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT32)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint32_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	log_flag(DATA, "%s: uint32_t 0x%" PRIxPTR "=%u", __func__,
		 (uintptr_t) src, *src);
	(void) data_set_int(dst, *src);

	return SLURM_SUCCESS;
}

static int PARSE_FUNC(UINT32_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *str, args_t *args,
				     data_t *parent_path)
{
	uint32_t *dst = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(str) == DATA_TYPE_NULL)
		*dst = NO_VAL;
	else
		rc = PARSE_FUNC(UINT32)(parser, obj, str, args, parent_path);

	log_flag(DATA, "%s: string %u rc[%d]=%s", __func__, *dst, rc,
		 slurm_strerror(rc));

	return rc;
}

static int DUMP_FUNC(UINT32_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	uint32_t *src = obj;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);

	if ((*src == NO_VAL) || (*src == INFINITE))
		data_set_null(dst);
	else
		rc = DUMP_FUNC(UINT32)(parser, obj, dst, args);

	return rc;
}

PARSE_DISABLED(STEP_NODES)

static int DUMP_FUNC(STEP_NODES)(const parser_t *const parser, void *src,
				 data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;
	hostlist_t host_list;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	(void) data_set_list(dst);

	/* ignore empty node list */
	if (!step->nodes)
		return SLURM_SUCCESS;

	if (!(host_list = hostlist_create(step->nodes)))
		return errno;

	xassert(hostlist_count(host_list) == step->nnodes);
	if (hostlist_count(host_list)) {
		char *host;
		hostlist_iterator_t itr = hostlist_iterator_create(host_list);

		while ((host = hostlist_next(itr))) {
			data_set_string(data_list_append(dst), host);
			free(host);
		}

		hostlist_iterator_destroy(itr);
	}

	FREE_NULL_HOSTLIST(host_list);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(STEP_TRES_REQ_MAX)

static int DUMP_FUNC(STEP_TRES_REQ_MAX)(const parser_t *const parser, void *src,
					data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_in_max,
			      step->stats.tres_usage_in_max_nodeid,
			      step->stats.tres_usage_in_max_taskid, step->nodes,
			      args);
}

PARSE_DISABLED(STEP_TRES_REQ_MIN)

static int DUMP_FUNC(STEP_TRES_REQ_MIN)(const parser_t *const parser, void *src,
					data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_in_min,
			      step->stats.tres_usage_in_min_nodeid,
			      step->stats.tres_usage_in_min_taskid, step->nodes,
			      args);
}

PARSE_DISABLED(STEP_TRES_USAGE_MAX)

static int DUMP_FUNC(STEP_TRES_USAGE_MAX)(const parser_t *const parser,
					  void *src, data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_out_max,
			      step->stats.tres_usage_out_max_nodeid,
			      step->stats.tres_usage_out_max_taskid,
			      step->nodes, args);
}

PARSE_DISABLED(STEP_TRES_USAGE_MIN)

static int DUMP_FUNC(STEP_TRES_USAGE_MIN)(const parser_t *const parser,
					  void *src, data_t *dst, args_t *args)
{
	slurmdb_step_rec_t *step = src;

	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(args->magic == MAGIC_ARGS);
	check_parser(parser);

	return _dump_tres_nct(parser, dst, step->stats.tres_usage_out_min,
			      step->stats.tres_usage_out_min_nodeid,
			      step->stats.tres_usage_out_min_taskid,
			      step->nodes, args);
}

static int PARSE_FUNC(BOOL)(const parser_t *const parser, void *obj,
			    data_t *src, args_t *args, data_t *parent_path)
{
	uint8_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_convert_type(src, DATA_TYPE_BOOL) == DATA_TYPE_BOOL) {
		*b = data_get_bool(src);
		return SLURM_SUCCESS;
	}

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(BOOL)(const parser_t *const parser, void *obj, data_t *dst,
			   args_t *args)
{
	uint8_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(BOOL16)(const parser_t *const parser, void *obj,
			      data_t *src, args_t *args, data_t *parent_path)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_convert_type(src, DATA_TYPE_BOOL) == DATA_TYPE_BOOL) {
		*b = data_get_bool(src);
		return SLURM_SUCCESS;
	}

	return ESLURM_REST_FAIL_PARSING;
}

static int DUMP_FUNC(BOOL16)(const parser_t *const parser, void *obj,
			     data_t *dst, args_t *args)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(BOOL16_NO_VAL)(const parser_t *const parser, void *obj,
				     data_t *src, args_t *args,
				     data_t *parent_path)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) == DATA_TYPE_NULL) {
		*b = NO_VAL16;
		return SLURM_SUCCESS;
	}

	return PARSE_FUNC(BOOL16)(parser, obj, src, args, parent_path);
}

static int DUMP_FUNC(BOOL16_NO_VAL)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	uint16_t *b = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*b == NO_VAL16) {
		/* leave as NULL */
		return SLURM_SUCCESS;
	}

	data_set_bool(dst, *b);
	return SLURM_SUCCESS;
}

static int PARSE_FUNC(ASSOC_SHORT_PTR)(const parser_t *const parser, void *obj,
				       data_t *src, args_t *args,
				       data_t *parent_path)
{
	slurmdb_assoc_rec_t **assoc_ptr = obj;
	slurmdb_assoc_rec_t *assoc = NULL;
	int rc;

	xassert(assoc_ptr);
	xassert(!*assoc_ptr);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	if ((rc = PARSE(ASSOC_SHORT, assoc, src, parent_path, args))) {
		slurmdb_destroy_assoc_rec(assoc);
		assoc = NULL;
	} else {
		*assoc_ptr = assoc;
	}

	return rc;
}

static int DUMP_FUNC(ASSOC_SHORT_PTR)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_assoc_rec_t **assoc_ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(assoc_ptr);

	if (!*assoc_ptr) {
		/* ignore NULL assoc ptr */
		return SLURM_SUCCESS;
	}

	return DUMP(ASSOC_SHORT, **assoc_ptr, dst, args);
}

static int PARSE_FUNC(ASSOC_USAGE_PTR)(const parser_t *const parser, void *obj,
				       data_t *src, args_t *args,
				       data_t *parent_path)
{
	slurmdb_assoc_usage_t **assoc_ptr = obj;
	slurmdb_assoc_usage_t *assoc = NULL;
	int rc;

	xassert(assoc_ptr);
	xassert(!*assoc_ptr);
	xassert(args->magic == MAGIC_ARGS);

	if (data_get_type(src) != DATA_TYPE_DICT)
		return ESLURM_REST_FAIL_PARSING;

	if ((rc = PARSE(ASSOC_USAGE, assoc, src, parent_path, args))) {
		slurmdb_destroy_assoc_usage(assoc);
		assoc = NULL;
	} else {
		*assoc_ptr = assoc;
	}

	return rc;
}

static int DUMP_FUNC(ASSOC_USAGE_PTR)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	slurmdb_assoc_usage_t **assoc_ptr = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(assoc_ptr);

	if (!*assoc_ptr) {
		/* ignore NULL assoc ptr */
		return SLURM_SUCCESS;
	}

	return DUMP(ASSOC_USAGE, *assoc_ptr, dst, args);
}

PARSE_DISABLED(STATS_MSG_CYCLE_MEAN)

static int DUMP_FUNC(STATS_MSG_CYCLE_MEAN)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->schedule_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_sum /
			   stats->schedule_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_CYCLE_MEAN_DEPTH)

static int DUMP_FUNC(STATS_MSG_CYCLE_MEAN_DEPTH)(const parser_t *const parser,
						 void *obj, data_t *dst,
						 args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->schedule_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_depth /
			   stats->schedule_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_CYCLE_PER_MIN)

static int DUMP_FUNC(STATS_MSG_CYCLE_PER_MIN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((stats->req_time - stats->req_time_start) < 60)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->schedule_cycle_counter /
			   ((stats->req_time - stats->req_time_start) / 60)));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_CYCLE_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_CYCLE_MEAN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_cycle_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_DEPTH_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_DEPTH_MEAN)(const parser_t *const parser,
					      void *obj, data_t *dst,
					      args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_depth_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_DEPTH_MEAN_TRY)

static int DUMP_FUNC(STATS_MSG_BF_DEPTH_MEAN_TRY)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_depth_try_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_QUEUE_LEN_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_QUEUE_LEN_MEAN)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_queue_len_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_TABLE_SIZE_MEAN)

static int DUMP_FUNC(STATS_MSG_BF_TABLE_SIZE_MEAN)(const parser_t *const parser,
						   void *obj, data_t *dst,
						   args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->bf_cycle_counter)
		return SLURM_SUCCESS;

	data_set_int(dst, (stats->bf_table_size_sum / stats->bf_cycle_counter));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_BF_ACTIVE)

static int DUMP_FUNC(STATS_MSG_BF_ACTIVE)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_bool(dst, (stats->bf_active != 0));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_RPCS_BY_TYPE)

static int DUMP_FUNC(STATS_MSG_RPCS_BY_TYPE)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	uint32_t *rpc_type_ave_time;
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->rpc_type_size)
		return SLURM_SUCCESS;

	data_set_list(dst);

	rpc_type_ave_time =
		xcalloc(stats->rpc_type_size, sizeof(*rpc_type_ave_time));

	for (int i = 0; i < stats->rpc_type_size; i++) {
		if (stats->rpc_type_time[i] > 0)
			rpc_type_ave_time[i] = stats->rpc_type_time[i] /
					       stats->rpc_type_cnt[i];
		else
			rpc_type_ave_time[i] = 0;
	}

	for (int i = 0; i < stats->rpc_type_size; i++) {
		data_t *r = data_set_dict(data_list_append(dst));
		data_set_string(data_key_set(r, "message_type"),
				rpc_num2string(stats->rpc_type_id[i]));
		data_set_int(data_key_set(r, "type_id"), stats->rpc_type_id[i]);
		data_set_int(data_key_set(r, "count"), stats->rpc_type_cnt[i]);
		data_set_int(data_key_set(r, "average_time"),
			     rpc_type_ave_time[i]);
		data_set_int(data_key_set(r, "total_time"),
			     stats->rpc_type_time[i]);
	}

	xfree(rpc_type_ave_time);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(STATS_MSG_RPCS_BY_USER)

static int DUMP_FUNC(STATS_MSG_RPCS_BY_USER)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	uint32_t *rpc_user_ave_time;
	stats_info_response_msg_t *stats = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!stats->rpc_user_size)
		return SLURM_SUCCESS;

	data_set_list(dst);

	rpc_user_ave_time =
		xcalloc(stats->rpc_user_size, sizeof(*rpc_user_ave_time));

	for (int i = 0; i < stats->rpc_user_size; i++) {
		if (stats->rpc_user_time[i] > 0)
			rpc_user_ave_time[i] = stats->rpc_user_time[i] /
					       stats->rpc_user_cnt[i];
		else
			rpc_user_ave_time[i] = 0;
	}

	for (int i = 0; i < stats->rpc_user_size; i++) {
		data_t *u = data_set_dict(data_list_append(dst));
		data_t *un = data_key_set(u, "user");
		char *user = uid_to_string_or_null(stats->rpc_user_id[i]);

		data_set_int(data_key_set(u, "user_id"), stats->rpc_user_id[i]);
		data_set_int(data_key_set(u, "count"), stats->rpc_user_cnt[i]);
		data_set_int(data_key_set(u, "average_time"),
			     rpc_user_ave_time[i]);
		data_set_int(data_key_set(u, "total_time"),
			     stats->rpc_user_time[i]);

		if (!user)
			data_set_string_fmt(un, "%u", stats->rpc_user_id[i]);
		else
			data_set_string_own(un, user);
	}

	xfree(rpc_user_ave_time);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_BASE_STATE)

static int DUMP_FUNC(NODE_BASE_STATE)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	uint32_t *state_ptr = obj;
	uint32_t state = *state_ptr;
	char *str_state;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	str_state = xstrdup(node_state_base_string(state));
	xstrtolower(str_state);
	data_set_string_own(dst, str_state);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(CSV_LIST)

static int DUMP_FUNC(CSV_LIST)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	char **src_ptr = obj;
	char *src = *src_ptr;
	char *save_ptr = NULL;
	char *token = NULL;
	char *str;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!src || (src[0] = '\0'))
		return SLURM_SUCCESS;

	str = xstrdup(src);
	token = strtok_r(str, ",", &save_ptr);
	while (token) {
		data_set_string(data_list_append(dst), token);
		token = strtok_r(NULL, ",", &save_ptr);
	}

	xfree(str);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_MEMORY)

static int DUMP_FUNC(NODE_SELECT_ALLOC_MEMORY)(const parser_t *const parser,
					       void *obj, data_t *dst,
					       args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint64_t alloc_memory = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_MEM_ALLOC,
					    NODE_STATE_ALLOCATED,
					    &alloc_memory))) {
		return on_error(
			DUMPING, parser->type, args, rc,
			"slurm_get_select_nodeinfo", __func__,
			"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_MEM_ALLOC) failed",
			node->name);
	}

	data_set_int(dst, alloc_memory);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_CPUS)

static int DUMP_FUNC(NODE_SELECT_ALLOC_CPUS)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint16_t alloc_cpus = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_SUBCNT,
					    NODE_STATE_ALLOCATED,
					    &alloc_cpus))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_SUBCNT) failed",
				node->name);
	}

	data_set_int(dst, alloc_cpus);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_ALLOC_IDLE_CPUS)

static int DUMP_FUNC(NODE_SELECT_ALLOC_IDLE_CPUS)(const parser_t *const parser,
						  void *obj, data_t *dst,
						  args_t *args)
{
	int rc;
	node_info_t *node = obj;
	uint16_t alloc_cpus = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_SUBCNT,
					    NODE_STATE_ALLOCATED,
					    &alloc_cpus))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_SUBCNT) failed",
				node->name);
	}

	data_set_int(dst, (node->cpus - alloc_cpus));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_TRES_USED)

static int DUMP_FUNC(NODE_SELECT_TRES_USED)(const parser_t *const parser,
					    void *obj, data_t *dst,
					    args_t *args)
{
	int rc;
	node_info_t *node = obj;
	char *node_alloc_tres = NULL;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_TRES_ALLOC_FMT_STR,
					    NODE_STATE_ALLOCATED,
					    &node_alloc_tres))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_FMT_STR) failed",
				node->name);
	}

	data_set_string_own(dst, node_alloc_tres);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODE_SELECT_TRES_WEIGHTED)

static int DUMP_FUNC(NODE_SELECT_TRES_WEIGHTED)(const parser_t *const parser,
						void *obj, data_t *dst,
						args_t *args)
{
	int rc;
	node_info_t *node = obj;
	double node_tres_weighted = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((rc = slurm_get_select_nodeinfo(node->select_nodeinfo,
					    SELECT_NODEDATA_TRES_ALLOC_WEIGHTED,
					    NODE_STATE_ALLOCATED,
					    &node_tres_weighted))) {
		return on_error(DUMPING, parser->type, args, rc,
				"slurm_get_select_nodeinfo", __func__,
				"slurm_get_select_nodeinfo(%s, SELECT_NODEDATA_TRES_ALLOC_WEIGHTED) failed",
				node->name);
	}

	data_set_float(dst, node_tres_weighted);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NODES)

static int DUMP_FUNC(NODES)(const parser_t *const parser, void *obj,
			    data_t *dst, args_t *args)
{
	node_info_msg_t *nodes = obj;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(nodes);

	data_set_list(dst);

	if (!nodes->record_count)
		return SLURM_SUCCESS;

	for (int i = 0; !rc && (i < nodes->record_count); i++)
		rc = DUMP(NODE, nodes->node_array[i], data_list_append(dst),
			  args);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(LICENSES)

static int DUMP_FUNC(LICENSES)(const parser_t *const parser, void *obj,
			       data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	license_info_msg_t **msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	if (!msg || !(*msg)->num_lic) {
		on_warn(DUMPING, parser->type, args, NULL, __func__,
			"Zero licenses to dump");
		return SLURM_SUCCESS;
	}

	for (size_t i = 0; !rc && (i < (*msg)->num_lic); i++)
		rc = DUMP(LICENSE, (*msg)->lic_array[i], data_list_append(dst),
			  args);

	return rc;
}

PARSE_DISABLED(CORE_SPEC)

static int DUMP_FUNC(CORE_SPEC)(const parser_t *const parser, void *obj,
				data_t *dst, args_t *args)
{
	uint16_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!(*mem & CORE_SPEC_THREAD))
		data_set_int(dst, *mem);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(THREAD_SPEC)

static int DUMP_FUNC(THREAD_SPEC)(const parser_t *const parser, void *obj,
				  data_t *dst, args_t *args)
{
	uint16_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((*mem & CORE_SPEC_THREAD))
		data_set_int(dst, (*mem & ~CORE_SPEC_THREAD));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_GRES_DETAIL)

static int DUMP_FUNC(JOB_INFO_GRES_DETAIL)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	slurm_job_info_t *job = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(job);

	data_set_list(dst);

	for (int i = 0; i < job->gres_detail_cnt; i++)
		data_set_string(data_list_append(dst), job->gres_detail_str[i]);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(NICE)

static int DUMP_FUNC(NICE)(const parser_t *const parser, void *obj, data_t *dst,
			   args_t *args)
{
	uint32_t *nice = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if ((*nice != NO_VAL) && (*nice != NICE_OFFSET))
		data_set_int(dst, (*nice - NICE_OFFSET));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_MEM_PER_CPU)

static int DUMP_FUNC(JOB_MEM_PER_CPU)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	uint64_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*mem & MEM_PER_CPU)
		data_set_int(dst, (*mem & ~MEM_PER_CPU));

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_MEM_PER_NODE)

static int DUMP_FUNC(JOB_MEM_PER_NODE)(const parser_t *const parser, void *obj,
				       data_t *dst, args_t *args)
{
	uint64_t *mem = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (!(*mem & MEM_PER_CPU))
		data_set_int(dst, *mem);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ACCT_GATHER_PROFILE)

static int DUMP_FUNC(ACCT_GATHER_PROFILE)(const parser_t *const parser,
					  void *obj, data_t *dst, args_t *args)
{
	uint32_t *profile = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*profile == ACCT_GATHER_PROFILE_NOT_SET)
		return SLURM_SUCCESS;

	data_set_list(dst);

	if (*profile == ACCT_GATHER_PROFILE_NONE) {
		data_set_string(data_list_append(dst), "None");
		return SLURM_SUCCESS;
	}

	if (*profile & ACCT_GATHER_PROFILE_ENERGY)
		data_set_string(data_list_append(dst), "Energy");
	if (*profile & ACCT_GATHER_PROFILE_LUSTRE)
		data_set_string(data_list_append(dst), "Lustre");
	if (*profile & ACCT_GATHER_PROFILE_NETWORK)
		data_set_string(data_list_append(dst), "Network");
	if (*profile & ACCT_GATHER_PROFILE_TASK)
		data_set_string(data_list_append(dst), "Task");

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_SHARED)

static int DUMP_FUNC(JOB_SHARED)(const parser_t *const parser, void *obj,
				 data_t *dst, args_t *args)
{
	uint16_t *shared = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (*shared == NO_VAL16)
		return SLURM_SUCCESS;

	switch (*shared) {
	case JOB_SHARED_NONE :
		data_set_string(dst, "none");
		break;
	case JOB_SHARED_OK :
		data_set_string(dst, "shared");
		break;
	case JOB_SHARED_USER :
		data_set_string(dst, "user");
		break;
	case JOB_SHARED_MCS :
		data_set_string(dst, "mcs");
		break;
	default :
		return ESLURM_DATA_FLAGS_INVALID_TYPE;
	}

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ALLOCATED_CORES)

static int DUMP_FUNC(ALLOCATED_CORES)(const parser_t *const parser, void *obj,
				      data_t *dst, args_t *args)
{
	uint32_t *cores = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (slurm_conf.select_type_param & (CR_CORE | CR_SOCKET))
		data_set_int(dst, *cores);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(ALLOCATED_CPUS)

static int DUMP_FUNC(ALLOCATED_CPUS)(const parser_t *const parser, void *obj,
				     data_t *dst, args_t *args)
{
	uint32_t *cpus = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (slurm_conf.select_type_param & (CR_CPU))
		data_set_int(dst, *cpus);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_RES_PTR)

static int DUMP_FUNC(JOB_RES_PTR)(const parser_t *const parser, void *obj,
				  data_t *dst, args_t *args)
{
	job_resources_t **res = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(res);

	if (!*res)
		return SLURM_SUCCESS;

	return DUMP(JOB_RES, *res, dst, args);
}

static void _dump_node_res(data_t *dnodes, job_resources_t *j,
			   const size_t node_inx, const char *nodename,
			   const size_t sock_inx, size_t *bit_inx,
			   const size_t array_size)
{
	size_t bit_reps;
	data_t *dnode = data_set_dict(data_list_append(dnodes));
	data_t *dsockets = data_set_dict(data_key_set(dnode, "sockets"));
	data_t **sockets;

	sockets = xcalloc(j->sockets_per_node[sock_inx], sizeof(*sockets));

	/* per node */

	data_set_string(data_key_set(dnode, "nodename"), nodename);

	data_set_int(data_key_set(dnode, "cpus_used"), j->cpus_used[node_inx]);
	data_set_int(data_key_set(dnode, "memory_used"),
		     j->memory_used[node_inx]);
	data_set_int(data_key_set(dnode, "memory_allocated"),
		     j->memory_allocated[node_inx]);

	/* set the used cores as found */

	bit_reps =
		j->sockets_per_node[sock_inx] * j->cores_per_socket[sock_inx];
	for (size_t i = 0; i < bit_reps; i++) {
		size_t socket_inx = i / j->cores_per_socket[sock_inx];
		size_t core_inx = i % j->cores_per_socket[sock_inx];

		xassert(*bit_inx < array_size);

		if (*bit_inx >= array_size) {
			error("%s: unexpected invalid bit index:%zu/%zu",
			      __func__, *bit_inx, array_size);
			break;
		}

		if (bit_test(j->core_bitmap, *bit_inx)) {
			data_t *dcores;

			if (!sockets[socket_inx]) {
				sockets[socket_inx] = data_set_dict(
					data_key_set_int(dsockets, socket_inx));
				dcores = data_set_dict(data_key_set(
					sockets[socket_inx], "cores"));
			} else {
				dcores = data_key_get(sockets[socket_inx],
						      "cores");
			}

			if (bit_test(j->core_bitmap_used, *bit_inx)) {
				data_set_string(data_key_set_int(dcores,
								 core_inx),
						"allocated_and_in_use");
			} else {
				data_set_string(data_key_set_int(dcores,
								 core_inx),
						"allocated");
			}
		}

		(*bit_inx)++;
	}

	xfree(sockets);
}

PARSE_DISABLED(JOB_RES_NODES)

static int DUMP_FUNC(JOB_RES_NODES)(const parser_t *const parser, void *obj,
				    data_t *dst, args_t *args)
{
	job_resources_t *j = obj;
	hostlist_t hl = NULL;
	size_t bit_inx = 0;
	size_t array_size;
	size_t sock_inx = 0, sock_reps = 0;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);
	xassert(j);
	data_set_list(dst);

	/* log_job_resources() used as an example */

	if (!j->cores_per_socket || !j->nhosts) {
		/* not enough info present */
		return SLURM_SUCCESS;
	}

	hl = hostlist_create(j->nodes);
	array_size = bit_size(j->core_bitmap);

	for (size_t node_inx = 0; node_inx < j->nhosts; node_inx++) {
		char *nodename = hostlist_nth(hl, node_inx);

		if (sock_reps >= j->sock_core_rep_count[sock_inx]) {
			sock_inx++;
			sock_reps = 0;
		}
		sock_reps++;

		_dump_node_res(dst, j, node_inx, nodename, sock_inx, &bit_inx,
			       array_size);

		free(nodename);
	}

	FREE_NULL_HOSTLIST(hl);
	return SLURM_SUCCESS;
}

PARSE_DISABLED(JOB_INFO_MSG)

static int DUMP_FUNC(JOB_INFO_MSG)(const parser_t *const parser, void *obj,
				   data_t *dst, args_t *args)
{
	int rc = SLURM_SUCCESS;
	job_info_msg_t **msg = obj;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (size_t i = 0; !rc && (i < (*msg)->record_count); ++i)
		rc = DUMP(JOB_INFO, (*msg)->job_array[i], data_list_append(dst),
			  args);

	return rc;
}

PARSE_DISABLED(CONTROLLER_PING_ARRAY)

static int DUMP_FUNC(CONTROLLER_PING_ARRAY)(const parser_t *const parser,
					    void *obj, data_t *dst,
					    args_t *args)
{
	int rc = SLURM_SUCCESS;
	controller_ping_t **ping_ptr = obj;
	controller_ping_t *ping = *ping_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	data_set_list(dst);

	for (; !rc && ping && ping->hostname; ping++) {
		rc = DUMP(CONTROLLER_PING, *ping,
			  data_set_dict(data_list_append(dst)), args);
	}

	return rc;
}

PARSE_DISABLED(CONTROLLER_PING_MODE)

static int DUMP_FUNC(CONTROLLER_PING_MODE)(const parser_t *const parser,
					   void *obj, data_t *dst, args_t *args)
{
	int *mode_ptr = obj;
	int mode = *mode_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (mode == 0)
		data_set_string(dst, "primary");
	else if ((mode == 1) && (slurm_conf.control_cnt == 2))
		data_set_string(dst, "backup");
	else
		data_set_string_fmt(dst, "backup%u", mode);

	return SLURM_SUCCESS;
}

PARSE_DISABLED(CONTROLLER_PING_RESULT)

static int DUMP_FUNC(CONTROLLER_PING_RESULT)(const parser_t *const parser,
					     void *obj, data_t *dst,
					     args_t *args)
{
	bool *ping_ptr = obj;
	int ping = *ping_ptr;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(dst) == DATA_TYPE_NULL);

	if (ping)
		data_set_string(dst, "UP");
	else
		data_set_string(dst, "DOWN");

	return SLURM_SUCCESS;
}

/*
 * The following struct arrays are not following the normal Slurm style but are
 * instead being treated as piles of data instead of code.
 */
// clang-format off
#define add_parser(stype, mtype, req, field, path, need)              \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = need,                                                \
}
#define add_parser_skip(stype, field)                                 \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.skip = true,                                                 \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.type = DATA_PARSER_TYPE_INVALID,                             \
	.type_string = "skipped",                                     \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = NEED_NONE,                                           \
}
/*
 * Parser that needs the location of struct as
 * it will reference multiple fields at once.
 */
#define add_complex_parser(stype, mtype, req, path, need)             \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.ptr_offset = NO_VAL,                                         \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_NONE,                                       \
	.size = NO_VAL,                                               \
	.needs = need                                                 \
}
#define add_parser_enum_flag(stype, mtype, req, field, path,          \
			     bit, name, need)                         \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BIT,                                        \
	.flag_mask = bit,                                             \
	.flag_name = name,                                            \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = need,                                                \
}
#define add_parser_skip_enum_flag(stype, field, bit)                  \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.skip = true,                                                 \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.type = DATA_PARSER_TYPE_INVALID,                             \
	.type_string = "skipped",                                     \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BIT,                                        \
	.flag_mask = bit,                                             \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = NEED_NONE,                                           \
}
/* will never set to FALSE, only will set to TRUE if matched  */
#define add_parse_enum_bool(stype, mtype, req, field, path,           \
			    name, need)                               \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_ ## mtype,                                \
	.type_string = XSTRINGIFY(DATA_PARSER_ ## mtype),             \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BOOL,                                       \
	.flag_mask = UINT64_MAX,                                      \
	.flag_name = name,                                            \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = need,                                                \
}
#define add_parse_skip_enum_bool(stype, path)                         \
{                                                                     \
	.magic = MAGIC_PARSER,                                        \
	.ptr_offset = offsetof(stype, field),                         \
	.field_name = XSTRINGIFY(field),                              \
	.key = path,                                                  \
	.required = req,                                              \
	.type = DATA_PARSER_TYPE_INVALID,                             \
	.type_string = "skipped",                                     \
	.obj_type_string = XSTRINGIFY(stype),                         \
	.flag = FLAG_TYPE_BOOL,                                       \
	.flag_mask = UINT64_MAX,                                      \
	.size = sizeof(((stype *) NULL)->field),                      \
	.needs = NEED_NONE,                                           \
}

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, false, field, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_assoc_rec_t, mtype, true, field, path, NEED_NONE)
static const parser_t PARSER_ARRAY(ASSOC_SHORT)[] = {
	/* Identifiers required for any given association */
	add_parse_req(STRING, acct, "account"),
	add_parse(STRING, cluster, "cluster"),
	add_parse(STRING, partition, "partition"),
	add_parse_req(STRING, user, "user"),
};
#undef add_parse
#undef add_parse_req

#define add_skip(field) \
	add_parser_skip(slurmdb_assoc_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_assoc_rec_t, mtype, false, field, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_assoc_rec_t, mtype, true, field, path, needs)
/* should mirror the structure of slurmdb_assoc_rec_t */
static const parser_t PARSER_ARRAY(ASSOC)[] = {
	add_skip(accounting_list),
	add_parse_req(STRING, acct, "account", NEED_NONE),
	add_skip(assoc_next),
	add_skip(assoc_next_id),
	add_skip(bf_usage),
	add_parse(STRING, cluster, "cluster", NEED_NONE),
	add_parse(QOS_ID, def_qos_id, "default/qos", NEED_QOS), add_parser_enum_flag(slurmdb_assoc_rec_t, ASSOC_FLAG_DELETED, false, flags, "flags", ASSOC_FLAG_DELETED, "DELETED", NEED_NONE),
	add_skip(lft),
	add_parse(UINT32, grp_jobs, "max/jobs/per/count", NEED_NONE),
	add_parse(UINT32, grp_jobs_accrue, "max/jobs/per/accruing", NEED_NONE),
	add_parse(UINT32, grp_submit_jobs, "max/jobs/per/submitted", NEED_NONE),
	add_parse(TRES_STR, grp_tres, "max/tres/total", NEED_TRES),
	add_parse(TRES_STR, max_tres_mins_pj, "max/tres/minutes/per/job", NEED_TRES),
	add_parse(TRES_STR, grp_tres_mins, "max/tres/group/minutes", NEED_TRES),
	add_skip(grp_tres_mins_ctld),
	add_parse(TRES_STR, grp_tres_run_mins, "max/tres/group/active", NEED_TRES),
	add_skip(grp_tres_run_mins_ctld),
	add_skip(max_tres_mins_ctld),
	add_skip(id),
	add_parse_enum_bool(slurmdb_assoc_rec_t, ASSOC_FLAG_DEFAULT, false, is_def, "flags", "DEFAULT", NEED_NONE),
	add_parse(UINT32, max_jobs, "max/jobs/active", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue, "max/jobs/accruing", NEED_NONE),
	add_parse(UINT32, max_submit_jobs, "max/jobs/total", NEED_NONE),
	add_skip(max_tres_mins_ctld),
	add_parse(TRES_STR, max_tres_run_mins, "max/tres/minutes/total", NEED_TRES),
	add_skip(grp_tres_run_mins_ctld),
	add_parse(UINT32, grp_wall, "max/per/account/wall_clock", NEED_NONE),
	add_skip(max_tres_mins_ctld),
	add_parse(TRES_STR, max_tres_pj, "max/tres/per/job", NEED_TRES),
	add_skip(max_tres_ctld),
	add_parse(TRES_STR, max_tres_pn, "max/tres/per/node", NEED_TRES),
	add_skip(max_tres_pn_ctld),
	add_parse(UINT32, max_wall_pj, "max/jobs/per/wall_clock", NEED_NONE),
	add_parse(UINT32, min_prio_thresh, "min/priority_threshold", NEED_NONE),
	add_parse(STRING, parent_acct, "parent_account", NEED_NONE),
	add_skip(parent_id),
	add_parse(STRING, partition, "partition", NEED_NONE),
	add_parse(UINT32, priority, "priority", NEED_NONE),
	add_parse(QOS_STRING_ID_LIST, qos_list, "qos", NEED_QOS),
	add_skip(rgt),
	add_parse(UINT32, shares_raw, "shares_raw", NEED_NONE),
	/* slurmdbd should never set uid - it should always be zero */
	add_skip(uid),
	add_parse(ASSOC_USAGE_PTR, usage, "usage", NEED_NONE),
	add_parse_req(STRING, user, "user", NEED_NONE),
	add_skip(user_rec ),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

#define add_skip(field) \
	add_parser_skip(slurmdb_user_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_user_rec_t, mtype, false, field, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_user_rec_t, mtype, true, field, path, needs)
/* should mirror the structure of slurmdb_user_rec_t */
static const parser_t PARSER_ARRAY(USER)[] = {
	add_parse(ADMIN_LVL, admin_level, "administrator_level", NEED_NONE),
	add_parse(ASSOC_SHORT_LIST, assoc_list, "associations", NEED_ASSOC),
	add_parse(COORD_LIST, coord_accts, "coordinators", NEED_NONE),
	add_parse(STRING, default_acct, "default/account", NEED_NONE),
	add_parse(STRING, default_wckey, "default/wckey", NEED_NONE),
	add_parser_enum_flag(slurmdb_user_rec_t, USER_FLAG_DELETED, false, flags, "flags", SLURMDB_USER_FLAG_DELETED, "DELETED", NEED_NONE),
	add_parse_req(STRING, name, "name", NEED_NONE),
	add_skip(old_name),
	/* uid should always be 0 */
	add_skip(uid),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

#define add_skip(field) \
	add_parser_skip(slurmdb_user_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_job_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_job_rec_t  */
static const parser_t PARSER_ARRAY(JOB)[] = {
	add_parse(STRING, account, "account", NEED_NONE),
	add_parse(STRING, admin_comment, "comment/administrator", NEED_NONE),
	add_parse(UINT32, alloc_nodes, "allocation_nodes", NEED_NONE),
	add_parse(UINT32, array_job_id, "array/job_id", NEED_NONE),
	add_parse(UINT32, array_max_tasks, "array/limits/max/running/tasks", NEED_NONE),
	add_parse(STRING, array_task_str, "array/task", NEED_NONE),
	add_parse(UINT32, array_task_id, "array/task_id", NEED_NONE),
	add_parse(ASSOC_ID, associd, "association", NEED_ASSOC),
	add_parse(STRING, cluster, "cluster", NEED_NONE),
	add_parse(STRING, constraints, "constraints", NEED_NONE),
	add_parse(STRING, container, "container", NEED_NONE),
	add_parse(JOB_EXIT_CODE, derived_ec, "derived_exit_code", NEED_NONE),
	add_parse(STRING, derived_es, "comment/job", NEED_NONE),
	add_parse(UINT32, elapsed, "time/elapsed", NEED_NONE),
	add_parse(UINT32, eligible, "time/eligible", NEED_NONE),
	add_parse(UINT32, end, "time/end", NEED_NONE),
	add_parse(JOB_EXIT_CODE, exitcode, "exit_code", NEED_NONE),
	add_parser_enum_flag(slurmdb_job_rec_t, JOB_FLAG_CLEAR_SCHED, false, flags, "flags", SLURMDB_JOB_CLEAR_SCHED, "CLEAR_SCHEDULING", NEED_NONE),
	add_parser_enum_flag(slurmdb_job_rec_t, JOB_FLAG_NOTSET, false, flags, "flags", SLURMDB_JOB_FLAG_NOTSET, "NOT_SET", NEED_NONE),
	add_parser_enum_flag(slurmdb_job_rec_t, JOB_FLAG_SUBMIT, false, flags, "flags", SLURMDB_JOB_FLAG_SUBMIT, "STARTED_ON_SUBMIT", NEED_NONE),
	add_parser_enum_flag(slurmdb_job_rec_t, JOB_FLAG_SCHED, false, flags, "flags", SLURMDB_JOB_FLAG_SCHED, "STARTED_ON_SCHEDULE", NEED_NONE),
	add_parser_enum_flag(slurmdb_job_rec_t, JOB_FLAG_BACKFILL, false, flags, "flags", SLURMDB_JOB_FLAG_BACKFILL, "STARTED_ON_BACKFILL", NEED_NONE),
	add_parse(GROUP_ID, gid, "group", NEED_NONE),
	add_parse(UINT32, het_job_id, "het/job_id", NEED_NONE),
	add_parse(UINT32, het_job_offset, "het/job_offset", NEED_NONE),
	add_parse(UINT32, jobid, "job_id", NEED_NONE),
	add_parse(STRING, jobname, "name", NEED_NONE),
	add_parse(STRING, mcs_label, "mcs/label", NEED_NONE),
	add_parse(STRING, nodes, "nodes", NEED_NONE),
	add_parse(STRING, partition, "partition", NEED_NONE),
	add_parse(UINT32, priority, "priority", NEED_NONE),
	add_parse(QOS_ID, qosid, "qos", NEED_QOS),
	add_parse(UINT32, req_cpus, "required/CPUs", NEED_NONE),
	add_parse(UINT32, req_mem, "required/memory", NEED_NONE),
	add_parse(USER_ID, requid, "kill_request_user", NEED_NONE),
	add_parse(UINT32, resvid, "reservation/id", NEED_NONE),
	add_parse(UINT32, resv_name, "reservation/name", NEED_NONE),
	add_parse(UINT32, eligible, "time/start", NEED_NONE),
	add_parse(JOB_STATE, state, "state/current", NEED_NONE),
	add_parse(JOB_REASON, state_reason_prev, "state/reason", NEED_NONE),
	add_parse(UINT32, submit, "time/submission", NEED_NONE),
	add_parse(STEP_LIST, steps, "steps", NEED_NONE),
	add_parse(UINT32, suspended, "time/suspended", NEED_NONE),
	add_parse(STRING, system_comment, "comment/system", NEED_NONE),
	add_parse(UINT32, sys_cpu_sec, "time/system/seconds", NEED_NONE),
	add_parse(UINT32, sys_cpu_usec, "time/system/microseconds", NEED_NONE),
	add_parse(UINT32, timelimit, "time/limit", NEED_NONE),
	add_parse(UINT32, tot_cpu_sec, "time/total/seconds", NEED_NONE),
	add_parse(UINT32, tot_cpu_usec, "time/total/microseconds", NEED_NONE),
	add_parse(TRES_STR, tres_alloc_str, "tres/allocated", NEED_TRES),
	add_parse(TRES_STR, tres_req_str, "tres/requested", NEED_TRES),
	add_skip(uid), /* dup with user below */
	/* parse uid or user depending on which is available */
	add_complex_parser(slurmdb_job_rec_t, JOB_USER, false, "user", NEED_NONE),
	add_parse(UINT32, user_cpu_sec, "time/user/seconds", NEED_NONE),
	add_parse(UINT32, user_cpu_usec, "time/user/microseconds", NEED_NONE),
	add_parse(WCKEY_TAG, wckey, "wckey", NEED_NONE),
	add_parse(STRING, work_dir, "working_directory", NEED_NONE),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_account_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_account_rec_t */
static const parser_t PARSER_ARRAY(ACCOUNT)[] = {
	add_parse(ASSOC_SHORT_LIST, assoc_list, "associations", NEED_ASSOC),
	add_parse(COORD_LIST, coordinators, "coordinators", NEED_NONE),
	add_parse(STRING, description, "description", NEED_NONE),
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(STRING, organization, "organization", NEED_NONE),
	add_parser_enum_flag(slurmdb_account_rec_t, ACCOUNT_FLAG_DELETED, false, flags, "flags", SLURMDB_ACCT_FLAG_DELETED, "DELETED", NEED_NONE),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_accounting_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_accounting_rec_t */
static const parser_t PARSER_ARRAY(ACCOUNTING)[] = {
	add_parse(UINT64, alloc_secs, "allocated/seconds", NEED_NONE),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(UINT32, period_start, "start", NEED_NONE),
	add_parse(TRES, tres_rec, "TRES", NEED_TRES),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_coord_rec_t, mtype, false, field, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_coord_rec_t, mtype, true, field, path, needs)
/* should mirror the structure of slurmdb_coord_rec_t  */
static const parser_t PARSER_ARRAY(COORD)[] = {
	add_parse_req(STRING, name, "name", NEED_NONE),
	add_parse(UINT16, direct, "direct", NEED_NONE),
};
#undef add_parse
#undef add_parse_req

#define add_skip(field) \
	add_parser_skip(slurmdb_wckey_rec_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, false, field, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_wckey_rec_t, mtype, true, field, path, NEED_NONE)
/* should mirror the structure of slurmdb_wckey_rec_t */
static const parser_t PARSER_ARRAY(WCKEY)[] = {
	add_parse(ACCOUNTING_LIST, accounting_list, "accounting"),
	add_parse_req(STRING, cluster, "cluster"),
	add_parse(UINT32, id, "id"),
	add_parse_req(STRING, name, "name"),
	add_parse_req(STRING, user, "user"),
	add_skip(uid),
	add_parser_enum_flag(slurmdb_wckey_rec_t, WCKEY_FLAG_DELETED, false, flags, "flags", SLURMDB_WCKEY_FLAG_DELETED, "DELETED", NEED_NONE),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

#define add_skip(field) \
	add_parser_skip(slurmdb_tres_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_tres_rec_t, mtype, false, field, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_tres_rec_t, mtype, true, field, path, needs)
/* should mirror the structure of slurmdb_tres_rec_t  */
static const parser_t PARSER_ARRAY(TRES)[] = {
	add_skip(alloc_secs), /* sreport func */
	add_skip(rec_count), /* not packed */
	add_parse_req(STRING, type, "type", NEED_NONE),
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(INT64, count, "count", NEED_NONE),
};
#undef add_parse
#undef add_parse_req
#undef add_skip

#define add_skip(field) \
	add_parser_skip(slurmdb_qos_rec_t, field)
#define add_skip_flag(field, flag) \
	add_parser_skip_enum_flag(slurmdb_qos_rec_t, field, flag)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_qos_rec_t, mtype, false, field, path, needs)
#define add_parse_req(mtype, field, path, needs) \
	add_parser(slurmdb_qos_rec_t, mtype, true, field, path, needs)
/* should mirror the structure of slurmdb_qos_rec_t */
static const parser_t PARSER_ARRAY(QOS)[] = {
	add_parse(STRING, description, "description", NEED_NONE),
	add_skip_flag(preempt_mode, QOS_FLAG_BASE),
	add_skip_flag(preempt_mode, QOS_FLAG_NOTSET),
	add_skip_flag(preempt_mode, QOS_FLAG_ADD),
	add_skip_flag(preempt_mode, QOS_FLAG_REMOVE ),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PART_MIN_NODE, false, flags, "flags", QOS_FLAG_PART_MIN_NODE, "PARTITION_MINIMUM_NODE", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PART_MAX_NODE, false, flags, "flags", QOS_FLAG_PART_MAX_NODE, "PARTITION_MAXIMUM_NODE", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PART_TIME_LIMIT, false, flags, "flags", QOS_FLAG_PART_TIME_LIMIT, "PARTITION_TIME_LIMIT", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_ENFORCE_USAGE_THRES, false, flags, "flags", QOS_FLAG_ENFORCE_USAGE_THRES, "ENFORCE_USAGE_THRESHOLD", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_NO_RESERVE, false, flags, "flags", QOS_FLAG_NO_RESERVE, "NO_RESERVE", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_REQ_RESV, false, flags, "flags", QOS_FLAG_REQ_RESV, "REQUIRED_RESERVATION", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_DENY_LIMIT, false, flags, "flags", QOS_FLAG_DENY_LIMIT, "DENY_LIMIT", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_OVER_PART_QOS, false, flags, "flags", QOS_FLAG_OVER_PART_QOS, "OVERRIDE_PARTITION_QOS", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_NO_DECAY, false, flags, "flags", QOS_FLAG_NO_DECAY, "NO_DECAY", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_USAGE_FACTOR_SAFE, false, flags, "flags", QOS_FLAG_USAGE_FACTOR_SAFE, "USAGE_FACTOR_SAFE", NEED_NONE),
	add_parse(UINT32, id, "id", NEED_NONE),
	add_parse(UINT32, grace_time, "limits/grace_time", NEED_NONE),
	add_parse(UINT32, grp_jobs_accrue, "limits/max/active_jobs/accruing", NEED_NONE),
	add_parse(UINT32, grp_jobs, "limits/max/active_jobs/count", NEED_NONE),
	add_parse(TRES_STR, grp_tres, "limits/max/tres/total", NEED_TRES),
	add_skip(grp_tres_ctld), /* not packed */
	add_parse(TRES_STR, grp_tres_run_mins, "limits/max/tres/minutes/per/qos", NEED_TRES),
	add_skip(grp_tres_run_mins_ctld), /* not packed */
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(UINT32, grp_wall, "limits/max/wall_clock/per/qos", NEED_NONE),
	add_parse(FLOAT64, limit_factor, "limits/factor", NEED_NONE),
	add_parse(UINT32, max_jobs_pa, "limits/max/jobs/active_jobs/per/account", NEED_NONE),
	add_parse(UINT32, max_jobs_pu, "limits/max/jobs/active_jobs/per/user", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue_pa, "limits/max/accruing/per/account", NEED_NONE),
	add_parse(UINT32, max_jobs_accrue_pu, "limits/max/accruing/per/user", NEED_NONE),
	add_parse(UINT32, max_submit_jobs_pa, "limits/max/jobs/per/account", NEED_NONE),
	add_parse(UINT32, max_submit_jobs_pu, "limits/max/jobs/per/user", NEED_NONE),
	add_parse(TRES_STR, max_tres_mins_pj, "limits/max/tres/minutes/per/job", NEED_TRES),
	add_skip(max_tres_mins_pj_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pa, "limits/max/tres/per/account", NEED_TRES),
	add_skip(max_tres_pa_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pj, "limits/max/tres/per/job", NEED_TRES),
	add_skip(max_tres_pj_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pn, "limits/max/tres/per/node", NEED_TRES),
	add_skip(max_tres_pn_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_pu, "limits/max/tres/per/user", NEED_TRES),
	add_skip(max_tres_pu_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_run_mins_pa, "limits/max/tres/minutes/per/account", NEED_TRES),
	add_skip(max_tres_run_mins_pa_ctld), /* not packed */
	add_parse(TRES_STR, max_tres_run_mins_pu, "limits/max/tres/minutes/per/user", NEED_TRES),
	add_skip(max_tres_run_mins_pu_ctld), /* not packed */
	add_parse(UINT32, max_wall_pj, "limits/max/wall_clock/per/job", NEED_NONE),
	add_parse(UINT32, min_prio_thresh, "limits/min/priority_threshold", NEED_NONE),
	add_parse(TRES_STR, min_tres_pj, "limits/min/tres/per/job", NEED_NONE),
	add_skip(min_tres_pj_ctld), /* not packed */
	add_complex_parser(slurmdb_qos_rec_t, QOS_PREEMPT_LIST, false, "preempt/list", NEED_QOS),
	add_skip_flag(preempt_mode, PREEMPT_MODE_OFF), /* implied by empty list */
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PREEMPT_MODE_SUSPEND, false, preempt_mode, "preempt/mode", PREEMPT_MODE_SUSPEND, "SUSPEND", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PREEMPT_MODE_REQUEUE, false, preempt_mode, "preempt/mode", PREEMPT_MODE_REQUEUE, "REQUEUE", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PREEMPT_MODE_CANCEL, false, preempt_mode, "preempt/mode", PREEMPT_MODE_CANCEL, "CANCEL", NEED_NONE),
	add_parser_enum_flag(slurmdb_qos_rec_t, QOS_FLAG_PREEMPT_MODE_GANG, false, preempt_mode, "preempt/mode", PREEMPT_MODE_GANG, "GANG", NEED_NONE),
	add_parse(UINT32, preempt_exempt_time, "preempt/exempt_time", NEED_NONE),
	add_parse(UINT32, priority, "priority", NEED_NONE),
	add_skip(usage), /* not packed */
	add_parse(FLOAT64, usage_factor, "usage_factor", NEED_NONE),
	add_parse(FLOAT64, usage_thres, "usage_threshold", NEED_NONE),
	add_skip(blocked_until), /* not packed */
};
#undef add_parse
#undef add_parse_req
#undef add_skip
#undef add_skip_flag

#define add_skip(field) \
	add_parser_skip(slurmdb_step_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_step_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_step_rec_t */
static const parser_t PARSER_ARRAY(STEP)[] = {
	add_parse(UINT32, elapsed, "time/elapsed", NEED_NONE),
	add_parse(UINT32, end, "time/end", NEED_NONE),
	add_parse(JOB_EXIT_CODE, exitcode, "exit_code", NEED_NONE),
	add_skip(job_ptr), /* redundant here */
	add_parse(UINT32, nnodes, "nodes/count", NEED_NONE),
	add_parse(STRING, nodes, "nodes/range", NEED_NONE),
	add_parse(UINT32, ntasks, "tasks/count", NEED_NONE),
	add_parse(STRING, pid_str, "pid", NEED_NONE),
	add_parse(UINT32, req_cpufreq_min, "CPU/requested_frequency/min", NEED_NONE),
	add_parse(UINT32, req_cpufreq_max, "CPU/requested_frequency/max", NEED_NONE),
	add_parser_enum_flag(slurmdb_step_rec_t, STEP_FLAG_CPU_FREQ_CONSERVATIVE, false, req_cpufreq_gov, "CPU/governor", CPU_FREQ_CONSERVATIVE, "Conservative", NEED_NONE),
	add_parser_enum_flag(slurmdb_step_rec_t, STEP_FLAG_CPU_FREQ_PERFORMANCE, false, req_cpufreq_gov, "CPU/governor", CPU_FREQ_PERFORMANCE, "Performance", NEED_NONE),
	add_parser_enum_flag(slurmdb_step_rec_t, STEP_FLAG_CPU_FREQ_POWERSAVE, false, req_cpufreq_gov, "CPU/governor", CPU_FREQ_POWERSAVE, "PowerSave", NEED_NONE),
	add_parser_enum_flag(slurmdb_step_rec_t, STEP_FLAG_CPU_FREQ_ONDEMAND, false, req_cpufreq_gov, "CPU/governor", CPU_FREQ_ONDEMAND, "OnDemand", NEED_NONE),
	add_parser_enum_flag(slurmdb_step_rec_t, STEP_FLAG_CPU_FREQ_USERSPACE, false, req_cpufreq_gov, "CPU/governor", CPU_FREQ_USERSPACE, "UserSpace", NEED_NONE),
	add_parse(USER_ID, requid, "kill_request_user", NEED_NONE),
	add_parse(UINT32, start, "time/start", NEED_NONE),
	add_parse(JOB_STATE, state, "state", NEED_NONE),
	add_parse(UINT32, stats.act_cpufreq, "statistics/CPU/actual_frequency", NEED_NONE),
	add_parse(UINT32, stats.consumed_energy, "statistics/energy/consumed", NEED_NONE),
	add_parse(UINT32, step_id.job_id, "step/job_id", NEED_NONE),
	add_parse(UINT32, step_id.step_het_comp, "step/het/component", NEED_NONE),
	add_parse(STEP_ID, step_id.step_id, "step/id", NEED_NONE),
	add_parse(STRING, stepname, "step/name", NEED_NONE),
	add_parse(UINT32, suspended, "time/suspended", NEED_NONE),
	add_parse(UINT32, sys_cpu_sec, "time/system/seconds", NEED_NONE),
	add_parse(UINT32, sys_cpu_usec, "time/system/microseconds", NEED_NONE),
	add_parse(TASK_DISTRIBUTION, task_dist, "task/distribution", NEED_NONE),
	add_parse(UINT32, tot_cpu_sec, "time/total/seconds", NEED_NONE),
	add_parse(UINT32, tot_cpu_usec, "time/total/microseconds", NEED_NONE),
	add_parse(UINT32, user_cpu_sec, "time/user/seconds", NEED_NONE),
	add_parse(UINT32, user_cpu_usec, "time/user/microseconds", NEED_NONE),
	add_complex_parser(slurmdb_step_rec_t, STEP_NODES, false, "nodes/list", NEED_NONE),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_REQ_MAX, false, "tres/requested/max", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_REQ_MIN, false, "tres/requested/min", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_USAGE_MAX, false, "tres/consumed/max", NEED_TRES),
	add_complex_parser(slurmdb_step_rec_t, STEP_TRES_USAGE_MIN, false, "tres/consumed/min", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_in_ave, "tres/requested/average", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_in_tot, "tres/requested/total", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_out_ave, "tres/consumed/average", NEED_TRES),
	add_parse(TRES_STR, stats.tres_usage_out_tot, "tres/consumed/total", NEED_TRES),
	add_parse(TRES_STR, tres_alloc_str, "tres/allocated", NEED_TRES),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_stats_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_stats_rec_t */
static const parser_t PARSER_ARRAY(STATS_REC)[] = {
	add_parse(UINT32, time_start, "time_start", NEED_NONE),
	add_parse(STATS_REC_ARRAY, dbd_rollup_stats, "rollups", NEED_NONE),
	add_parse(STATS_RPC_LIST, rpc_list, "RPCs", NEED_NONE),
	add_parse(STATS_USER_LIST, user_list, "users", NEED_NONE),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t PARSER_ARRAY(STATS_USER)[] = {
	add_parse(USER_ID, id, "user", NEED_NONE),
	add_parse(UINT32, cnt, "count", NEED_NONE),
	add_parse(UINT64, time_ave, "time/average", NEED_NONE),
	add_parse(UINT64, time, "time/total", NEED_NONE),
};
#undef add_parse

#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_rpc_obj_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_rpc_obj_t */
static const parser_t PARSER_ARRAY(STATS_RPC)[] = {
	add_parse(RPC_ID, id, "rpc", NEED_NONE),
	add_parse(UINT32, cnt, "count", NEED_NONE),
	add_parse(UINT64, time_ave, "time/average", NEED_NONE),
	add_parse(UINT64, time, "time/total", NEED_NONE),
};
#undef add_parse

#define add_skip(field) \
	add_parser_skip(slurmdb_cluster_rec_t, field)
#define add_parse(mtype, field, path, needs) \
	add_parser(slurmdb_cluster_rec_t, mtype, false, field, path, needs)
/* should mirror the structure of slurmdb_cluster_rec_t */
static const parser_t PARSER_ARRAY(CLUSTER_REC)[] = {
	add_skip(classification), /* to be deprecated */
	add_skip(comm_fail_time), /* not packed */
	add_skip(control_addr), /* not packed */
	add_parse(STRING, control_host, "controller/host", NEED_NONE),
	add_parse(UINT32, control_port, "controller/port", NEED_NONE),
	add_skip(dim_size), /* BG deprecated */
	add_skip(fed), /* federation not supportted */
	add_parser_enum_flag(slurmdb_cluster_rec_t, CLUSTER_REC_FLAG_MULTSD, false, flags, "flags", CLUSTER_FLAG_MULTSD, "MULTIPLE_SLURMD", NEED_NONE),
	add_parser_enum_flag(slurmdb_cluster_rec_t, CLUSTER_REC_FLAG_FE, false, flags, "flags", CLUSTER_FLAG_FE, "FRONT_END", NEED_NONE),
	add_parser_enum_flag(slurmdb_cluster_rec_t, CLUSTER_REC_FLAG_CRAY, false, flags, "flags", CLUSTER_FLAG_CRAY, "CRAY_NATIVE", NEED_NONE),
	add_parser_enum_flag(slurmdb_cluster_rec_t, CLUSTER_REC_FLAG_FED, false, flags, "flags", CLUSTER_FLAG_FED, "FEDERATION", NEED_NONE),
	add_parser_enum_flag(slurmdb_cluster_rec_t, CLUSTER_REC_FLAG_EXT, false, flags, "flags", CLUSTER_FLAG_EXT, "EXTERNAL", NEED_NONE),
	add_skip(lock), /* not packed */
	add_parse(STRING, name, "name", NEED_NONE),
	add_parse(STRING, nodes, "nodes", NEED_NONE),
	add_parse(SELECT_PLUGIN_ID, plugin_id_select, "select_plugin", NEED_NONE),
	add_parse(ASSOC_SHORT_PTR, root_assoc, "associations/root", NEED_ASSOC),
	add_parse(UINT16, rpc_version, "rpc_version", NEED_NONE),
	add_skip(send_rpc), /* not packed */
	add_parse(TRES_STR, tres_str, "tres", NEED_TRES),
};
#undef add_parse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_cluster_accounting_rec_t, mtype, false, field, path, NEED_NONE)
/* should mirror the structure of slurmdb_cluster_accounting_rec_t */
static const parser_t PARSER_ARRAY(CLUSTER_ACCT_REC)[] = {
	add_parse(UINT64, alloc_secs, "time/allocated"),
	add_parse(UINT64, down_secs, "time/down"),
	add_parse(UINT64, idle_secs, "time/idle"),
	add_parse(UINT64, over_secs, "time/overcommitted"),
	add_parse(UINT64, pdown_secs, "time/planned_down"),
	add_parse(UINT64, period_start, "time/start"),
	add_parse(UINT64, period_start, "time/reserved"),
	add_parse(STRING, tres_rec.name, "tres/name"),
	add_parse(STRING, tres_rec.type, "tres/type"),
	add_parse(UINT32, tres_rec.id, "tres/id"),
	add_parse(UINT64, tres_rec.count, "tres/count"),
};
#undef add_parse

#define add_parse(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, false, field, path, NEED_NONE)
#define add_parse_req(mtype, field, path) \
	add_parser(slurmdb_tres_nct_rec_t, mtype, true, field, path, NEED_NONE)
/* should mirror the structure of slurmdb_tres_nct_rec_t  */
static const parser_t PARSER_ARRAY(TRES_NCT)[] = {
	add_parse_req(STRING, type, "type"),
	add_parse(STRING, name, "name"),
	add_parse(UINT32, id, "id"),
	add_parse(INT64, count, "count"),
	add_parse(INT64, task, "task"),
	add_parse(STRING, node, "node"),
};
#undef add_parse
#undef add_parse_req

#define add_skip(field) \
	add_parser_skip(slurmdb_assoc_usage_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurmdb_assoc_usage_t, mtype, false, field, path, NEED_NONE)
/* should mirror the structure of slurmdb_assoc_usage_t */
static const parser_t PARSER_ARRAY(ASSOC_USAGE)[] = {
	add_parse(UINT32, accrue_cnt, "accrue_job_count"),
	add_skip(children_list), /* not packed */
	add_skip(grp_node_bitmap), /* not packed */
	add_skip(grp_node_job_cnt), /* not packed */
	add_skip(grp_used_tres), /* not packed */
	add_skip(grp_used_tres_run_secs), /* not packed */
	add_parse(FLOAT64, grp_used_wall, "group_used_wallclock"),
	add_parse(FLOAT64, fs_factor, "fairshare_factor"),
	add_parse(UINT32, level_shares, "fairshare_shares"),
	add_skip(parent_assoc_ptr), /* not packed */
	add_parse(FLOAT64, priority_norm, "normalized_priority"),
	add_skip(fs_assoc_ptr), /* not packed */
	add_parse(FLOAT128, shares_norm, "normalized_shares"),
	add_parse(FLOAT64, usage_efctv, "effective_normalized_usage"),
	add_parse(FLOAT64, usage_norm, "normalized_usage"),
	add_parse(UINT64, usage_raw, "raw_usage"),
	add_skip(fs_assoc_ptr), /* not packed */
	add_parse(UINT32, used_jobs, "active_jobs"),
	add_parse(UINT32, used_submit_jobs, "job_count"),
	add_parse(FLOAT64, level_fs, "fairshare_level"),
	add_skip(valid_qos),
};
#undef add_parse
#undef add_skip

#define add_skip(field) \
	add_parser_skip(stats_info_response_msg_t, field)
#define add_parse(mtype, field, path) \
	add_parser(stats_info_response_msg_t, mtype, false, field, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(stats_info_response_msg_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(STATS_MSG)[] = {
	add_parse(UINT32, parts_packed, "parts_packed"),
	add_parse(INT64, req_time, "req_time"),
	add_parse(INT64, req_time_start, "req_time_start"),
	add_parse(UINT32, server_thread_count, "server_thread_count"),
	add_parse(UINT32, agent_queue_size, "agent_queue_size"),
	add_parse(UINT32, agent_count, "agent_count"),
	add_parse(UINT32, agent_thread_count, "agent_thread_count"),
	add_parse(UINT32, dbd_agent_queue_size, "dbd_agent_queue_size"),
	add_parse(UINT32, gettimeofday_latency, "gettimeofday_latency"),
	add_parse(UINT32, schedule_cycle_max, "schedule_cycle_max"),
	add_parse(UINT32, schedule_cycle_last, "schedule_cycle_last"),
	add_skip(schedule_cycle_sum),
	add_parse(UINT32, schedule_cycle_counter, "schedule_cycle_total"),
	add_cparse(STATS_MSG_CYCLE_MEAN, "schedule_cycle_mean"),
	add_cparse(STATS_MSG_CYCLE_MEAN_DEPTH, "schedule_cycle_mean_depth"),
	add_cparse(STATS_MSG_CYCLE_PER_MIN, "schedule_cycle_per_minute"),
	add_skip(schedule_cycle_counter),
	add_skip(schedule_cycle_depth),
	add_parse(UINT32, schedule_queue_len, "schedule_queue_length"),
	add_parse(UINT32, jobs_submitted, "jobs_submitted"),
	add_parse(UINT32, jobs_started, "jobs_started"),
	add_parse(UINT32, jobs_completed, "jobs_completed"),
	add_parse(UINT32, jobs_canceled, "jobs_canceled"),
	add_parse(UINT32, jobs_failed, "jobs_failed"),
	add_parse(UINT32, jobs_pending, "jobs_pending"),
	add_parse(UINT32, jobs_running, "jobs_running"),
	add_parse(INT64, job_states_ts, "job_states_ts"),
	add_parse(UINT32, bf_backfilled_jobs, "bf_backfilled_jobs"),
	add_parse(UINT32, bf_last_backfilled_jobs, "bf_last_backfilled_jobs"),
	add_parse(UINT32, bf_backfilled_het_jobs, "bf_backfilled_het_jobs"),
	add_parse(UINT32, bf_cycle_counter, "bf_cycle_counter"),
	add_cparse(STATS_MSG_BF_CYCLE_MEAN, "bf_cycle_mean"),
	add_cparse(STATS_MSG_BF_DEPTH_MEAN, "bf_depth_mean"),
	add_cparse(STATS_MSG_BF_DEPTH_MEAN_TRY, "bf_depth_mean_try"),
	add_parse(UINT64, bf_cycle_sum, "bf_cycle_sum"),
	add_parse(UINT32, bf_cycle_last, "bf_cycle_last"),
	add_parse(UINT32, bf_last_depth, "bf_last_depth"),
	add_parse(UINT32, bf_last_depth_try, "bf_last_depth_try"),
	add_parse(UINT32, bf_depth_sum, "bf_depth_sum"),
	add_parse(UINT32, bf_depth_try_sum, "bf_depth_try_sum"),
	add_parse(UINT32, bf_queue_len, "bf_queue_len"),
	add_cparse(STATS_MSG_BF_QUEUE_LEN_MEAN, "bf_queue_len_mean"),
	add_parse(UINT32, bf_queue_len_sum, "bf_queue_len_sum"),
	add_parse(UINT32, bf_table_size, "bf_table_size"),
	add_skip(bf_table_size_sum),
	add_cparse(STATS_MSG_BF_QUEUE_LEN_MEAN, "bf_table_size_mean"),
	add_parse(INT64, bf_when_last_cycle, "bf_when_last_cycle"),
	add_cparse(STATS_MSG_BF_ACTIVE, "bf_active"),
	add_skip(rpc_type_size),
	add_cparse(STATS_MSG_RPCS_BY_TYPE, "rpcs_by_message_type"),
	add_skip(rpc_type_id), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_type_cnt), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_type_time), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_cparse(STATS_MSG_RPCS_BY_USER, "rpcs_by_user"),
	add_skip(rpc_user_size), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_id), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_cnt), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_user_time), /* handled by STATS_MSG_RPCS_BY_TYPE */
	add_skip(rpc_queue_type_count), /* TODO: implement */
	add_skip(rpc_queue_type_id ), /* TODO: implement */
	add_skip(rpc_queue_count), /* TODO: implement */
	add_skip(rpc_dump_count), /* TODO: implement */
	add_skip(rpc_dump_types), /* TODO: implement */
	add_skip(rpc_dump_hostlist), /* TODO: implement */
};
#undef add_parse
#undef add_cparse
#undef add_skip

#define add_skip(field) \
	add_parser_skip(node_info_t, field)
#define add_parse(mtype, field, path) \
	add_parser(node_info_t, mtype, false, field, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(node_info_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(NODE)[] = {
	add_parse(STRING, arch, "architecture"),
	add_parse(STRING, bcast_address, "burstbuffer_network_address"),
	add_parse(UINT16, boards, "boards"),
	add_parse(UINT64, boot_time, "boot_time"),
	add_skip(cluster_name), /* intentionally omitted */
	add_parse(UINT16, cores, "cores"),
	add_skip(core_spec_cnt), /* intentionally omitted */
	add_parse(UINT32, cpu_bind, "cpu_binding"),
	add_parse(UINT32, cpu_load, "cpu_load"),
	add_parse(UINT64, free_mem, "free_mem"),
	add_parse(UINT16, cpus, "cpus"),
	add_skip(cpus_efctv),
	add_skip(cpu_spec_list), /* intentionally omitted */
	add_skip(energy), /* intentionally omitted */
	add_skip(ext_sensors), /* intentionally omitted */
	add_skip(power), /* intentionally omitted */
	add_parse(STRING, extra, "extra"),
	add_parse(STRING, features, "features"),
	add_parse(STRING, features_act, "active_features"),
	add_parse(STRING, gres, "gres"),
	add_parse(STRING, gres_drain, "gres_drained"),
	add_parse(STRING, gres_used, "gres_used"),
	add_skip(last_busy),
	add_parse(STRING, mcs_label, "mcs_label"),
	add_skip(mem_spec_limit), /* intentionally omitted */
	add_parse(STRING, name, "name"),
	add_parse(NODE_BASE_STATE, next_state, "next_state_after_reboot"),
	add_parser_enum_flag(node_info_t, NODE_STATE_CLOUD, false, node_state, "next_state_after_reboot_flags", NODE_STATE_CLOUD, "CLOUD", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_COMPLETING, false, node_state, "next_state_after_reboot_flags", NODE_STATE_COMPLETING, "COMPLETING", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DRAIN, false, node_state, "next_state_after_reboot_flags", NODE_STATE_DRAIN, "DRAIN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DYNAMIC_FUTURE, false, node_state, "next_state_after_reboot_flags", NODE_STATE_DYNAMIC_FUTURE, "DYNAMIC_FUTURE", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DYNAMIC_NORM, false, node_state, "next_state_after_reboot_flags", NODE_STATE_DYNAMIC_NORM, "DYNAMIC_NORM", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_INVALID_REG, false, node_state, "next_state_after_reboot_flags", NODE_STATE_INVALID_REG, "INVALID_REG", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_FAIL, false, node_state, "next_state_after_reboot_flags", NODE_STATE_FAIL, "FAIL", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_MAINT, false, node_state, "snext_state_after_reboot_flagstate_flags", NODE_STATE_MAINT, "MAINTENANCE", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWER_DOWN, false, node_state, "next_state_after_reboot_flags", NODE_STATE_POWER_DOWN, "POWER_DOWN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWER_UP, false, node_state, "next_state_after_reboot_flags", NODE_STATE_POWER_UP, "POWER_UP", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_NET, false, node_state, "next_state_after_reboot_flags", NODE_STATE_NET, "PERFCTRS", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERED_DOWN, false, node_state, "next_state_after_reboot_flags", NODE_STATE_POWERED_DOWN, "POWERED_DOWN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_REQUESTED, false, node_state, "next_state_after_reboot_flags", NODE_STATE_REBOOT_REQUESTED, "REBOOT_REQUESTED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_ISSUED, false, node_state, "next_state_after_reboot_flags", NODE_STATE_REBOOT_ISSUED, "REBOOT_ISSUED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_ISSUED, false, node_state, "next_state_after_reboot_flags", NODE_STATE_REBOOT_ISSUED, "REBOOT_ISSUED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_RES, false, node_state, "next_state_after_reboot_flags", NODE_STATE_RES, "RESERVED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_RESUME, false, node_state, "next_state_after_reboot_flags", NODE_RESUME, "RESUME", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_NO_RESPOND, false, node_state, "next_state_after_reboot_flags", NODE_STATE_NO_RESPOND, "NOT_RESPONDING", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_PLANNED, false, node_state, "next_state_after_reboot_flags", NODE_STATE_PLANNED, "PLANNED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERING_UP, false, node_state, "next_state_after_reboot_flags", NODE_STATE_POWERING_UP, "POWERING_UP", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERING_DOWN, false, node_state, "next_state_after_reboot_flags", NODE_STATE_POWERING_DOWN, "POWERING_DOWN", NEED_NONE),
	add_parse(STRING, node_addr, "address"),
	add_parse(STRING, node_hostname, "hostname"),
	add_parse(NODE_BASE_STATE, node_state, "state"),
	add_parser_enum_flag(node_info_t, NODE_STATE_CLOUD, false, node_state, "state_flags", NODE_STATE_CLOUD, "CLOUD", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_COMPLETING, false, node_state, "state_flags", NODE_STATE_COMPLETING, "COMPLETING", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DRAIN, false, node_state, "state_flags", NODE_STATE_DRAIN, "DRAIN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DYNAMIC_FUTURE, false, node_state, "state_flags", NODE_STATE_DYNAMIC_FUTURE, "DYNAMIC_FUTURE", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_DYNAMIC_NORM, false, node_state, "state_flags", NODE_STATE_DYNAMIC_NORM, "DYNAMIC_NORM", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_INVALID_REG, false, node_state, "state_flags", NODE_STATE_INVALID_REG, "INVALID_REG", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_FAIL, false, node_state, "state_flags", NODE_STATE_FAIL, "FAIL", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_MAINT, false, node_state, "state_flags", NODE_STATE_MAINT, "MAINTENANCE", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWER_DOWN, false, node_state, "state_flags", NODE_STATE_POWER_DOWN, "POWER_DOWN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWER_UP, false, node_state, "state_flags", NODE_STATE_POWER_UP, "POWER_UP", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_NET, false, node_state, "state_flags", NODE_STATE_NET, "PERFCTRS", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERED_DOWN, false, node_state, "state_flags", NODE_STATE_POWERED_DOWN, "POWERED_DOWN", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_REQUESTED, false, node_state, "state_flags", NODE_STATE_REBOOT_REQUESTED, "REBOOT_REQUESTED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_ISSUED, false, node_state, "state_flags", NODE_STATE_REBOOT_ISSUED, "REBOOT_ISSUED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_REBOOT_ISSUED, false, node_state, "state_flags", NODE_STATE_REBOOT_ISSUED, "REBOOT_ISSUED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_RES, false, node_state, "state_flags", NODE_STATE_RES, "RESERVED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_RESUME, false, node_state, "state_flags", NODE_RESUME, "RESUME", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_NO_RESPOND, false, node_state, "state_flags", NODE_STATE_NO_RESPOND, "NOT_RESPONDING", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_PLANNED, false, node_state, "state_flags", NODE_STATE_PLANNED, "PLANNED", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERING_UP, false, node_state, "state_flags", NODE_STATE_POWERING_UP, "POWERING_UP", NEED_NONE),
	add_parser_enum_flag(node_info_t, NODE_STATE_POWERING_DOWN, false, node_state, "state_flags", NODE_STATE_POWERING_DOWN, "POWERING_DOWN", NEED_NONE),
	add_parse(STRING, os, "operating_system"),
	add_parse(USER_ID, owner, "owner"),
	add_parse(CSV_LIST, partitions, "partitions"),
	add_parse(UINT16, port, "port"),
	add_parse(UINT16, real_memory, "real_memory"),
	add_parse(STRING, comment, "comment"),
	add_parse(STRING, reason, "reason"),
	add_parse(UINT64, reason_time, "reason_changed_at"),
	add_parse(USER_ID, reason_uid, "reason_set_by_user"),
	add_cparse(NODE_SELECT_ALLOC_MEMORY, "alloc_memory"),
	add_cparse(NODE_SELECT_ALLOC_CPUS, "alloc_cpus"),
	add_cparse(NODE_SELECT_ALLOC_IDLE_CPUS, "alloc_idle_cpus"),
	add_cparse(NODE_SELECT_TRES_USED, "tres_used"),
	add_cparse(NODE_SELECT_TRES_WEIGHTED, "tres_weighted"),
	add_parse(UINT64, slurmd_start_time, "slurmd_start_time"),
	add_parse(UINT16, sockets, "sockets"),
	add_parse(UINT16, threads, "threads"),
	add_parse(UINT32, tmp_disk, "temporary_disk"),
	add_parse(UINT32, weight, "weight"),
	add_parse(STRING, tres_fmt_str, "tres"),
	add_parse(STRING, version, "version"),
};
#undef add_parse
#undef add_cparse
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(slurm_license_info_t, mtype, false, field, path, NEED_NONE)
static const parser_t PARSER_ARRAY(LICENSE)[] = {
	add_parse(STRING, name, "LicenseName"),
	add_parse(UINT32, total, "Total"),
	add_parse(UINT32, in_use, "Used"),
	add_parse(UINT32, available, "Free"),
	add_parse_enum_bool(slurm_license_info_t, LICENSE_FLAG_REMOTE, false, remote, "flags", "REMOTE", NEED_NONE),
	add_parse(UINT32, reserved, "Reserved"),
};
#undef add_parse

#define add_skip(field) \
	add_parser_skip(slurm_job_info_t, field)
#define add_parse(mtype, field, path) \
	add_parser(slurm_job_info_t, mtype, false, field, path, NEED_NONE)
#define add_bit_flag(flag) \
	add_parser_enum_flag(slurm_job_info_t, JOB_INFO_FLAG_ ## flag, false, bitflags, "flags", flag, XSTRINGIFY(flag), NEED_NONE)
#define add_show_flag(flag) \
	add_parser_enum_flag(slurm_job_info_t, JOB_INFO_FLAG_ ## flag, false, show_flags, "show_flags", flag, XSTRINGIFY(flag), NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(slurm_job_info_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(JOB_INFO)[] = {
	add_parse(STRING, account, "account"),
	add_parse(UINT64, accrue_time, "accrue_time"),
	add_parse(STRING, admin_comment, "admin_comment"),
	add_skip(alloc_node),
	add_skip(alloc_sid),
	add_parse(UINT32_NO_VAL, array_job_id, "array_job_id"),
	add_parse(UINT32_NO_VAL, array_task_id, "array_task_id"),
	add_parse(UINT32_NO_VAL, array_max_tasks, "array_max_tasks"),
	add_parse(STRING, array_task_str, "array_task_string"),
	add_parse(UINT32, assoc_id, "association_id"),
	add_parse(STRING, batch_features, "batch_features"),
	add_parse(BOOL, batch_flag, "batch_flag"),
	add_parse(STRING, batch_host, "batch_host"),
	add_bit_flag(KILL_INV_DEP),
	add_bit_flag(NO_KILL_INV_DEP),
	add_bit_flag(HAS_STATE_DIR),
	add_bit_flag(BACKFILL_TEST),
	add_bit_flag(GRES_ENFORCE_BIND),
	add_bit_flag(TEST_NOW_ONLY),
	add_bit_flag(SPREAD_JOB),
	add_bit_flag(USE_MIN_NODES),
	add_bit_flag(JOB_KILL_HURRY),
	add_bit_flag(TRES_STR_CALC),
	add_bit_flag(SIB_JOB_FLUSH),
	add_bit_flag(HET_JOB_FLAG),
	add_bit_flag(JOB_CPUS_SET),
	add_bit_flag(TOP_PRIO_TMP),
	add_bit_flag(JOB_ACCRUE_OVER),
	add_bit_flag(GRES_DISABLE_BIND),
	add_bit_flag(JOB_WAS_RUNNING),
	add_bit_flag(JOB_MEM_SET),
	add_bit_flag(JOB_RESIZED),
	add_skip(boards_per_node),
	add_parse(STRING, burst_buffer, "burst_buffer"),
	add_parse(STRING, burst_buffer_state, "burst_buffer_state"),
	add_parse(STRING, cluster, "cluster"),
	add_parse(STRING, cluster_features, "cluster_features"),
	add_parse(STRING, command, "command"),
	add_parse(STRING, comment, "comment"),
	add_parse(STRING, container, "container"),
	add_parse(BOOL16_NO_VAL, contiguous, "contiguous"),
	add_parse(CORE_SPEC, core_spec, "core_spec"),
	add_parse(THREAD_SPEC, core_spec, "thread_spec"),
	add_parse(UINT16_NO_VAL, cores_per_socket, "cores_per_socket"),
	add_parse(FLOAT64_NO_VAL, billable_tres, "billable_tres"),
	add_parse(UINT16_NO_VAL, cpus_per_task, "cpus_per_task"),
	add_parse(UINT32_NO_VAL, cpu_freq_min, "cpu_frequency_minimum"),
	add_parse(UINT32_NO_VAL, cpu_freq_max, "cpu_frequency_maximum"),
	add_parse(UINT32_NO_VAL, cpu_freq_gov, "cpu_frequency_governor"),
	add_parse(STRING, cpus_per_tres, "cpus_per_tres"),
	add_parse(STRING, cronspec, "cron"),
	add_parse(UINT64, deadline, "deadline"),
	add_parse(UINT32_NO_VAL, delay_boot, "delay_boot"),
	add_parse(STRING, dependency, "dependency"),
	add_parse(UINT32, derived_ec, "derived_exit_code"),
	add_parse(UINT64, eligible_time, "eligible_time"),
	add_parse(UINT64, end_time, "end_time"),
	add_parse(STRING, exc_nodes, "excluded_nodes"),
	add_skip(exc_node_inx),
	add_parse(UINT32, exit_code, "exit_code"),
	add_parse(STRING, features, "features"),
	add_parse(STRING, fed_origin_str, "federation_origin"),
	add_parse(STRING, fed_siblings_active_str, "federation_siblings_active"),
	add_parse(STRING, fed_siblings_viable_str, "federation_siblings_viable"),
	add_skip(gres_detail_cnt),
	add_cparse(JOB_INFO_GRES_DETAIL, "gres_detail"),
	add_parse(UINT32, group_id, "group_id"),
	add_parse(GROUP_ID, group_id, "group_name"),
	add_parse(UINT32_NO_VAL, het_job_id, "het_job_id"),
	add_parse(STRING, het_job_id_set, "het_job_id_set"),
	add_parse(UINT32_NO_VAL, het_job_offset, "het_job_offset"),
	add_parse(UINT32_NO_VAL, job_id, "job_id"),
	add_parse(JOB_RES_PTR, job_resrcs, "job_resources"),
	add_parse(JOB_STATE, job_state, "job_state"),
	add_parse(UINT64, last_sched_eval, "last_sched_evaluation"),
	add_parse(STRING, licenses, "licenses"),
	add_skip(mail_type),
	add_parse(STRING, mail_user, "mail_user"),
	add_parse(UINT32_NO_VAL, max_cpus, "max_cpus"),
	add_parse(UINT32_NO_VAL, max_nodes, "max_nodes"),
	add_parse(STRING, mcs_label, "mcs_label"),
	add_parse(STRING, mem_per_tres, "memory_per_tres"),
	add_parse(STRING, name, "name"),
	add_skip(network),
	add_parse(STRING, nodes, "nodes"),
	add_parse(NICE, nice, "nice"),
	add_parse(UINT16_NO_VAL, ntasks_per_core, "tasks_per_core"),
	add_parse(UINT16_NO_VAL, ntasks_per_tres, "tasks_per_tres"),
	add_parse(UINT16_NO_VAL, ntasks_per_node, "tasks_per_node"),
	add_parse(UINT16_NO_VAL, ntasks_per_socket, "tasks_per_socket"),
	add_parse(UINT16_NO_VAL, ntasks_per_board, "tasks_per_board"),
	add_parse(UINT32_NO_VAL, num_cpus, "cpus"),
	add_parse(UINT32_NO_VAL, num_nodes, "node_count"),
	add_parse(UINT32_NO_VAL, num_tasks, "tasks"),
	add_parse(STRING, partition, "partition"),
	add_parse(STRING, prefer, "prefer"),
	add_parse(JOB_MEM_PER_CPU, pn_min_memory, "memory_per_cpu"),
	add_parse(JOB_MEM_PER_NODE, pn_min_memory, "memory_per_node"),
	add_parse(UINT16_NO_VAL, pn_min_cpus, "minimum_cpus_per_node"),
	add_parse(UINT32_NO_VAL, pn_min_tmp_disk, "minimum_tmp_disk_per_node"),
	add_skip(power_flags),
	add_parse(UINT64, preempt_time, "preempt_time"),
	add_parse(UINT64, preemptable_time, "preemptable_time"),
	add_parse(UINT64, pre_sus_time, "pre_sus_time"),
	add_parse(UINT32_NO_VAL, priority, "priority"),
	add_parse(ACCT_GATHER_PROFILE, profile, "profile"),
	add_parse(QOS_NAME, qos, "qos"),
	add_parse(BOOL, reboot, "reboot"),
	add_parse(STRING, req_nodes, "required_nodes"),
	add_skip(req_node_inx),
	add_parse(UINT32, req_switch, "minimum_switches"),
	add_parse(UINT16, requeue, "requeue"),
	add_parse(UINT64, resize_time, "resize_time"),
	add_parse(UINT16, restart_cnt, "restart_cnt"),
	add_parse(STRING, resv_name, "resv_name"),
	add_skip(sched_nodes),
	add_skip(select_jobinfo),
	add_parse(STRING, selinux_context, "selinux_context"),
	add_parse(JOB_SHARED, shared, "shared"),
	add_show_flag(SHOW_ALL),
	add_show_flag(SHOW_DETAIL),
	add_show_flag(SHOW_MIXED),
	add_show_flag(SHOW_LOCAL),
	add_show_flag(SHOW_SIBLING),
	add_show_flag(SHOW_FEDERATION),
	add_show_flag(SHOW_FUTURE),
	add_parse(UINT16, sockets_per_board, "sockets_per_board"),
	add_parse(UINT16_NO_VAL, sockets_per_node, "sockets_per_node"),
	add_parse(UINT64, start_time, "start_time"),
	add_skip(start_protocol_ver),
	add_parse(STRING, state_desc, "state_description"),
	add_parse(JOB_STATE, state_reason, "state_reason"),
	add_parse(STRING, std_err, "standard_error"),
	add_parse(STRING, std_in, "standard_input"),
	add_parse(STRING, std_out, "standard_output"),
	add_parse(UINT64, submit_time, "submit_time"),
	add_parse(UINT64, suspend_time, "suspend_time"),
	add_parse(STRING, system_comment, "system_comment"),
	add_parse(STRING, container, "container"),
	add_parse(UINT32_NO_VAL, time_limit, "time_limit"),
	add_parse(UINT32_NO_VAL, time_min, "time_minimum"),
	add_parse(UINT16_NO_VAL, threads_per_core, "threads_per_core"),
	add_parse(STRING, tres_bind, "tres_bind"),
	add_parse(STRING, tres_freq, "tres_freq"),
	add_parse(STRING, tres_per_job, "tres_per_job"),
	add_parse(STRING, tres_per_node, "tres_per_node"),
	add_parse(STRING, tres_per_socket, "tres_per_socket"),
	add_parse(STRING, tres_per_task, "tres_per_task"),
	add_parse(STRING, tres_req_str, "tres_req_str"),
	add_parse(STRING, tres_alloc_str, "tres_alloc_str"),
	add_parse(UINT32, user_id, "user_id"),
	add_parse(USER_ID, user_id, "user_name"),
	add_skip(wait4switch),
	add_parse(STRING, wckey, "wckey"),
	add_parse(STRING, work_dir, "current_working_directory"),
};
#undef add_parse
#undef add_cparse
#undef add_bit_flag
#undef add_show_flag
#undef add_skip

#define add_parse(mtype, field, path) \
	add_parser(job_resources_t, mtype, false, field, path, NEED_NONE)
#define add_cparse(mtype, path) \
	add_complex_parser(job_resources_t, mtype, false, path, NEED_NONE)
static const parser_t PARSER_ARRAY(JOB_RES)[] = {
	add_parse(STRING, nodes, "nodes"),
	add_parse(ALLOCATED_CORES, ncpus, "allocated_cores"),
	add_parse(ALLOCATED_CPUS, ncpus, "allocated_cpus"),
	add_parse(UINT32, nhosts, "allocated_hosts"),
	add_cparse(JOB_RES_NODES, "allocated_nodes"),
};
#undef add_parse
#undef add_cparse

#define add_parse(mtype, field, path) \
	add_parser(controller_ping_t, mtype, false, field, path, NEED_NONE)
static const parser_t PARSER_ARRAY(CONTROLLER_PING)[] = {
	add_parse(STRING, hostname, "hostname"),
	add_parse(CONTROLLER_PING_RESULT, pinged, "pinged"),
	add_parse(UINT32, latency, "latency"),
	add_parse(CONTROLLER_PING_MODE, offset, "mode"),
};
#undef add_parse

#undef add_complex_parser
#undef add_parser_enum_flag
#undef add_parse_enum_bool

/* add parser array (for struct) */
#define addpa(typev, typet)                                                    \
	{                                                                      \
		.magic = MAGIC_PARSER,                                         \
		.type = DATA_PARSER_##typev,                                   \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(typet),                          \
		.size = sizeof(typet),                                         \
		.needs = NEED_NONE,                                            \
		.fields = PARSER_ARRAY(typev),                                 \
		.field_count = ARRAY_SIZE(PARSER_ARRAY(typev)),                \
		.flag = FLAG_TYPE_NONE,                                        \
	}
/* add parser for List */
#define addpl(typev, typel, delf, addf, need)                                  \
	{                                                                      \
		.magic = MAGIC_PARSER, .type = DATA_PARSER_##typev,            \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.ptr_offset = NO_VAL,                                          \
		.obj_type_string = XSTRINGIFY(List),                           \
		.list_type = DATA_PARSER_##typel,                              \
		.list_del_func = delf,                                         \
		.list_new_func = addf,                                         \
		.size = sizeof(List),                                          \
		.needs = need,                                                 \
		.flag = FLAG_TYPE_NONE,                                        \
	}
/* add parser for simple type */
#define addps(typev, stype, need)                                              \
	{                                                                      \
		.magic = MAGIC_PARSER, .type = DATA_PARSER_##typev,            \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(stype),                          \
		.size = sizeof(stype),                                         \
		.needs = need,                                                 \
		.parse = PARSE_FUNC(typev),                                    \
		.dump = DUMP_FUNC(typev),                                      \
		.flag = FLAG_TYPE_NONE,                                        \
	}
/* add parser for complex type */
#define addpc(typev, typet, need)                                              \
	{                                                                      \
		.magic = MAGIC_PARSER, .type = DATA_PARSER_##typev,            \
		.type_string = XSTRINGIFY(DATA_PARSER_ ## typev),              \
		.obj_type_string = XSTRINGIFY(typet),                          \
		.size = sizeof(typet),                                         \
		.needs = need,                                                 \
		.parse = PARSE_FUNC(typev),                                    \
		.dump = DUMP_FUNC(typev),                                      \
		.flag = FLAG_TYPE_NONE,                                        \
	}
static const parser_t parsers[] = {
	/* Simple type parsers */
	addps(STRING, char *, NEED_NONE),
	addps(UINT32, uint32_t, NEED_NONE),
	addps(UINT32_NO_VAL, uint32_t, NEED_NONE),
	addps(UINT64, uint64_t, NEED_NONE),
	addps(UINT16, uint16_t, NEED_NONE),
	addps(UINT16_NO_VAL, uint16_t, NEED_NONE),
	addps(INT64, int64_t, NEED_NONE),
	addps(FLOAT128, long double, NEED_NONE),
	addps(FLOAT64, double, NEED_NONE),
	addps(FLOAT64_NO_VAL, double, NEED_NONE),
	addps(BOOL, uint8_t, NEED_NONE),
	addps(BOOL16, uint8_t, NEED_NONE),
	addps(BOOL16_NO_VAL, uint8_t, NEED_NONE),
	addps(QOS_NAME, char *, NEED_QOS),
	addps(QOS_ID, uint32_t, NEED_NONE),
	addps(QOS_STRING_ID_LIST, List, NEED_NONE),
	addps(JOB_EXIT_CODE, int32_t, NEED_NONE),
	addps(JOB_USER, slurmdb_job_rec_t, NEED_NONE),
	addps(ADMIN_LVL, uint16_t, NEED_NONE),
	addps(ASSOC_ID, uint32_t, NEED_NONE),
	addps(STATS_REC_ARRAY, slurmdb_stats_rec_t, NEED_NONE),
	addps(RPC_ID, slurmdbd_msg_type_t, NEED_NONE),
	addps(CLUSTER_ACCT_REC, slurmdb_cluster_accounting_rec_t, NEED_NONE),
	addps(SELECT_PLUGIN_ID, int, NEED_NONE),
	addps(TASK_DISTRIBUTION, uint32_t, NEED_NONE),
	addps(STEP_ID, uint32_t, NEED_NONE),
	addps(WCKEY_TAG, uint32_t, NEED_NONE),
	addps(GROUP_ID, gid_t, NEED_NONE),
	addps(JOB_REASON, uint32_t, NEED_NONE),
	addps(JOB_STATE, uint32_t, NEED_NONE),
	addps(USER_ID, uid_t, NEED_NONE),
	addps(TRES_STR, char *, NEED_TRES),
	addps(ASSOC_SHORT_PTR, slurmdb_assoc_rec_t *, NEED_NONE),
	addps(ASSOC_USAGE_PTR, slurmdb_assoc_usage_t *, NEED_NONE),
	addps(NODE_BASE_STATE, uint32_t, NEED_NONE),
	addps(CSV_LIST, char *, NEED_NONE),
	addps(LICENSES, license_info_msg_t *, NEED_NONE),
	addps(CORE_SPEC, uint16_t, NEED_NONE),
	addps(THREAD_SPEC, uint16_t, NEED_NONE),
	addps(NICE, uint32_t, NEED_NONE),
	addps(JOB_MEM_PER_CPU, uint64_t, NEED_NONE),
	addps(JOB_MEM_PER_NODE, uint64_t, NEED_NONE),
	addps(ACCT_GATHER_PROFILE, uint32_t, NEED_NONE),
	addps(JOB_SHARED, uint16_t, NEED_NONE),
	addps(ALLOCATED_CORES, uint32_t, NEED_NONE),
	addps(ALLOCATED_CPUS, uint32_t, NEED_NONE),
	addps(JOB_RES_PTR, job_resources_t *, NEED_NONE),
	addps(CONTROLLER_PING_MODE, char *, NEED_NONE),
	addps(CONTROLLER_PING_RESULT, char *, NEED_NONE),
	addps(CONTROLLER_PING_ARRAY, controller_ping_t *, NEED_NONE),

	/* Complex type parsers */
	addpc(QOS_PREEMPT_LIST, slurmdb_qos_rec_t, NEED_QOS),
	addpc(STEP_NODES, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_REQ_MAX, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_REQ_MIN, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_USAGE_MAX, slurmdb_step_rec_t, NEED_TRES),
	addpc(STEP_TRES_USAGE_MIN, slurmdb_step_rec_t, NEED_TRES),
	addpc(STATS_MSG_CYCLE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_CYCLE_MEAN_DEPTH, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_CYCLE_PER_MIN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_CYCLE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_DEPTH_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_DEPTH_MEAN_TRY, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_QUEUE_LEN_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_TABLE_SIZE_MEAN, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_BF_ACTIVE, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_RPCS_BY_TYPE, stats_info_response_msg_t, NEED_NONE),
	addpc(STATS_MSG_RPCS_BY_USER, stats_info_response_msg_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_MEMORY, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_CPUS, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_ALLOC_IDLE_CPUS, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_TRES_USED, node_info_t, NEED_NONE),
	addpc(NODE_SELECT_TRES_WEIGHTED, node_info_t, NEED_NONE),
	addpc(NODES, node_info_msg_t, NEED_NONE),
	addpc(JOB_INFO_GRES_DETAIL, slurm_job_info_t, NEED_NONE),
	addpc(JOB_RES_NODES, job_resources_t, NEED_NONE),
	addpc(JOB_INFO_MSG, job_info_msg_t *, NEED_NONE),

	/* Array of parsers */
	addpa(ASSOC_SHORT, slurmdb_assoc_rec_t),
	addpa(ASSOC, slurmdb_assoc_rec_t),
	addpa(USER, slurmdb_user_rec_t),
	addpa(JOB, slurmdb_job_rec_t),
	addpa(STEP, slurmdb_step_rec_t),
	addpa(ACCOUNT, slurmdb_account_rec_t),
	addpa(ACCOUNTING, slurmdb_accounting_rec_t),
	addpa(COORD, slurmdb_coord_rec_t),
	addpa(WCKEY, slurmdb_wckey_rec_t),
	addpa(TRES, slurmdb_tres_rec_t),
	addpa(TRES_NCT, slurmdb_tres_nct_rec_t),
	addpa(QOS, slurmdb_qos_rec_t),
	addpa(STATS_REC, slurmdb_stats_rec_t),
	addpa(CLUSTER_REC, slurmdb_cluster_rec_t),
	addpa(CLUSTER_ACCT_REC, slurmdb_cluster_accounting_rec_t),
	addpa(ASSOC_USAGE, slurmdb_assoc_usage_t),
	addpa(STATS_RPC, slurmdb_rpc_obj_t),
	addpa(STATS_USER, slurmdb_rpc_obj_t),
	addpa(STATS_MSG, stats_info_response_msg_t),
	addpa(NODE, node_info_t),
	addpa(LICENSE, slurm_license_info_t),
	addpa(JOB_INFO, slurm_job_info_t),
	addpa(JOB_RES, job_resources_t),
	addpa(CONTROLLER_PING, controller_ping_t),

	/* List parsers */
	addpl(QOS_LIST, QOS, slurmdb_destroy_qos_rec, create_qos_rec_obj, NEED_QOS),
	addpl(QOS_NAME_LIST, QOS_NAME, xfree_ptr, create_parser_list_obj, NEED_QOS),
	addpl(QOS_ID_LIST, QOS_ID, xfree_ptr, create_parser_list_obj, NEED_QOS),
	addpl(QOS_STRING_ID_LIST, QOS_STRING_ID, xfree_ptr, create_qos_rec_obj, NEED_QOS),
	addpl(USER_LIST, USER, slurmdb_destroy_user_rec, create_user_rec_obj, NEED_NONE),
	addpl(WCKEY_LIST, WCKEY, slurmdb_destroy_wckey_rec, create_wckey_rec_obj, NEED_NONE),
	addpl(ACCOUNT_LIST, ACCOUNT, slurmdb_destroy_account_rec, create_parser_list_obj, NEED_NONE),
	addpl(ACCOUNTING_LIST, ACCOUNTING, slurmdb_destroy_accounting_rec, create_parser_list_obj, NEED_NONE),
	addpl(CLUSTER_REC_LIST, CLUSTER_REC, slurmdb_destroy_cluster_rec, create_cluster_rec_obj, NEED_NONE),
	addpl(ASSOC_LIST, ASSOC, slurmdb_destroy_assoc_rec, create_assoc_rec_obj, NEED_NONE),
	addpl(ASSOC_SHORT_LIST, ASSOC_SHORT, slurmdb_destroy_assoc_rec, create_assoc_rec_obj, NEED_NONE),
	addpl(COORD_LIST, COORD, slurmdb_destroy_coord_rec, create_parser_list_obj, NEED_NONE),
	addpl(CLUSTER_ACCT_REC_LIST, CLUSTER_ACCT_REC, slurmdb_destroy_clus_res_rec, create_parser_list_obj, NEED_NONE),
	addpl(JOB_LIST, JOB, slurmdb_destroy_job_rec, create_job_rec_obj, NEED_NONE),
	addpl(STEP_LIST, STEP, slurmdb_destroy_step_rec, create_step_rec_obj, NEED_NONE),
	addpl(STATS_RPC_LIST, STATS_RPC, NULL, NULL, NEED_NONE),
	addpl(STATS_USER_LIST, STATS_USER, NULL, NULL, NEED_NONE),
	addpl(TRES_LIST, TRES, slurmdb_destroy_tres_rec, create_parser_list_obj, NEED_NONE),
};
#undef addpl
#undef addps
#undef addpc
#undef addpa

// clang-format on

extern const parser_t *const find_parser_by_type(type_t type)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		if (parsers[i].type == type)
			return &parsers[i];

	fatal_abort("%s: failed to find parser with type %u", __func__, type);
}

extern void parsers_init()
{
#ifndef NDEBUG
	/* sanity check the parsers */
	for (int i = 0; i < ARRAY_SIZE(parsers); i++)
		check_parser(&parsers[i]);
#endif /* !NDEBUG */
}

#ifndef NDEBUG

extern void verify_parser_not_sliced_funcname(const parser_t *const parser,
					      const char *func,
					      const char *file, int line)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (parsers[i].fields) {
			const parser_t *const fparser = parsers[i].fields;
			for (int j = 0; j < parsers[i].field_count; j++)
				if (&fparser[j] == parser)
					fatal_abort("%s: direct reference of linking parser %s(0x%" PRIxPTR ") inside of parser array %s(0x%" PRIxPTR ")[%d]=%s(0x%" PRIxPTR ") detected as %s:%d",
						func, parser->type_string,
						(uintptr_t) parser,
						fparser->type_string,
						(uintptr_t) fparser, j,
						fparser[j].type_string,
						(uintptr_t) &fparser[j], file,
						line);
		}
	}
}

extern void verify_parser_sliced_funcname(const parser_t *const parser,
					  const char *func, const char *file,
					  int line)
{
	for (int i = 0; i < ARRAY_SIZE(parsers); i++) {
		if (&parsers[i] == parser) {
			fatal_abort("%s: expected linking parser %s(0x%" PRIxPTR ") inside of parser array %s:%d",
				    func, parser->type_string,
				    (uintptr_t) parser, file, line);
		}

		if (parsers[i].fields) {
			const parser_t *const fparser = parsers[i].fields;
			for (int j = 0; j < parsers[i].field_count; j++)
				if (&fparser[j] == parser)
					return;
		}
	}

	fatal_abort("%s: orphan parser %s(0x%" PRIxPTR ") detected", func,
		    parser->type_string, (uintptr_t) parser);
}

#endif /* !NDEBUG */
