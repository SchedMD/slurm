/*****************************************************************************\
 *  certgen_script.c
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

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/slurm_xlator.h"

#include "src/common/fetch_config.h"
#include "src/common/log.h"
#include "src/common/read_config.h"
#include "src/common/ref.h"
#include "src/common/run_command.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/certgen.h"

decl_static_data(keygen_sh_txt);
decl_static_data(certgen_sh_txt);

const char plugin_name[] = "Certificate generation script plugin";
const char plugin_type[] = "certgen/script";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

extern int init(void)
{
	debug("loaded");

	return SLURM_SUCCESS;
}

extern void fini(void)
{
	debug("unloaded");
}

static char *_exec_script(char *script_path, char *input)
{
	int rc = SLURM_SUCCESS;
	char **script_argv = NULL;
	int status = SLURM_ERROR;
	bool timed_out = false;
	char *output = NULL;

	run_command_args_t run_command_args = {
		.max_wait = 5000,
		.status = &status,
		.timed_out = &timed_out,
	};

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	script_argv[0] = script_path;
	script_argv[1] = input;

	run_command_args.script_path = script_path;
	run_command_args.script_argv = script_argv;

	output = run_command(&run_command_args);

	if (timed_out) {
		error("%s: Timed out running script '%s'",
		      plugin_type, script_path);
		rc = SLURM_ERROR;
	} else if (status) {
		error("%s: '%s' returned rc %d. stdout+stderr from script:\n%s",
		      plugin_type, script_path, status, output);
		rc = SLURM_ERROR;
	} else if (!output || !*output) {
		error("%s: Expected output from '%s', but got nothing",
		      plugin_type, script_path);
		rc = SLURM_ERROR;
	}

	xfree(script_argv);

	if (rc)
		xfree(output);

	return output;
}

static int _create_exec_script(char *name, char *contents, char **script_path)
{
	int fd = -1;

	if ((fd = dump_to_memfd(name, contents, script_path)) < 0) {
		error("%s: Failed to create script file", plugin_type);
		xfree(script_path);
	}

	return fd;
}

static char *_exec_internal_keygen(void)
{
	char *keygen_contents = NULL;
	char *name = "keygen.sh";
	char *script_path = NULL;
	int script_fd = -1;
	char *key = NULL;

	static_ref_to_cstring(keygen_contents, keygen_sh_txt);

	script_fd = _create_exec_script(name, keygen_contents, &script_path);
	if (script_fd < 0) {
		error("%s: Failed to create executable script '%s'",
		      plugin_type, name);
		xfree(keygen_contents);
		return NULL;
	}

	key = _exec_script(script_path, NULL);

	close(script_fd);
	xfree(script_path);
	xfree(keygen_contents);

	return key;
}

static char *_exec_internal_certgen(char *key)
{
	char *certgen_contents = NULL;
	char *name = "certgen.sh";
	char *script_path = NULL;
	int script_fd = -1;
	char *cert = NULL;

	static_ref_to_cstring(certgen_contents, certgen_sh_txt);

	script_fd = _create_exec_script(name, certgen_contents, &script_path);
	if (script_fd < 0) {
		error("%s: Failed to create executable script '%s'",
		      plugin_type, name);
		xfree(certgen_contents);
		return NULL;
	}

	cert = _exec_script(script_path, key);

	close(script_fd);
	xfree(script_path);
	xfree(certgen_contents);

	return cert;
}

extern int certgen_p_self_signed(char **cert_pem, char **key_pem)
{
	int rc = SLURM_SUCCESS;
	char *certgen_script = NULL, *keygen_script = NULL;
	char *cert = NULL, *key = NULL;

	xassert(cert_pem);
	xassert(key_pem);

	certgen_script =
		conf_get_opt_str(slurm_conf.certgen_params, "certgen_script=");
	keygen_script =
		conf_get_opt_str(slurm_conf.certgen_params, "keygen_script=");

	if (keygen_script) {
		if (!(key = _exec_script(keygen_script, NULL))) {
			error("%s: Unable to generate private key from script '%s'",
			      plugin_type, keygen_script);
			rc = SLURM_ERROR;
			goto end;
		}
	} else if (!(key = _exec_internal_keygen())) {
		error("%s: Unable to generate private key",
		      plugin_type);
		rc = SLURM_ERROR;
		goto end;
	}

	log_flag(TLS, "Successfully generated private key");

	if (certgen_script) {
		if (!(cert = _exec_script(certgen_script, key))) {
			error("%s: Unable to generate certificate from script '%s'",
			      plugin_type, certgen_script);
			rc = SLURM_ERROR;
			goto end;
		}
	} else if (!(cert = _exec_internal_certgen(key))) {
		error("%s: Unable to generate certificate",
		      plugin_type);
		rc = SLURM_ERROR;
		goto end;
	}

	log_flag(TLS, "Successfully generated certificate:\n%s", cert);

	*cert_pem = cert;
	*key_pem = key;

end:
	xfree(certgen_script);
	xfree(keygen_script);

	if (rc) {
		xfree(cert);
		xfree(key);
	}

	return rc;
}
