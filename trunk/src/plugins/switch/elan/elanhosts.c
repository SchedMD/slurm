/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <mgrondona@llnl.gov>.
 *  UCRL-CODE-2003-005.
 *  
 *  This file is part of Pdsh, a parallel remote shell program.
 *  For details, see <http://www.llnl.gov/linux/pdsh/>.
 *  
 *  Pdsh is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  Pdsh is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with Pdsh; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#if     HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "src/common/slurm_xlator.h"
#include "elanhosts.h"

/* Default ElanId config file */
#define ELANID_CONFIG_FILE "/etc/elanhosts"

/* 
 * Error strings for error codes returned by parse_elanid_config()
 */
static char *errstr[] = 
{ "No error", 
  "Out of memory!",
  "Parse error", 
  "Number of ElanIds specified != number of hosts",
  "Type must be \"eip\" \"eth\" or \"other\"",
  NULL
};

/*
 *  Container for converting hostnames to ElanIDs
 */
struct elan_info {
    elanhost_type_t type;     /* type of entry                              */
    int             elanid;   /* ElanID corresponding to this hostname      */
    char           *hostname; /* Resolveable hostname                       */
};

struct elanhost_config {
#ifndef NDEBUG
    int magic;
#   define ELANHOST_CONFIG_MAGIC 0xe100e100
#endif
    int maxid;         /* Storage for max ElanID in config                   */
	List elanid_list;  /* List of elan_info objects describing configuration */
    char errstr[1024]; /* String describing last error from this object      */
};


/* 
 * Static Prototypes:
 */
static elanhost_config_t _elanhost_config_alloc(void);
static void _elanhost_err(elanhost_config_t ec, const char *fmt, ...);
static int _find_host(struct elan_info *ei, char *key);
static int _parse_elanid_config(elanhost_config_t ec, const char *path);
static int _parse_elanid_line(elanhost_config_t ec, char *buf); 
static struct elan_info * _elan_info_create(elanhost_type_t type, 
                                            int elanid, char *hostname);
static void _elan_info_destroy(struct elan_info *ei);


elanhost_config_t elanhost_config_create()
{
    return _elanhost_config_alloc();
}


int elanhost_config_read(elanhost_config_t ec, const char *filename)
{
    assert(ec != NULL);
    assert(ec->magic == ELANHOST_CONFIG_MAGIC);
    assert(ec->elanid_list != NULL);

    if (filename == NULL)
        filename = ELANID_CONFIG_FILE;

    if (_parse_elanid_config(ec, filename) < 0) 
        return(-1);

    return(0);
}

void elanhost_config_destroy(elanhost_config_t ec)
{
    assert(ec != NULL);
    assert(ec->magic == ELANHOST_CONFIG_MAGIC);
    list_destroy(ec->elanid_list);
    assert(ec->magic = ~ELANHOST_CONFIG_MAGIC);
    free(ec);
}

int elanhost_config_maxid(elanhost_config_t ec)
{
    assert(ec != NULL);
    assert(ec->magic == ELANHOST_CONFIG_MAGIC);

    return ec->maxid;
}

int elanhost_host2elanid(elanhost_config_t ec, char *host)
{
    struct elan_info *ei;

    assert(ec != NULL);
    assert(host != NULL);
    assert(ec->magic == ELANHOST_CONFIG_MAGIC);

    ei = list_find_first(ec->elanid_list, (ListFindF) _find_host, host);

    if (!ei) {
        _elanhost_err(ec, "Unable to find host \"%s\" in configuration", host);
        return -1;
    }

    return ei->elanid;
}

const char *elanhost_config_err(elanhost_config_t ec)
{
    return ec->errstr;
}


struct elanid_find_arg {
    elanhost_type_t type;
    int elanid;
};

static int _find_elanid(struct elan_info *ei, struct elanid_find_arg *arg)
{ 
    if (ei->type != arg->type)
        return 0;

    if (ei->elanid != arg->elanid)
        return 0;

    return 1;
}

char *elanhost_elanid2host(elanhost_config_t ec, elanhost_type_t type, int eid)
{
    struct elan_info *ei;
    struct elanid_find_arg arg;

    assert(ec != NULL);
    assert(eid >= 0);
    assert(ec->magic == ELANHOST_CONFIG_MAGIC);

    arg.type = type;
    arg.elanid = eid;

    ei = list_find_first(ec->elanid_list, (ListFindF) _find_elanid, &arg);

    if (!ei) {
        _elanhost_err(ec, "Unable to find host with type=%d elanid=%d", 
                         type, eid);
        return(NULL);
    }

    return ei->hostname;
}

static elanhost_config_t _elanhost_config_alloc(void)
{
    elanhost_config_t new = malloc(sizeof(*new));

    new->maxid = -1;
    new->elanid_list = list_create((ListDelF) _elan_info_destroy);

    assert(new->magic = ELANHOST_CONFIG_MAGIC);

    return new;
}

static void _elanhost_err(elanhost_config_t ec, const char *fmt, ...)
{
    va_list ap;

    assert(ec != NULL);
    assert(fmt != NULL);

    va_start(ap, fmt);
    vsnprintf(ec->errstr, 1024, fmt, ap);
    va_end(ap);

    return;
}

/*
 * Parse the "elanhosts" config file which has the form
 * 
 *   ElanIds  Hostnames
 *   [n-m]    host_n,...,host_m
 *   [n-m]    host[n-m]
 *   etc.
 *
 * and which maps ElanIds to hostnames on the cluster.
 * The results are stored in the config object's elanid_list member. 
 *
 * Returns 0 on Success, and an error code < 0 on failure.
 */
static int _parse_elanid_config(elanhost_config_t ec, const char *path)
{
	char  buf[4096];
	int   line;
	FILE *fp;

	if (!(fp = fopen(path, "r"))) {
		_elanhost_err(ec, "failed to open %s\n",  path);
		return -1;
	}

	line = 1;
	while (fgets(buf, 4096, fp)) {
		int rc;
		if ((rc = _parse_elanid_line(ec, buf)) < 0) {
			_elanhost_err(ec, "%s: line %d: %s", path, line, errstr[-rc]);
			return -1;
		}
		line++;
	}

	if (fclose(fp) < 0)
		_elanhost_err(ec, "close(%s): %m", path);

	return 0;
}


/*
 *  Translate type strings "eip," "eth," or "other" into their
 *   corresponding elanhost_type_t number
 */
static elanhost_type_t _get_type_num(char *type)
{
    if (strcasecmp(type, "eip") == 0)
        return ELANHOST_EIP;
    else if (strcasecmp(type, "eth") == 0)
        return ELANHOST_ETH;
    else if (strcasecmp(type, "other") == 0)
        return ELANHOST_OTHER;
    else
        return -1;
}

/*
 *  Parse one line of elanId list appending results to list "eil"
 *
 *  Returns -1 for parse error, -2 if the number of elanids specified
 *  doesn't equal the number of hosts.
 *
 *  Returns 0 on success
 */
static int 
_parse_elanid_line(elanhost_config_t ec, char *buf)
{
	hostlist_t  el, hl;
	const char *separators = " \t\n";
    char       *type;
	char       *elanids;
	char       *hosts;
	char       *sp, *s;
	int         rc = 0;
    int         typenum;

	/* 
	 *  Nullify any comments
	 */
	if ((s = strchr(buf, '#')))
		*s = '\0';

    if (!(type = strtok_r(buf, separators, &sp)))
        return 0;

	if (!(elanids = strtok_r(NULL, separators, &sp)))
		return -1;

	if (!(hosts = strtok_r(NULL, separators, &sp)))
		return -2;

	el = hostlist_create(NULL);
	hl = hostlist_create(NULL);

	if (!el || !hl) {
		rc = -1;
		goto done;
	}

	if (hostlist_push(el, elanids) != hostlist_push(hl, hosts)) {
		rc = -3; 
		goto done;
	}

    if ((typenum = _get_type_num(type)) < 0)
        return -4;

	while ((s = hostlist_shift(el))) {
		char *eptr;
		int   elanid = (int) strtoul(s, &eptr, 10);

		if (*eptr != '\0') {
			rc = -2;
			goto done;
		}

		free(s);
		if (!(s = hostlist_shift(hl))) {
			rc = -1;
			goto done;
		}

        if (elanid > ec->maxid)
            ec->maxid = elanid;

		list_append(ec->elanid_list, _elan_info_create(typenum, elanid, s));
	}

    done:
	hostlist_destroy(el);
	hostlist_destroy(hl);

	return rc;
}

static struct elan_info *
_elan_info_create(elanhost_type_t type, int elanid, char *hostname)
{
	struct elan_info *ei = (struct elan_info *) malloc(sizeof(*ei));
    ei->type     = type;
	ei->elanid   = elanid;
	ei->hostname = hostname;
	return ei;
}

static void
_elan_info_destroy(struct elan_info *ei)
{
	if (ei->hostname)
		free(ei->hostname);
	free(ei);
}


/*
 *  List Find function for mapping hostname to an ElanId
 */
static int _find_host(struct elan_info *ei, char *key)
{
    if (strcmp(ei->hostname, key) != 0)
        return 0;
    else
        return 1;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

