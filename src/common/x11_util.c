/*****************************************************************************\
 *  x11_util.c - x11 forwarding support functions
 *		 also see src/slurmd/slurmstepd/x11_forwarding.[ch]
 *****************************************************************************
 *  Copyright (C) 2017-2019 SchedMD LLC.
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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

const char *x11_flags2str(uint16_t flags)
{
	if (flags & X11_FORWARD_ALL)
		return "all";
	if (flags & X11_FORWARD_BATCH)
		return "batch";
	if (flags & X11_FORWARD_FIRST)
		return "first";
	if (flags & X11_FORWARD_LAST)
		return "last";

	return "unset";
}

/*
 * Get local TCP port for X11 from DISPLAY environment variable, alongside an
 * xmalloc()'d hostname in *target. If the port returned is 0, *target returns
 * an xmalloc()'d string pointing to the local UNIX socket.
 *
 * Warning - will call exit(-1) if not able to retrieve.
 */
extern void x11_get_display(uint16_t *port, char **target)
{
	char *display, *port_split, *port_period;
	*target = NULL;

	display = xstrdup(getenv("DISPLAY"));

	if (!display) {
		error("No DISPLAY variable set, cannot setup x11 forwarding.");
		exit(-1);
	}

	if (display[0] == ':') {
		struct stat st;
		char *screen_period;
		*port = 0;
		screen_period = strchr(display, '.');
		if (screen_period)
			*screen_period = '\0';
		xstrfmtcat(*target, "/tmp/.X11-unix/X%s", display + 1);
		xfree(display);
		if (stat(*target, &st) != 0) {
			error("Cannot stat() local X11 socket `%s`", *target);
			exit(-1);
		}
		return;
	}

	/*
	 * Parse out port number
	 * Example: localhost/unix:89.0 or localhost/unix:89
	 */
	port_split = strchr(display, ':');
	if (!port_split) {
		error("Error parsing DISPLAY environment variable. "
		      "Cannot use X11 forwarding.");
		exit(-1);
	}
	*port_split = '\0';

	/*
	 * Handle the "screen" portion of the display port.
	 * Xorg does not require a screen to be specified, defaults to 0.
	 */
	port_split++;
	port_period = strchr(port_split, '.');
	if (port_period)
		*port_period = '\0';

	*port = atoi(port_split) + X11_TCP_PORT_OFFSET;
	*target = display;
}

extern char *x11_get_xauth(void)
{
	int status, matchlen;
	char **xauth_argv;
	regex_t reg;
	regmatch_t regmatch[2];
	char *result, *cookie;
	run_command_args_t run_command_args = {
		.max_wait = 10000,
		.script_path = XAUTH_PATH,
		.script_type = "xauth",
		.status = &status,
	};

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
				      "[[:space:]]+([[:xdigit:]]+)$";

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[0] = xstrdup("xauth");
	xauth_argv[1] = xstrdup("list");
	xauth_argv[2] = xstrdup(getenv("DISPLAY"));

	run_command_args.script_argv = xauth_argv;
	result = run_command(&run_command_args);

	xfree_array(xauth_argv);

	if (status) {
		error("Problem running xauth command. "
		      "Cannot use X11 forwarding.");
		exit(-1);

	}

	regcomp(&reg, cookie_pattern, REG_EXTENDED|REG_NEWLINE);
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

extern int x11_set_xauth(char *xauthority, char *cookie, uint16_t display)
{
	int i=0, status;
	char *result;
	char **xauth_argv;
	char template[] = "/tmp/xauth-source-XXXXXX";
	char *contents = NULL;
	char host[256];
	int fd;
	run_command_args_t run_command_args = {
		.max_wait = 10000,
		.script_path = XAUTH_PATH,
		.script_type = "xauth",
		.status = &status,
	};

	if (gethostname(host, sizeof(host)))
		fatal("%s: gethostname() failed: %m", __func__);

	/* protect against weak file permissions in old glibc */
	umask(0077);
	if ((fd = mkstemp(template)) < 0)
		fatal("%s: could not create temp file", __func__);

	xstrfmtcat(contents, "add %s/unix:%u MIT-MAGIC-COOKIE-1 %s\n",
		   host, display, cookie);
	safe_write(fd, contents, strlen(contents));
	xfree(contents);
	close(fd);

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[i++] = "xauth";
	xauth_argv[i++] = "-v";
	xauth_argv[i++] = "-f";
	xauth_argv[i++] = xauthority;
	xauth_argv[i++] = "source";
	xauth_argv[i++] = template;
	xauth_argv[i++] = NULL;
	xassert(i < 10);

	run_command_args.script_argv = xauth_argv;
	result = run_command(&run_command_args);

	(void) unlink(template);
	xfree(xauth_argv);

	debug2("%s: result from xauth: %s", __func__, result);
	xfree(result);

	return status;

rwfail:
	fatal("%s: could not write temporary xauth file", __func__);
	return SLURM_ERROR;
}

extern int x11_delete_xauth(char *xauthority, char *host, uint16_t display)
{
	int i=0, status;
	char *result;
	char **xauth_argv;
	run_command_args_t run_command_args = {
		.max_wait = 10000,
		.script_path = XAUTH_PATH,
		.script_type = "xauth",
		.status = &status,
	};

	xauth_argv = xmalloc(sizeof(char *) * 10);
	xauth_argv[i++] = xstrdup("xauth");
	xauth_argv[i++] = xstrdup("-v");
	xauth_argv[i++] = xstrdup("-f");
	xauth_argv[i++] = xstrdup(xauthority);
	xauth_argv[i++] = xstrdup("remove");
	xauth_argv[i++] = xstrdup_printf("%s/unix:%u", host, display);
	xauth_argv[i++] = NULL;
	xassert(i < 10);

	run_command_args.script_argv = xauth_argv;
	result = run_command(&run_command_args);

	xfree_array(xauth_argv);

	debug2("%s: result from xauth: %s", __func__, result);
	xfree(result);

	return status;
}
