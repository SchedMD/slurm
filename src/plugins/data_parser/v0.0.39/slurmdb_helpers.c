/*****************************************************************************\
 *  slurmdb_helpers.c - data parsing slurmdb helpers
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

#include "src/common/data.h"
#include "src/common/log.h"
#include "src/common/net.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/uid.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "alloc.h"
#include "events.h"
#include "parsing.h"
#include "slurmdb_helpers.h"

extern int db_query_list_funcname(parse_op_t op, data_parser_type_t type,
				  args_t *args, List *list,
				  db_list_query_func_t func, void *cond,
				  const char *func_name,
				  const char *func_caller_name)
{
	int rc;
	List l;

	xassert(!*list);
	xassert(args->db_conn);

	errno = 0;
	l = func(args->db_conn, cond);

	if (errno) {
		FREE_NULL_LIST(l);
		rc = on_error(op, type, args, errno, func_name,
			      func_caller_name,
			      "function 0x%" PRIxPTR " failed",
			      (uintptr_t) func);
	} else if (!l) {
		rc = on_error(op, type, args, ESLURM_REST_INVALID_QUERY,
			      func_name, func_caller_name,
			      "function 0x%" PRIxPTR " returned NULL list",
			      (uintptr_t) func);
	} else if (!list_count(l)) {
		FREE_NULL_LIST(l);

		rc = on_error(op, type, args, ESLURM_REST_EMPTY_RESULT,
			      func_name, func_caller_name,
			      "function 0x%" PRIxPTR " returned empty list",
			      (uintptr_t) func);
	} else {
		rc = SLURM_SUCCESS;
	}

	if (!rc)
		*list = l;

	return rc;
}

extern int resolve_qos(parse_op_t op, const parser_t *const parser,
		       slurmdb_qos_rec_t **qos_ptr, data_t *src, args_t *args,
		       data_t *parent_path, const char *caller,
		       bool ignore_failure)
{
	slurmdb_qos_rec_t *qos = NULL;
	char *path = NULL;
	int rc = SLURM_SUCCESS;

	xassert(args->magic == MAGIC_ARGS);
	xassert(data_get_type(src));
	xassert(data_get_type(parent_path));
	xassert(!*qos_ptr);

	/* find qos by name from global list */
	if (!args->qos_list) {
		rc = ESLURM_REST_EMPTY_RESULT;
		if (!ignore_failure)
			on_error(op, parser->type, args, rc,
				 set_source_path(&path, parent_path), caller,
				 "Unable to resolve QOS when there are no QOS");
		goto done;
	}

	if (data_get_type(src) == DATA_TYPE_NULL) {
		/* nothing to resolve */
		return SLURM_SUCCESS;
	} else if (data_get_type(src) == DATA_TYPE_DICT) {
		/* user may have provided entire QOS record */
		const parser_t *const qos_parser =
			find_parser_by_type(DATA_PARSER_QOS);
		slurmdb_qos_rec_t *pqos = alloc_parser_obj(qos_parser);

		xassert(xsize(pqos) == sizeof(*pqos));

		if ((rc = parse(pqos, sizeof(*pqos), qos_parser, src, args,
				parent_path))) {
			if (!ignore_failure)
				on_error(op, parser->type, args, rc,
					 set_source_path(&path, parent_path),
					 caller,
					 "Parsing dictionary into QOS failed");
			slurmdb_destroy_qos_rec(pqos);
			goto done;
		}

		xassert(!qos);
		if (pqos->id > 0) {
			if (!(qos = list_find_first(args->qos_list,
						    slurmdb_find_qos_in_list,
						    &pqos->id))) {
				rc = ESLURM_REST_EMPTY_RESULT;
				if (!ignore_failure)
					on_error(op, parser->type, args, rc,
						 __func__,
						 set_source_path(&path,
								 parent_path),
						 "Unable to find QOS by given ID#%d",
						 pqos->id);
			}
		} else if (pqos->name) {
			if (!(qos = list_find_first(
				      args->qos_list,
				      slurmdb_find_qos_in_list_by_name,
				      pqos->name))) {
				rc = ESLURM_REST_EMPTY_RESULT;
				if (!ignore_failure)
					on_error(op, parser->type, args, rc,
						 set_source_path(&path,
								 parent_path),
						 __func__,
						 "Unable to find QOS by given name: %s",
						 pqos->name);
			}
		} else {
			rc = ESLURM_REST_FAIL_PARSING;
			if (!ignore_failure)
				on_error(op, parser->type, args,
					 ESLURM_REST_FAIL_PARSING,
					 set_source_path(&path, parent_path),
					 caller,
					 "Unable to find QOS without ID# or name provided");
		}

		slurmdb_destroy_qos_rec(pqos);
		goto done;
	}

	/* convert to best guessed type */
	(void) data_convert_type(src, DATA_TYPE_NONE);

	if (data_get_type(src) == DATA_TYPE_INT_64) {
		uint64_t qos_id_full = data_get_int(src);
		uint32_t qos_id = qos_id_full;

		if (qos_id_full > INT32_MAX) {
			rc = ESLURM_INVALID_QOS;
			if (!ignore_failure)
				on_error(op, parser->type, args, rc,
					 set_source_path(&path, parent_path),
					 caller, "QOS id#%"PRIu64" too large",
					 qos_id_full);
			goto done;
		}

		qos = list_find_first(args->qos_list, slurmdb_find_qos_in_list,
				      &qos_id);
	} else if (data_convert_type(src, DATA_TYPE_STRING) ==
		   DATA_TYPE_STRING) {
		char *qos_name = data_get_string(src);

		if (!qos_name || !qos_name[0]) {
			rc = ESLURM_INVALID_QOS;
			if (ignore_failure)
				on_error(op, parser->type, args, rc,
					 set_source_path(&path, parent_path),
					 caller, "Unable to resolve QOS with empty name");
			goto done;
		}

		qos = list_find_first(args->qos_list,
				      slurmdb_find_qos_in_list_by_name,
				      qos_name);
	} else {
		rc = ESLURM_REST_FAIL_PARSING;
		if (ignore_failure)
			on_error(op, parser->type, args, rc,
				 set_source_path(&path, parent_path), caller,
				 "QOS resolution failed with unexpected QOS name/id formated as data type:%s",
				 data_type_to_string(data_get_type(src)));
		goto done;
	}

done:
	xfree(path);

	if (rc)
		return rc;

	if (!qos)
		return ESLURM_REST_EMPTY_RESULT;

	*qos_ptr = qos;
	return SLURM_SUCCESS;
}

extern int load_prereqs_funcname(parse_op_t op, const parser_t *const parser,
				 args_t *args, const char *func_name)
{
	int rc = SLURM_SUCCESS;

	check_parser(parser);
	xassert(args->magic == MAGIC_ARGS);
	xassert((op == PARSING) || (op == DUMPING) || (op == QUERYING));

	if (parser->needs && !args->db_conn) {
		args->db_conn = slurmdb_connection_get(NULL);
		args->close_db_conn = true;
	}

	if ((parser->needs & NEED_TRES) && !args->tres_list) {
		slurmdb_tres_cond_t cond = {
			.with_deleted = 1,
		};

		if ((rc = _db_query_list(QUERYING, parser->type, args,
					 &args->tres_list, slurmdb_tres_get,
					 &cond))) {
			error("%s: loading TRES for parser 0x%" PRIxPTR
			      " failed[%d]: %s",
			      __func__, (uintptr_t) args, rc,
			      slurm_strerror(rc));
			return rc;
		}

		log_flag(DATA, "loaded %u TRES for parser 0x%" PRIxPTR,
			 list_count(args->tres_list), (uintptr_t) args);
	}

	if ((parser->needs & NEED_QOS) && !args->qos_list) {
		slurmdb_qos_cond_t cond = { 0 };

		if ((rc = _db_query_list(QUERYING, parser->type, args,
					 &args->qos_list, slurmdb_qos_get,
					 &cond))) {
			error("%s: loading QOS for parser 0x%" PRIxPTR
			      " failed[%d]: %s",
			      __func__, (uintptr_t) args, rc,
			      slurm_strerror(rc));
			return rc;
		}

		log_flag(DATA, "loaded %u QOS for parser 0x%" PRIxPTR,
			 list_count(args->qos_list), (uintptr_t) args);
	}

	if ((parser->needs & NEED_ASSOC) && !args->assoc_list) {
		slurmdb_assoc_cond_t cond = { 0 };

		if ((rc = _db_query_list(QUERYING, parser->type, args,
					 &args->assoc_list,
					 slurmdb_associations_get, &cond))) {
			error("%s: loading ASSOCS for parser 0x%" PRIxPTR
			      " failed[%d]: %s",
			      __func__, (uintptr_t) args, rc,
			      slurm_strerror(rc));
			return rc;
		}

		log_flag(DATA, "loaded %u ASSOCS for parser 0x%" PRIxPTR,
			 list_count(args->assoc_list), (uintptr_t) args);
	}

	return SLURM_SUCCESS;
}

/* checks for mis-matches and rejects on the spot */
#define _match(field, x, y)                          \
	do {                                         \
		/* both null */                      \
		if (!x->field && !y->field)          \
			continue;                    \
		/* only 1 is null */                 \
		if (!x->field != !y->field)          \
			return 0;                    \
		if (xstrcasecmp(x->field, y->field)) \
			return 0;                    \
	} while (0)

extern int compare_assoc(const slurmdb_assoc_rec_t *x,
			 const slurmdb_assoc_rec_t *y)
{
	if ((y->id > 0) && (y->id == x->id))
		return 1;

	_match(acct, x, y);
	_match(cluster, x, y);
	_match(cluster, x, y);
	_match(partition, x, y);
	_match(user, x, y);

	return 1;
}

#undef _match

extern int fuzzy_match_tres(slurmdb_tres_rec_t *tres,
			    slurmdb_tres_rec_t *needle)
{
	debug5("Comparing database tres(name:%s, type:%s, id:%u) with requested(name:%s, type:%s, id:%u).",
	       tres->name, tres->type, tres->id, needle->name, needle->type,
	       needle->id);

	if ((needle->id > 0) &&
	    ((needle->id == tres->id) &&
	     (!needle->type || !xstrcasecmp(needle->type, tres->type)) &&
	     (!needle->name || !xstrcasecmp(needle->name, tres->name))))
		return 1;
	if ((!needle->name || !needle->name[0]) &&
	    !xstrcasecmp(needle->type, tres->type))
		return 1;
	else if (!xstrcasecmp(needle->name, tres->name) &&
		 !xstrcasecmp(needle->type, tres->type))
		return 1;
	else
		return 0;
}
