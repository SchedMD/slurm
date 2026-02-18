/*****************************************************************************\
 *  sercli.h - serialize and deserialize to CLI
 *****************************************************************************
 *  Copyright (C) SchedMD LLC.
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

#include "src/common/sercli.h"
#include "src/common/plugin.h"
#include "src/common/read_config.h"
#include "src/common/xassert.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/data_parser.h"
#include "src/interfaces/serializer.h"

static bool _on_error(void *arg, data_parser_type_t type, int error_code,
		      const char *source, const char *why, ...)
{
	va_list ap;
	char *str;
	data_parser_dump_cli_ctxt_t *ctxt = arg;
	openapi_resp_error_t *e = NULL;

	if (ctxt) {
		xassert(ctxt->magic == DATA_PARSER_DUMP_CLI_CTXT_MAGIC);
		xassert(ctxt->errors);

		if (!ctxt->errors)
			return false;

		e = xmalloc(sizeof(*e));
	}

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	if (str) {
		error("%s: parser=%s rc[%d]=%s -> %s",
		      (source ? source : __func__),
		      (!ctxt ? "DEFAULT" : ctxt->data_parser),
		      error_code, slurm_strerror(error_code), str);

		if (e)
			e->description = str;
		else
			xfree(str);
	}

	if (error_code) {
		if (e)
			e->num = error_code;

		if (ctxt && !ctxt->rc)
			ctxt->rc = error_code;
	}

	/*
	 * e is always non-NULL is ctxt is non-NULL, but check if e != NULL to
	 * silence a coverity warning.
	 */
	if (source && ctxt && e)
		e->source = xstrdup(source);

	if (ctxt)
		list_append(ctxt->errors, e);

	return false;
}

static void _on_warn(void *arg, data_parser_type_t type, const char *source,
		     const char *why, ...)
{
	va_list ap;
	char *str;
	data_parser_dump_cli_ctxt_t *ctxt = arg;
	openapi_resp_warning_t *w = NULL;

	if (ctxt) {
		xassert(ctxt->magic == DATA_PARSER_DUMP_CLI_CTXT_MAGIC);
		xassert(ctxt->warnings);

		if (!ctxt->warnings)
			return;

		w = xmalloc(sizeof(*w));
	}

	va_start(ap, why);
	str = vxstrfmt(why, ap);
	va_end(ap);

	if (str) {
		debug("%s: parser=%s WARNING: %s",
		      (source ? source : __func__),
		      (!ctxt ? "DEFAULT" : ctxt->data_parser), str);

		if (ctxt)
			w->description = str;
		else
			xfree(str);
	}

	if (source && ctxt)
		w->source = xstrdup(source);

	if (ctxt)
		list_append(ctxt->warnings, w);
}

static void _plugrack_foreach_list(const char *full_type, const char *fq_path,
				   const plugin_handle_t id, void *arg)
{
	dprintf(STDOUT_FILENO, "%s\n", full_type);
}

extern int data_parser_dump_cli_stdout(data_parser_type_t type, void *obj,
				       int obj_bytes, void *acct_db_conn,
				       const char *mime_type,
				       const char *data_parser,
				       data_parser_dump_cli_ctxt_t *ctxt,
				       openapi_resp_meta_t *meta)
{
	int rc = SLURM_SUCCESS;
	data_t *dresp = NULL;
	data_parser_t *parser;
	char *out = NULL;

	if (!xstrcasecmp(data_parser, "list")) {
		dprintf(STDERR_FILENO, "Possible data_parser plugins:\n");
		parser = data_parser_g_new(NULL, NULL, NULL, NULL, NULL, NULL,
					   NULL, NULL, "list",
					   _plugrack_foreach_list, false);
		FREE_NULL_DATA_PARSER(parser);
		return SLURM_SUCCESS;
	}

	if (!(parser = data_parser_cli_parser(data_parser, ctxt))) {
		rc = ESLURM_DATA_INVALID_PARSER;
		error("%s output not supported by %s",
		      mime_type, SLURM_DATA_PARSER_VERSION);
		goto cleanup;
	}

	if (acct_db_conn)
		data_parser_g_assign(parser, DATA_PARSER_ATTR_DBCONN_PTR,
				     acct_db_conn);

	xassert(!meta->plugin.data_parser);
	meta->plugin.data_parser = xstrdup(data_parser_get_plugin(parser));

	dresp = data_new();

	if (!data_parser_g_dump(parser, type, obj, obj_bytes, dresp) &&
	    (data_get_type(dresp) != DATA_TYPE_NULL)) {
		serializer_flags_t sflags = SER_FLAGS_NONE;

		if (data_parser_g_is_complex(parser))
			sflags |= SER_FLAGS_COMPLEX;

		serialize_g_data_to_string(&out, NULL, dresp, mime_type,
					   sflags);
	}

	if (out && out[0])
		printf("%s\n", out);
	else
		debug("No output generated");

cleanup:
	/*
	 * This is only called from the CLI just before exiting.
	 * Skip the explicit free here to improve responsiveness.
	 */
#ifdef MEMORY_LEAK_DEBUG
	xfree(out);
	FREE_NULL_DATA(dresp);
	FREE_NULL_DATA_PARSER(parser);
#endif

	return rc;
}

extern data_parser_t *data_parser_cli_parser(const char *data_parser, void *arg)
{
	char *default_data_parser = (slurm_conf.data_parser_parameters ?
					     slurm_conf.data_parser_parameters :
					     SLURM_DATA_PARSER_VERSION);
	return data_parser_g_new(_on_error, _on_error, _on_error, arg, _on_warn,
				 _on_warn, _on_warn, arg,
				 (data_parser ? data_parser :
						default_data_parser),
				 NULL, false);
}
