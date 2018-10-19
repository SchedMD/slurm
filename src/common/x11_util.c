/*****************************************************************************\
 *  x11_util.c - x11 forwarding support functions
 *		 also see src/slurmd/slurmstepd/x11_forwarding.[ch]
 *****************************************************************************
 *  Copyright (C) 2017 SchedMD LLC.
 *  Written by Tim Wickberg <tim@schedmd.com>
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

#include <regex.h>

#include "src/common/list.h"
#include "src/common/read_config.h"
#include "src/common/run_command.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"

#include "src/common/x11_util.h"

/*
 * This should likely be detected at build time, but I have not
 * seen any common systems where this is not the correct path.
 */
#define XAUTH_PATH "/usr/bin/xauth"

/*
 * Convert an --x11 argument into flags.
 * Note that it is legal to specify multiple options. The slurmstepd
 * on a node will decide whether to set the forward up once for the entire
 * job if it matches any of these qualifiers.
 */
uint16_t x11_str2flags(const char *str)
{
	uint16_t flags = 0;

	if (!xstrcasecmp(str, "all"))
		flags |= X11_FORWARD_ALL;
	if (!xstrcasecmp(str, "batch"))
		flags |= X11_FORWARD_BATCH;
	if (!xstrcasecmp(str, "first"))
		flags |= X11_FORWARD_FIRST;
	if (!xstrcasecmp(str, "last"))
		flags |= X11_FORWARD_LAST;

	return flags;
}


/* set target port based on DISPLAY */
extern int x11_get_display_port(void)
{
	int port;
	char *display, *port_split, *port_period;

	display = xstrdup(getenv("DISPLAY"));

	if (!display) {
		error("No DISPLAY variable set, cannot setup x11 forwarding.");
		exit(-1);
	}

	if (display[0] == ':') {
		error("Cannot forward to local display. "
		      "Can only use X11 forwarding with network displays.");
		exit(-1);
	}

	port_split = strchr(display, ':');
	if (!port_split) {
		error("Error parsing DISPLAY environment variable. "
		      "Cannot use X11 forwarding.");
		exit(-1);
	}

	port_split++;
	port_period = strchr(port_split, '.');
	if (!port_period) {
		error("Error parsing DISPLAY environment variable. "
		      "Cannot use X11 forwarding.");
		exit(-1);
	}
	*port_period = '\0';

	port = atoi(port_split) + X11_TCP_PORT_OFFSET;

	xfree(display);

	return port;
}

extern char *x11_get_xauth(void)
{
	int status, matchlen;
	char **xauth_argv;
	regex_t reg;
	regmatch_t regmatch[2];
	char *result, *cookie;
	/*
	 * Two real-world examples:
	 * "zoidberg/unix:10  MIT-MAGIC-COOKIE-1  abcdef0123456789"
	 * "zoidberg:10  MIT-MAGIC-COOKIE-1  abcdef0123456789"
	 *
	 * The "/unix" bit is optional, and captured in "[[:alnum:].-/]+:".
	 * '.' and '-' are also allowed in the hostname portion, so match them
	 * in addition to '/'.
	 *
	 * Warning: the '-' must be either first or last in the [] brackets,
	 * otherwise it will be interpreted as a range instead of the literal
	 * character.
	 */
	static char *cookie_pattern = "^[[:alnum:]./-]+:[[:digit:]]+"
				      "[[:space:]]+MIT-MAGIC-COOKIE-1"
				      "[[:space:]]+([[:xdigit:]]+)\n$";

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[0] = xstrdup("xauth");
	xauth_argv[1] = xstrdup("list");
	xauth_argv[2] = xstrdup(getenv("DISPLAY"));

	result = run_command("xauth", XAUTH_PATH, xauth_argv, 100, &status);

	debug2("%s: result from xauth: %s", __func__, result);

	free_command_argv(xauth_argv);

	if (status) {
		error("Problem running xauth command. "
		      "Cannot use X11 forwarding.");
		exit(-1);

	}

	regcomp(&reg, cookie_pattern, REG_EXTENDED);
	if (regexec(&reg, result, 2, regmatch, 0) == REG_NOMATCH) {
		error("%s: Could not retrieve magic cookie. "
		      "Cannot use X11 forwarding.", __func__);
		exit(-1);
	}

	matchlen = regmatch[1].rm_eo - regmatch[1].rm_so + 1;
	cookie = xmalloc(matchlen);
	strlcpy(cookie, result + regmatch[1].rm_so, matchlen);
	xfree(result);

	return cookie;
}

extern int x11_set_xauth(char *xauthority, char *cookie,
			 char *host, uint16_t display)
{
	int i=0, status;
	char *result;
	char **xauth_argv;

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[i++] = xstrdup("xauth");
	xauth_argv[i++] = xstrdup("-v");
	xauth_argv[i++] = xstrdup("-f");
	xauth_argv[i++] = xstrdup(xauthority);
	xauth_argv[i++] = xstrdup("add");
	xauth_argv[i++] = xstrdup_printf("%s/unix:%u", host, display);
	xauth_argv[i++] = xstrdup("MIT-MAGIC-COOKIE-1");
	xauth_argv[i++] = xstrdup(cookie);
	xauth_argv[i++] = NULL;
	xassert(i < 10);

	result = run_command("xauth", XAUTH_PATH, xauth_argv, 10000, &status);

	free_command_argv(xauth_argv);

	debug2("%s: result from xauth: %s", __func__, result);
	xfree(result);

	return status;
}

extern int x11_delete_xauth(char *xauthority, char *host, uint16_t display)
{
	int i=0, status;
	char *result;
	char **xauth_argv;

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[i++] = xstrdup("xauth");
	xauth_argv[i++] = xstrdup("-v");
	xauth_argv[i++] = xstrdup("-f");
	xauth_argv[i++] = xstrdup(xauthority);
	xauth_argv[i++] = xstrdup("remove");
	xauth_argv[i++] = xstrdup_printf("%s/unix:%u", host, display);
	xauth_argv[i++] = NULL;
	xassert(i < 10);

	result = run_command("xauth", XAUTH_PATH, xauth_argv, 10000, &status);

	free_command_argv(xauth_argv);

	debug2("%s: result from xauth: %s", __func__, result);
	xfree(result);

	return status;
}
