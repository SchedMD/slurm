/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  UCRL-CODE-2002-045.
 *  
 *  Written by Chris Dunlap <cdunlap@llnl.gov>
 *         and Jim Garlick  <garlick@llnl.gov>.
 *  
 *  This file is part of pam_rms, a PAM module for restricting access to
 *  the compute nodes within a cluster based on information obtained from
 *  the Quadrics Resource Management System (RMS).  For details, see
 *  <http://www.llnl.gov/linux/pam_rms/>.
 *  
 *  pam_rms is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *  
 *  pam_rms is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with pam_rms; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#include <ctype.h>
#include <errno.h>
#include <msql/msql.h>
#include <pwd.h>
#include <rms/rmscall.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>


/*  Max pids in a program group 
 */
#define MAX_PIDS        100

/*  Max active program groups on a node
 */
#define MAX_PRGS        100

/*  Max contig. node blocks in a job (RMS sched limit is 32 for 2.73) 
 */
#define MAX_RANGES      64

/*  Msql query templates
 */
#define NODES_QUERY \
    "select name from nodes where name = '%s'"
#define RES_QUERY \
    "select hostnames from resources where username = '%s'" \
    " and status = 'allocated'"

/*  Define the externally visible functions in this file.
 */
#define PAM_SM_ACCOUNT
#include <security/pam_modules.h>
#include <security/_pam_macros.h>


struct _options {
    int enable_debug;
    int enable_silence;
    const char *msg_prefix;
    const char *msg_suffix;
};

struct _range {
    int lo;
    int hi;
};


static void _log_msg(int level, const char *format, ...);
static void _parse_args(struct _options *opts, int argc, const char **argv);
static void _hostrange_parse_one(char *str, struct _range *range);
static int  _hostrange_parse_inner(char *str, struct _range *ranges, int len);
static int  _hostrange_parse(char *str, struct _range ranges[], int len);
static int  _hostrange_member(char *hostname, char *str);
static int  _rms_match_allocation(const char *user);
static int  _rms_match_uid(uid_t uid);
static int  _rms_match_uid_to_prg(uid_t uid, int prg);
static void _send_denial_msg(pam_handle_t *pamh, struct _options *opts,
            const char *user, uid_t uid);


/**********************************\
 *  Account Management Functions  *
\**********************************/

PAM_EXTERN int
pam_sm_acct_mgmt(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    struct _options opts;
    int retval;
    const char *user;
    struct passwd *pw;
    uid_t uid;
    int auth = PAM_PERM_DENIED;

    _parse_args(&opts, argc, argv);
    if (flags & PAM_SILENT)
        opts.enable_silence = 1;

    retval = pam_get_item(pamh, PAM_USER, (const void **) &user);
    if ((retval != PAM_SUCCESS) || (user == NULL) || (*user == '\0')) {
        _log_msg(LOG_ERR, "unable to identify user: %s",
            pam_strerror(pamh, retval));
        return(PAM_USER_UNKNOWN);
    }
    if (!(pw = getpwnam(user))) {
        _log_msg(LOG_ERR, "user %s does not exist", user);
        return(PAM_USER_UNKNOWN);
    }
    uid = pw->pw_uid;

    if (uid == 0)
        auth = PAM_SUCCESS;
    else if (_rms_match_uid(uid))
        auth = PAM_SUCCESS;
    else if (_rms_match_allocation(user))
        auth = PAM_SUCCESS;

    if ((auth != PAM_SUCCESS) && (!opts.enable_silence))
        _send_denial_msg(pamh, &opts, user, uid);
    _log_msg(LOG_INFO, "access %s for user %s (uid=%d)",
        (auth == PAM_SUCCESS) ? "granted" : "denied", user, uid);

    return(auth);
}


/************************\
 *  Internal Functions  *
\************************/

/*
 *  Writes message described by the 'format' string to syslog.
 */
static void
_log_msg(int level, const char *format, ...)
{
    va_list args;

    openlog("pam_rms", LOG_CONS | LOG_PID, LOG_AUTHPRIV);
    va_start(args, format);
    vsyslog(level, format, args);
    va_end(args);
    closelog();
    return;
}

/*
 *  Parses module args passed via PAM's config.
 */
static void
_parse_args(struct _options *opts, int argc, const char **argv)
{
    int i;

    opts->enable_debug = 0;
    opts->enable_silence = 0;
    opts->msg_prefix = "";
    opts->msg_suffix = "";

    /*  rsh_kludge:
     *  The rsh service under RH71 (rsh-0.17-2.5) truncates the first char
     *  of this msg.  The rsh client sends 3 NUL-terminated ASCII strings:
     *  client-user-name, server-user-name, and command string.  The server
     *  then validates the user.  If the user is valid, it responds with a
     *  1-byte zero; o/w, it responds with a 1-byte one followed by an ASCII
     *  error message and a newline.  RH's server is using the default PAM
     *  conversation function which doesn't prepend the message with a
     *  single-byte error code.  As a result, the client receives a string,
     *  interprets the first byte as a non-zero status, and treats the
     *  remaining string as an error message.  The rsh_kludge prepends a
     *  newline which will be interpreted by the rsh client as an error status.
     *
     *  rlogin_kludge:
     *  The rlogin service under RH71 (rsh-0.17-2.5) does not perform a
     *  carriage-return after the PAM error message is displayed which results
     *  in the "staircase-effect" of the next message. The rlogin_kludge
     *  appends a carriage-return to prevent this.
     */
    for (i=0; i<argc; i++) {
        if (!strcmp(argv[i], "debug"))
            opts->enable_debug = 1;
        else if (!strcmp(argv[i], "no_warn"))
            opts->enable_silence = 1;
        else if (!strcmp(argv[i], "rsh_kludge"))
            opts->msg_prefix = "\n";
        else if (!strcmp(argv[i], "rlogin_kludge"))
            opts->msg_suffix = "\r";
        else
            _log_msg(LOG_ERR, "unknown option [%s]", argv[i]);
    }
    return;
}

/* 
 *  Convert 'str' containing hyphenated range or a single digit to 'range'.
 */
static void
_hostrange_parse_one(char *str, struct _range *range)
{
    char *p;

    if ((p = strchr(str, '-')))
        *p++ = '\0';
    range->lo = atoi(str);
    range->hi = (p && *p) ? atoi(p) : range->lo;
}

/*
 *  Convert 'str' containing comma separated digits and ranges into an array
 *  of struct _range types (max 'len' elements).
 */
static int
_hostrange_parse_inner(char *str, struct _range *ranges, int len)
{
    char *p;
    int count = 0;

    while (str) {
        if (count == len) {
            _log_msg(LOG_ERR, "max number of ranges exceeded\n");
            break;
        }
        if ((p = strchr(str, ',')))
            *p++ = '\0';
        _hostrange_parse_one(str, &ranges[count++]);
        str = p;
    }
    return count;
}

/* 
 *  Parse a "base[list]" format string, where 'list' is a comma-separated
 *  list of numbers and hyphenated numerical ranges.  The 'str' argument is
 *  modified as a side-effect.
 */
static int
_hostrange_parse(char *str, struct _range ranges[], int len)
{
    char *list, *p;

    if ((list = strchr(str, '[')))
        *list++ = '\0';
    else
        return 0;

    if ((p = strchr(list, ']')))
        *p = '\0';
    else
        return 0;

    return _hostrange_parse_inner(list, ranges, MAX_RANGES);
}

/* 
 *  Return 1 if 'hostname' is a member of 'str', a quadrics-style host list as 
 *  returned by RMS db query, else 0.  The 'str' argument is truncated to the
 *  base prefix as a side-effect.
 *
 *  XXX: Note that the quadrics-style lists can include space separated 
 *  hostnames when two diffferent "bases" (root hostnames) are present.  
 *  This is not yet grokked by _hostrange_member() but it should not show 
 *  up on any LLNL systems which all use a uniform naming convention for 
 *  compute nodes.
 */
static int
_hostrange_member(char *hostname, char *str)
{
    struct _range ranges[MAX_RANGES];
    char *suffix;
    int n, suffixnum;

    if (!*hostname || !*str)
        return 0;

    /*  Can be a single hostname 
     */
    if (strcmp(hostname, str) == 0)
        return 1;

    if ((n = _hostrange_parse(str, ranges, MAX_RANGES)) == 0) {
        _log_msg(LOG_ERR, "mangled host list from RMS");
        return 0;
    }

    /*  The base has to match 
     */
    if (strncmp(hostname, str, strlen(str)) != 0)
        return 0;

    suffix = hostname + strlen(str);
    if (strlen(suffix) == 0)
        return 0;
    suffixnum = atoi(suffix);

    while (n-- > 0) {
        if (ranges[n].lo <= suffixnum && ranges[n].hi >= suffixnum)
            return 1;
    }
    return 0;
}

/*
 *  Query the RMS database to find out if 'user' has been allocated 'hostname'.
 *  If so, return 1 indicating that 'user' is authorized to access 'hostname',
 *  else return 0.  
 *
 *  NOTE: This method may not scale well as the centralized RMS database is a 
 *  potential choke point, therefore it should be used only if the program 
 *  description query, which is entirely local, fails.
 *
 *  Testing note: I was concerned about lack of detailed understanding of
 *  the msql API and the potential for memory leaks out there.  As a
 *  sanity check, I ran 10,000 iterations of this function and verified that
 *  the memory footprint did not grow.
 *
 *  Reference: /usr/lib/rms/src/rmsquery.c (released under GPL).
 *  See also:  http://www.sitesearch.oclc.org/helpzone/msql/manual.html
 */
static int 
_rms_match_allocation(const char *user)
{
    char hostname[MAXHOSTNAMELEN], *p;
    char query[256];
    char dbname[64] = { '\0' };
    m_result *res;
    int authorized = 0;
    int fd;

    /*  Record non-FQDN version of this node's hostname.
     */
    if (gethostname(hostname, sizeof(hostname)) < 0) {
        _log_msg(LOG_ERR, "gethostname: %m");
        return 0;
    }
    if ((p = strchr(hostname, '.')))
        *p = '\0';

    /*  Connect to the database server; RMS convention is 'rmshost' 
     */
    fd = msqlConnect("rmshost");
    if (fd < 0) {
        _log_msg(LOG_ERR, "failed to connect to database: %s", msqlErrMsg);
        goto done;
    }

    /*  Select database - mostly lifted from rmsquery.c 
     */
    if (snprintf(query, sizeof(query), NODES_QUERY, hostname) == -1) {
        _log_msg(LOG_ERR, "buffer overrun");
        goto done;
    }
    if ((res = msqlListDBs(fd))) {
        m_row row;

        while ((row = msqlFetchRow(res))) {
            if (strncmp(row[0], "rms_", 4) != 0)
                continue;
            if (msqlSelectDB(fd, row[0]) == 0) {
                if (msqlQuery(fd, query) > 0) {
                    strncpy(dbname, row[0], sizeof(dbname));
                    dbname[sizeof(dbname) - 1] = '\0';
                    break;
                }
            }
        }
        msqlFreeResult(res);
    }
    if (!dbname[0]) {
        _log_msg(LOG_ERR, "failed to obtain database name for this node");
        goto done;
    }
    if (msqlSelectDB(fd, dbname) < 0) {
        _log_msg(LOG_ERR, "msqlSelect: %s", msqlErrMsg);
        goto done;
    }

    /*  Query user's allocated resources and get edev[9-12] style resp 
     */
    if (snprintf(query, sizeof(query), RES_QUERY, user) < 0) {
        _log_msg(LOG_ERR, "buffer overrun");
        goto done;
    }
    if (msqlQuery(fd, query) < 0) {
        _log_msg(LOG_ERR, "msqlQuery: %s", msqlErrMsg);
        goto done;
    }
    if ((res = msqlStoreResult())) {
        m_row row;

        while ((row = msqlFetchRow(res))) {

            p = strchr(row[0], '\0');
            while ((--p >= row[0]) && isspace(*p))
                *p = '\0';

            if (_hostrange_member(hostname, row[0])) {
                authorized = 1;
                break;
            }
        }
        msqlFreeResult(res);
    }

    /*  Close socket and return result 
     */
done:
    if (fd >= 0)
        msqlClose(fd);
    return authorized;
}

/*
 *  Iterates through program descriptions looking for active programs that
 *  contain processes run by the specified 'uid'.  Returns true (ie, non-zero)
 *  on a match; o/w, returns zero.
 */
static int
_rms_match_uid(uid_t uid)
{
    int prgs[MAX_PRGS];
    prgstats_t stats;
    int nprgs;
    int i;

    /*  Get the array of program descriptions.
     */
    if (rms_prgids(MAX_PRGS, prgs, &nprgs) < 0) {
        _log_msg(LOG_ERR, "rms_prgids failed");
        return(0);
    }

    /*  If one is active and matches the uid, then declare victory.
     */
    for (i=0; i<nprgs; i++) {
        if (rms_prggetstats(prgs[i], &stats) < 0) {
            _log_msg(LOG_ERR, "rms_prggetstats failed (prg=%d)", prgs[i]);
            continue;
        }
        if ((stats.flags == PRG_RUNNING)) {
            if (_rms_match_uid_to_prg(uid, prgs[i]))
                return(1);
        }
    }
    return(0);
}

/*
 *  Checks each pid associated with the program description 'prg' to
 *  see if any match the specified 'uid'.  Returns true (ie, non-zero)
 *  on a match; o/w, returns zero.
 */
static int
_rms_match_uid_to_prg(uid_t uid, int prg)
{
    pid_t pids[MAX_PIDS];
    int npids;
    char tmpstr[MAXPATHLEN];
    int i;
    int n;
    struct stat statbuf;

    /*  Get pids for each process belonging to a given parallel program.
     */
    if (rms_prginfo(prg, MAX_PIDS, pids, &npids) < 0) {
        _log_msg(LOG_ERR, "rms_prginfo failed (prg=%d)", prg);
        return(0);
    }

    for (i=0; i<npids; i++) {
        n = snprintf(tmpstr, sizeof(tmpstr), "/proc/%d", pids[i]);
        if ((n < 0) || (n >= sizeof(tmpstr))) {
            _log_msg(LOG_ERR, "exceeded buffer for /proc pid filename");
            continue;
        }
        if (stat(tmpstr, &statbuf) < 0) {
            _log_msg(LOG_ERR, "stat(%s) failed: %s", tmpstr, strerror(errno));
            continue;
        }
        if (statbuf.st_uid == uid)
            return(1);
    }
    return(0);
}

/*
 *  Sends a message to the application informing the user
 *  that access was denied due to RMS.
 */
static void
_send_denial_msg(pam_handle_t *pamh, struct _options *opts,
                 const char *user, uid_t uid)
{
    int retval;
    struct pam_conv *conv;
    int n;
    char str[PAM_MAX_MSG_SIZE];
    struct pam_message msg[1];
    const struct pam_message *pmsg[1];
    struct pam_response *prsp;

    /*  Get conversation function to talk with app.
     */
    retval = pam_get_item(pamh, PAM_CONV, (const void **) &conv);
    if (retval != PAM_SUCCESS) {
        _log_msg(LOG_ERR, "unable to get pam_conv: %s",
            pam_strerror(pamh, retval));
        return;
    }

    /*  Construct msg to send to app.
     */
    n = snprintf(str, sizeof(str),
        "%sAccess denied: user %s (uid=%d) has no active jobs.%s",
        opts->msg_prefix, user, uid, opts->msg_suffix);
    if ((n < 0) || (n >= sizeof(str)))
        _log_msg(LOG_ERR, "exceeded buffer for pam_conv message");
    msg[0].msg_style = PAM_ERROR_MSG;
    msg[0].msg = str;
    pmsg[0] = &msg[0];
    prsp = NULL;

    /*  Send msg to app and free the (meaningless) rsp.
     */
    retval = conv->conv(1, pmsg, &prsp, conv->appdata_ptr);
    if (retval != PAM_SUCCESS)
        _log_msg(LOG_ERR, "unable to converse with app: %s",
            pam_strerror(pamh, retval));
    if (prsp != NULL)
        _pam_drop_reply(prsp, 1);

    return;
}


/*************************************\
 *  Statically Loaded Module Struct  *
\*************************************/

#ifdef PAM_STATIC
struct pam_module _pam_rms_modstruct = {
    "pam_rms",
    NULL,
    NULL,
    pam_sm_acct_mgmt,
    NULL,
    NULL,
    NULL,
};
#endif /* PAM_STATIC */
