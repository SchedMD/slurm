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

#define GEN_CSR_SCRIPT_KEY "generate_csr_script="
#define GET_TOKEN_SCRIPT_KEY "get_node_token_script="
#define SIGN_CSR_SCRIPT_KEY "sign_csr_script="
#define VALID_NODE_SCRIPT_KEY "validate_node_script="

typedef enum {
	GEN_CSR,
	GET_TOKEN,
	SIGN_CSR,
	VALID_NODE,
	CERT_SCRIPT_COUNT,
} cert_script_type_t;

typedef struct {
	char *key;
	char *path;
	bool run_in_slurmctld;
	bool required;
} cert_script_t;

cert_script_t cert_scripts[] = {
	[GEN_CSR] = {
		.key = "generate_csr_script=",
		.run_in_slurmctld = false,
		.required = true,
	},
	[GET_TOKEN] = {
		.key = "get_node_token_script=",
		.run_in_slurmctld = false,
		.required = true,
	},
	[SIGN_CSR] = {
		.key = "sign_csr_script=",
		.run_in_slurmctld = true,
		.required = true,
	},
	[VALID_NODE] = {
		.key = "validate_node_script=",
		.run_in_slurmctld = true,
		.required = false,
	},
};

extern int init(void)
{
	debug("loaded");

	/*
	 * Make sure that we have the scripts that we need based on where we are
	 * initializing the plugin from.
	 */
	for (int i = 0; i < CERT_SCRIPT_COUNT; i++) {
		xassert(cert_scripts[i].key);

		if (running_in_slurmctld() != cert_scripts[i].run_in_slurmctld)
			continue;

		cert_scripts[i].path = conf_get_opt_str(
			slurm_conf.certmgr_params, cert_scripts[i].key);
		if (!cert_scripts[i].path && cert_scripts[i].required) {
			error("No script was set with '%s' in CertmgrParameters setting",
			      cert_scripts[i].key);
			return SLURM_ERROR;
		}
	}

	return SLURM_SUCCESS;
}

extern int fini(void)
{
	return SLURM_SUCCESS;
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

extern char *certmgr_p_sign_csr(char *csr, char *token, node_record_t *node)
{
	char **script_argv;
	int script_rc = SLURM_ERROR;
	char *signed_cert_pem = NULL;
	char *output = NULL;

	if (node->cert_token) {
		if (xstrcmp(node->cert_token, token)) {
			error("%s: Token does not match what was set in node record table for node '%s'.",
			      plugin_type, node->name);
			return NULL;
		}

		log_flag(TLS, "Token received from node '%s' matches what was set in node record table.",
			 node->name);
		goto skip_validation_script;
	}

	if (!cert_scripts[VALID_NODE].path) {
		log_flag(TLS, "No token set in node record table for node '%s', and no validation script is configured. Token is invalid.",
			 node->name);
		return NULL;
	}

	log_flag(TLS, "No token set in node record table for node '%s'. Will run validation script to check token.",
		 node->name);

	script_argv = xcalloc(3, sizeof(char *)); /* NULL terminated */
	/* script_argv[0] set to script path later */
	script_argv[1] = token;

	output = _run_script(VALID_NODE, script_argv, &script_rc);
	xfree(output);
	xfree(script_argv);

	if (script_rc) {
		error("%s: Unable to validate node certificate signing request for node '%s'.",
		      plugin_type, node->name);
		return NULL;
	}

skip_validation_script:
	log_flag(TLS, "Successfully validated node token for node %s.",
		 node->name);

	script_argv = xcalloc(3, sizeof(char *));
	/* script_argv[0] set to script path later */
	script_argv[1] = csr;

	signed_cert_pem = _run_script(SIGN_CSR, script_argv, &script_rc);
	if (script_rc) {
		error("%s: Unable to sign node certificate signing request for node '%s'.",
		      plugin_type, node->name);
		goto fail;
	} else if (!signed_cert_pem || !*signed_cert_pem) {
		error("%s: Unable to sign node certificate signing request for node '%s'. Script printed nothing to stdout",
		      plugin_type, node->name);
		goto fail;
	} else {
		log_flag(TLS, "Successfully generated signed certificate for node '%s': \n%s",
			 node->name, signed_cert_pem);
	}

	return signed_cert_pem;

fail:
	xfree(signed_cert_pem);
	return NULL;
}
