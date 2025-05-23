/*****************************************************************************\
 *  certmgr_script.c
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

#include "src/common/log.h"
#include "src/common/node_conf.h"
#include "src/common/run_command.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/interfaces/certmgr.h"

const char plugin_name[] = "Certificate manager script plugin";
const char plugin_type[] = "certmgr/script";
const uint32_t plugin_version = SLURM_VERSION_NUMBER;

static char *self_signed_cert = NULL;
static char *private_key = NULL;

typedef enum {
	GEN_CSR,
	GEN_PRIVATE_KEY,
	GEN_SELF_SIGNED,
	GET_CERT_KEY,
	GET_TOKEN,
	SIGN_CSR,
	VALID_NODE,
} cert_script_type_t;

typedef struct {
	char *key;
	char *path;
	bool required;
} cert_script_t;

cert_script_t cert_scripts[] = {
	[GEN_CSR] = {
		.key = "generate_csr_script=",
		.required = true,
	},
	[GEN_SELF_SIGNED] = {
		.key = "gen_self_signed_cert_script=",
		.required = true,
	},
	[GEN_PRIVATE_KEY] = {
		.key = "gen_private_key_script=",
		.required = true,
	},
	[GET_CERT_KEY] = {
		.key = "get_node_cert_key_script=",
		.required = true,
	},
	[GET_TOKEN] = {
		.key = "get_node_token_script=",
		.required = true,
	},
	[SIGN_CSR] = {
		.key = "sign_csr_script=",
		.required = true,
	},
	[VALID_NODE] = {
		.key = "validate_node_script=",
		.required = false,
	},
};

static int _set_script_path(cert_script_t *script)
{
	script->path = conf_get_opt_str(slurm_conf.certmgr_params, script->key);
	if (!script->path && script->required) {
		error("No script was set with '%s' in CertmgrParameters setting",
		      script->key);
		return SLURM_ERROR;
	}
	return SLURM_SUCCESS;
}

static int _load_script_paths(void)
{
	if (running_in_slurmctld()) {
		/* needs to validate nodes and sign CSR's */
		if (_set_script_path(&cert_scripts[SIGN_CSR]))
			return SLURM_ERROR;
		if (_set_script_path(&cert_scripts[VALID_NODE]))
			return SLURM_ERROR;
		return SLURM_SUCCESS;
	}

	if (!running_in_daemon()) {
		/* needs ephemeral self signed certificate */
		if (_set_script_path(&cert_scripts[GEN_PRIVATE_KEY]))
			return SLURM_ERROR;
		if (_set_script_path(&cert_scripts[GEN_SELF_SIGNED]))
			return SLURM_ERROR;
		return SLURM_SUCCESS;
	}

	if (running_in_daemon()) {
		/* needs resources to get a signed certificate from slurmctld */
		if (_set_script_path(&cert_scripts[GEN_CSR]))
			return SLURM_ERROR;
		if (_set_script_path(&cert_scripts[GET_CERT_KEY]))
			return SLURM_ERROR;
		if (_set_script_path(&cert_scripts[GET_TOKEN]))
			return SLURM_ERROR;
		return SLURM_SUCCESS;
	}

	return SLURM_ERROR;
}

static char *_run_script(cert_script_type_t cert_script_type,
			 char **script_argv, int *rc_ptr)
{
	int status = SLURM_ERROR;
	bool timed_out = false;
	char *output = NULL;
	char *script_path = NULL;

	xassert(rc_ptr);

	run_command_args_t run_command_args = {
		.max_wait = 5000,
		.script_argv = script_argv,
		.status = rc_ptr,
		.timed_out = &timed_out,
	};

	script_path = cert_scripts[cert_script_type].path;

	run_command_args.script_path = script_path;

	if (script_argv)
		script_argv[0] = script_path;

	output = run_command(&run_command_args);

	if (timed_out) {
		error("%s: Timed out running script '%s'",
		      plugin_type, run_command_args.script_path);
		goto fail;
	}

	if (*rc_ptr) {
		error("%s: Error code %d encountered while running script '%s'. stdout+stderr from script:\n%s",
		      plugin_type, status, run_command_args.script_path,
		      output);
		goto fail;
	}

	return output;
fail:
	xfree(output);
	return NULL;
}

static char *_gen_private_key(void)
{
	char **script_argv;
	int script_rc;
	char *key = NULL;

	script_argv = xcalloc(2, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */

	key = _run_script(GEN_PRIVATE_KEY, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to generate private key",
		      plugin_type);
		goto fail;
	} else if (!key || !*key) {
		error("%s: Unable to generate private key. Script printed nothing to stdout",
		      plugin_type);
		goto fail;
	} else {
		log_flag(TLS, "Successfully generated private key.");
	}

	return key;

fail:
	xfree(key);
	return NULL;
}

extern char *certmgr_p_get_node_cert_key(char *node_name)
{
	char **script_argv;
	int script_rc;
	char *key = NULL;

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = node_name;

	key = _run_script(GET_CERT_KEY, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to get node's private certificate key.", plugin_type);
		goto fail;
	} else if (!key || !*key) {
		error("%s: Unable to get node's private certificate key. Script printed nothing to stdout",
		      plugin_type);
		goto fail;
	} else {
		log_flag(TLS, "Successfully retrieved node's private certificate key");
	}

	return key;

fail:
	xfree(key);
	return NULL;
}

static char *_gen_self_signed_cert(char *private_key_pem)
{
	char **script_argv;
	int script_rc;
	char *cert = NULL;

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = private_key_pem;

	cert = _run_script(GEN_SELF_SIGNED, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to generate self signed certificate",
		      plugin_type);
		goto fail;
	} else if (!cert || !*cert) {
		error("%s: Unable to generate self signed certificate. Script printed nothing to stdout",
		      plugin_type);
		goto fail;
	} else {
		log_flag(TLS, "Successfully generated self signed certificate: \n%s", cert);
	}

	return cert;
fail:
	xfree(cert);
	return NULL;
}

extern int init(void)
{
	debug("loaded");

	if (_load_script_paths())
		return SLURM_ERROR;

	if (!running_in_daemon()) {
		if (!(private_key = _gen_private_key())) {
			error("Could not generate private key");
			return SLURM_ERROR;
		}
		if (!(self_signed_cert = _gen_self_signed_cert(private_key))) {
			error("Could not generate self signed certificate");
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	xfree(self_signed_cert);
	xfree(private_key);
	return SLURM_SUCCESS;
}

extern char *certmgr_p_get_node_token(char *node_name)
{
	char **script_argv;
	int script_rc;
	char *token = NULL;

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = node_name;

	token = _run_script(GET_TOKEN, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to get node's unique token.", plugin_type);
		goto fail;
	} else if (!token || !*token) {
		error("%s: Unable to get node's unique token. Script printed nothing to stdout",
		      plugin_type);
		goto fail;
	} else {
		log_flag(TLS, "Successfully retrieved unique node token");
	}

	return token;

fail:
	xfree(token);
	return NULL;
}

extern char *certmgr_p_generate_csr(char *node_name)
{
	char **script_argv;
	int script_rc;
	char *csr = NULL;

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = node_name;

	csr = _run_script(GEN_CSR, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to generate node certificate signing request",
		      plugin_type);
		goto fail;
	} else if (!csr || !*csr) {
		error("%s: Unable to generate node certificate signing request. Script printed nothing to stdout",
		      plugin_type);
		goto fail;
	} else {
		log_flag(TLS, "Successfully generated csr: \n%s", csr);
	}

	return csr;
fail:
	xfree(csr);
	return NULL;
}

extern char *certmgr_p_sign_csr(char *csr, bool is_client_auth, char *token,
				char *name)
{
	char **script_argv;
	int script_rc = SLURM_ERROR;
	char *signed_cert_pem = NULL;
	char *output = NULL;
	node_record_t *node = NULL;

	if (!name) {
		error("%s: No name given, cannot sign CSR.",
		      plugin_type);
		return NULL;
	}

	if (!(node = find_node_record(name))) {
		log_flag(TLS, "Could not find node record for '%s'.", name);
	}

	if (is_client_auth) {
		log_flag(TLS, "Client '%s' connected via mTLS, skipping validation.", name);
		goto skip_validation_script;
	}

	if (node && node->cert_token) {
		if (xstrcmp(node->cert_token, token)) {
			error("%s: Token does not match what was set in node record table for node '%s'.",
			      plugin_type, name);
			return NULL;
		}

		log_flag(TLS, "Token received from node '%s' matches what was set in node record table.",
			 name);
		goto skip_validation_script;
	}

	if (!cert_scripts[VALID_NODE].path) {
		log_flag(TLS, "No token set in node record table for node '%s', and no validation script is configured. Token is invalid.",
			 name);
		return NULL;
	}

	if (node) {
		log_flag(TLS, "No token set in node record table for node '%s'. Will run validation script to check token.",
			 name);
	} else {
		log_flag(TLS, "Running validation script to check token for '%s'.",
			 name);
	}

	script_argv = xcalloc(4, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = name;
	script_argv[2] = token;

	output = _run_script(VALID_NODE, script_argv, &script_rc);
	xfree(output);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to validate node certificate signing request for node '%s'.",
		      plugin_type, name);
		return NULL;
	}

skip_validation_script:
	log_flag(TLS, "Successfully validated node token for node %s.",
		 name);

	script_argv = xcalloc(3, sizeof(char *));
	/* script_argv[0] set to script path later */
	script_argv[1] = csr;

	signed_cert_pem = _run_script(SIGN_CSR, script_argv, &script_rc);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to sign node certificate signing request for node '%s'.",
		      plugin_type, name);
		goto fail;
	} else if (!signed_cert_pem || !*signed_cert_pem) {
		error("%s: Unable to sign node certificate signing request for node '%s'. Script printed nothing to stdout",
		      plugin_type, name);
		goto fail;
	} else {
		log_flag(TLS, "Successfully generated signed certificate for node '%s': \n%s",
			 name, signed_cert_pem);
	}

	if ((xstrstr(slurm_conf.certmgr_params, "single_use_tokens")) && node &&
	    node->cert_token) {
		xfree(node->cert_token);
		log_flag(TLS, "Token for node '%s' has been reset following successful certificate signing.",
			 node->name);
	}

	return signed_cert_pem;

fail:
	xfree(signed_cert_pem);
	return NULL;
}
