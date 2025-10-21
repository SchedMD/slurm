/*****************************************************************************\
 *  http_switch.c - Auto switch between HTTP and RPC requests
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

#include "src/common/http_switch.h"
#include "src/common/http.h"
#include "src/common/http_con.h"
#include "src/common/read_config.h"
#include "src/common/slurm_protocol_util.h"

#include "src/conmgr/conmgr.h"

#include "src/interfaces/conn.h"
#include "src/interfaces/http_parser.h"
#include "src/interfaces/tls.h"
#include "src/interfaces/url_parser.h"

static struct {
	bool loaded;
	bool http;
	bool tls;
} status = { 0 };

extern bool http_switch_http_enabled(void)
{
	return (status.loaded && status.http);
}

extern bool http_switch_tls_enabled(void)
{
	return (status.loaded && status.http && status.tls);
}

extern conmgr_con_type_t http_switch_con_type(void)
{
	return (http_switch_http_enabled() ? CON_TYPE_RAW : CON_TYPE_RPC);
}

extern conmgr_con_flags_t http_switch_con_flags(void)
{
	conmgr_con_flags_t flags = CON_FLAG_NONE;

	if (http_switch_tls_enabled())
		flags |= CON_FLAG_TLS_FINGERPRINT;
	else if (conn_tls_enabled())
		flags |= CON_FLAG_TLS_SERVER;

	return flags;
}

extern int http_switch_on_data(conmgr_fd_t *con,
			       int (*on_http)(conmgr_fd_t *con))
{
	buf_t *buffer = conmgr_fd_shadow_in_buffer(con);
	rpc_fingerprint_t status = rpc_fingerprint(buffer);
	int rc = SLURM_SUCCESS;

	if (status == RPC_FINGERPRINT_NOT_FOUND) {
		rc = on_http(con);
	} else if (status == RPC_FINGERPRINT_FOUND) {
		if (conn_tls_enabled()) {
			conmgr_fd_ref_t *ref = conmgr_fd_new_ref(con);

			if (!conmgr_fd_is_tls(ref))
				rc = ESLURM_TLS_REQUIRED;

			conmgr_fd_free_ref(&ref);
		}

		if (!rc)
			rc = conmgr_fd_change_mode(con, CON_TYPE_RPC);
	} else {
		xassert(status == RPC_FINGERPRINT_NEED_MORE_BYTES);
		rc = SLURM_SUCCESS;
	}

	FREE_NULL_BUFFER(buffer);
	return rc;
}

extern void http_switch_init(void)
{
	int rc = EINVAL;
	xassert(!status.loaded);

	xassert(conmgr_enabled());

	/* Load plugins required for incoming HTTP requests */
	if ((slurm_conf.conf_flags & CONF_FLAG_DISABLE_HTTP)) {
		debug("Listening for HTTP requests disabled: CommunicationParameters=disable_http in slurm.conf");
	} else if ((rc = http_parser_g_init())) {
		debug("Listening for HTTP requests disabled: Unable to load http_parser plugin: %s",
		      slurm_strerror(rc));
	} else if ((rc = url_parser_g_init())) {
		debug("Listening for HTTP requests disabled: Unable to load url_parser plugin: %s",
		      slurm_strerror(rc));
	} else if ((rc = tls_g_init())) {
		debug("Listening for HTTP requests disabled: Unable to load TLS plugin: %s",
		      slurm_strerror(rc));
	} else {
		status.http = true;

		if (!tls_available()) {
			debug("Listening for TLS HTTP requests disabled: TLS plugin not loaded");
		} else if (conn_tls_enabled()) {
			debug("Listening for TLS HTTP requests: TLS RPCs enabled");
		} else if ((rc = tls_g_load_own_cert(NULL, 0, NULL, 0))) {
			status.tls = true;
			debug("Listening for TLS HTTP requests disabled: loading certificate failed: %s",
			      slurm_strerror(rc));
		} else {
			status.tls = true;
			debug("Listening for TLS HTTP requests enabled via server certificate");
		}
	}

	status.loaded = true;
}

extern void http_switch_fini(void)
{
	http_parser_g_fini();
	url_parser_g_fini();
	tls_g_fini();
}
