/* (C) 1998 Red Hat Software, Inc. -- Licensing details are in the COPYING
   file accompanying popt source distributions, available from
   ftp://ftp.redhat.com/pub/code/popt */

#undef	MYDEBUG

#include "system.h"

#include <config.h> 	/* include top level config, -mag */
#include <math.h>

#ifdef HAVE_VALUES_H
#  include <values.h>
#endif

#include "findme.h"
#include "poptint.h"

#ifndef HAVE_STRERROR
static char * strerror(int errno) {
    extern int sys_nerr;
    extern char * sys_errlist[];

    if ((0 <= errno) && (errno < sys_nerr))
	return sys_errlist[errno];
    else
	return POPT_("unknown errno");
}
#endif

void poptSetExecPath(poptContext con, const char * path, int allowAbsolute) {
    if (con->execPath) xfree(con->execPath);
    con->execPath = xstrdup(path);
    con->execAbsolute = allowAbsolute;
}

static void invokeCallbacksPRE(poptContext con, const struct poptOption * opt)
{
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    invokeCallbacksPRE(con, opt->arg);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		   (opt->argInfo & POPT_CBFLAG_PRE))
	{   poptCallbackType cb = (poptCallbackType)opt->arg;
	    /* Perform callback. */
	    cb(con, POPT_CALLBACK_REASON_PRE, NULL, NULL, opt->descrip);
	}
    }
}

static void invokeCallbacksPOST(poptContext con, const struct poptOption * opt)
{
    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    invokeCallbacksPOST(con, opt->arg);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		   (opt->argInfo & POPT_CBFLAG_POST))
	{   poptCallbackType cb = (poptCallbackType)opt->arg;
	    /* Perform callback. */
	    cb(con, POPT_CALLBACK_REASON_POST, NULL, NULL, opt->descrip);
	}
    }
}

static void invokeCallbacksOPTION(poptContext con,
				  const struct poptOption * opt,
				  const struct poptOption * myOpt,
				  const void * myData, int shorty)
{
    const struct poptOption * cbopt = NULL;

    for (; opt->longName || opt->shortName || opt->arg; opt++) {
	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    /* Recurse on included sub-tables. */
	    invokeCallbacksOPTION(con, opt->arg, myOpt, myData, shorty);
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK &&
		  !(opt->argInfo & POPT_CBFLAG_SKIPOPTION)) {
	    /* Save callback info. */
	    cbopt = opt;
	} else if (cbopt != NULL &&
		   ((myOpt->shortName && opt->shortName && shorty &&
			myOpt->shortName == opt->shortName) ||
		    (myOpt->longName && opt->longName &&
			!strcmp(myOpt->longName, opt->longName)))
		   )
	{   poptCallbackType cb = (poptCallbackType)cbopt->arg;
	    const void * cbData = (cbopt->descrip ? cbopt->descrip : myData);
	    /* Perform callback. */
	    cb(con, POPT_CALLBACK_REASON_OPTION, myOpt,
			con->os->nextArg, cbData);
	    /* Terminate (unless explcitly continuing). */
	    if (!(cbopt->argInfo & POPT_CBFLAG_CONTINUE))
		return;
	}
    }
}

poptContext poptGetContext(const char * name, int argc, const char ** argv,
			   const struct poptOption * options, int flags)
{
    poptContext con = malloc(sizeof(*con));

    memset(con, 0, sizeof(*con));

    con->os = con->optionStack;
    con->os->argc = argc;
    con->os->argv = argv;
    con->os->argb = NULL;

    if (!(flags & POPT_CONTEXT_KEEP_FIRST))
	con->os->next = 1;			/* skip argv[0] */

    con->leftovers = calloc( (argc + 1), sizeof(char *) );
    con->options = options;
    con->aliases = NULL;
    con->numAliases = 0;
    con->flags = flags;
    con->execs = NULL;
    con->numExecs = 0;
    con->finalArgvAlloced = argc * 2;
    con->finalArgv = calloc( con->finalArgvAlloced, sizeof(*con->finalArgv) );
    con->execAbsolute = 1;
    con->arg_strip = NULL;

    if (getenv("POSIXLY_CORRECT") || getenv("POSIX_ME_HARDER"))
	con->flags |= POPT_CONTEXT_POSIXMEHARDER;

    if (name)
	con->appName = strcpy(malloc(strlen(name) + 1), name);

    invokeCallbacksPRE(con, con->options);

    return con;
}

static void cleanOSE(struct optionStackEntry *os)
{
    if (os->nextArg) {
	xfree(os->nextArg);
	os->nextArg = NULL;
    }
    if (os->argv) {
	xfree(os->argv);
	os->argv = NULL;
    }
    if (os->argb) {
	PBM_FREE(os->argb);
	os->argb = NULL;
    }
}

void poptResetContext(poptContext con) {
    int i;

    while (con->os > con->optionStack) {
	cleanOSE(con->os--);
    }
    if (con->os->argb) {
	PBM_FREE(con->os->argb);
	con->os->argb = NULL;
    }
    con->os->currAlias = NULL;
    con->os->nextCharArg = NULL;
    con->os->nextArg = NULL;
    con->os->next = 1;			/* skip argv[0] */

    con->numLeftovers = 0;
    con->nextLeftover = 0;
    con->restLeftover = 0;
    con->doExec = NULL;

    for (i = 0; i < con->finalArgvCount; i++) {
	if (con->finalArgv[i]) {
	    xfree(con->finalArgv[i]);
	    con->finalArgv[i] = NULL;
	}
    }

    con->finalArgvCount = 0;

    if (con->arg_strip) {
	PBM_FREE(con->arg_strip);
	con->arg_strip = NULL;
    }
}

/* Only one of longName, shortName may be set at a time */
static int handleExec(poptContext con, char * longName, char shortName) {
    int i;

    i = con->numExecs - 1;
    if (longName) {
	while (i >= 0 && (!con->execs[i].longName ||
	    strcmp(con->execs[i].longName, longName))) i--;
    } else {
	while (i >= 0 &&
	    con->execs[i].shortName != shortName) i--;
    }

    if (i < 0) return 0;

    if (con->flags & POPT_CONTEXT_NO_EXEC)
	return 1;

    if (con->doExec == NULL) {
	con->doExec = con->execs + i;
	return 1;
    }

    /* We already have an exec to do; remember this option for next
       time 'round */
    if ((con->finalArgvCount + 1) >= (con->finalArgvAlloced)) {
	con->finalArgvAlloced += 10;
	con->finalArgv = realloc(con->finalArgv,
			sizeof(*con->finalArgv) * con->finalArgvAlloced);
    }

    i = con->finalArgvCount++;
    {	char *s  = malloc((longName ? strlen(longName) : 0) + 3);
	if (longName)
	    sprintf(s, "--%s", longName);
	else
	    sprintf(s, "-%c", shortName);
	con->finalArgv[i] = s;
    }

    return 1;
}

/* Only one of longName, shortName may be set at a time */
static int handleAlias(poptContext con, const char * longName, char shortName,
		       /*@keep@*/ const char * nextCharArg) {
    int i;

    if (con->os->currAlias && con->os->currAlias->longName && longName &&
	!strcmp(con->os->currAlias->longName, longName))
	return 0;
    if (con->os->currAlias && shortName &&
	    shortName == con->os->currAlias->shortName)
	return 0;

    i = con->numAliases - 1;
    if (longName) {
	while (i >= 0 && (!con->aliases[i].longName ||
	    strcmp(con->aliases[i].longName, longName))) i--;
    } else {
	while (i >= 0 &&
	    con->aliases[i].shortName != shortName) i--;
    }

    if (i < 0) return 0;

    if ((con->os - con->optionStack + 1) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    if (nextCharArg && *nextCharArg)
	con->os->nextCharArg = nextCharArg;

    con->os++;
    con->os->next = 0;
    con->os->stuffed = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = con->aliases + i;
    poptDupArgv(con->os->currAlias->argc, con->os->currAlias->argv,
		&con->os->argc, &con->os->argv);
    con->os->argb = NULL;

    return 1;
}

static void execCommand(poptContext con) {
    const char ** argv;
    int argc = 0;
    const char ** sargv;
    int sargc = 0;

    poptParseArgvString(con->doExec->script, &sargc, &sargv);

    if (sargv == NULL || sargc < 1 ||
	(!con->execAbsolute && strchr(sargv[0], '/')))
	    return;

    argv = malloc(sizeof(*argv) *
			(6 + sargc + con->numLeftovers + con->finalArgvCount));

    if (!strchr(sargv[0], '/') && con->execPath) {
	char *s = alloca(strlen(con->execPath) + strlen(sargv[0]) + sizeof("/"));
	sprintf(s, "%s/%s", con->execPath, sargv[0]);
	argv[argc] = s;
    } else {
	argv[argc] = findProgramPath(sargv[0]);
    }
    if (argv[argc++] == NULL) return;

    if (sargc > 1) {
	memcpy(argv + argc, sargv + 1, sizeof(*argv) * (sargc - 1));
	argc += (sargc - 1);
    }

    memcpy(argv + argc, con->finalArgv, sizeof(*argv) * con->finalArgvCount);
    argc += con->finalArgvCount;

    if (con->numLeftovers) {
	argv[argc++] = "--";
	memcpy(argv + argc, con->leftovers, sizeof(*argv) * con->numLeftovers);
	argc += con->numLeftovers;
    }

    argv[argc++] = NULL;

#ifdef __hpux
    setresuid(getuid(), getuid(),-1);
#else
/*
 * XXX " ... on BSD systems setuid() should be preferred over setreuid()"
 * XXX 	sez' Timur Bakeyev <mc@bat.ru>
 * XXX	from Norbert Warmuth <nwarmuth@privat.circular.de>
 */
#if defined(HAVE_SETUID)
    setuid(getuid());
#elif defined (HAVE_SETREUID)
    setreuid(getuid(), getuid()); /*hlauer: not portable to hpux9.01 */
#else
    ; /* Can't drop privileges */
#endif
#endif

    if (argv[0] == NULL)
	return;
#ifdef MYDEBUG
    {	const char ** arg;
	fprintf(stderr, "==> execvp(%s):", argv[0]);
	for (arg = argv; *arg; arg++)
	    fprintf(stderr, " %s", *arg);
	fprintf(stderr, "\n");
    }
#endif

    execvp(argv[0], (char *const *)argv);
}

/*@observer@*/ static const struct poptOption *
findOption(const struct poptOption * opt, const char * longName,
    char shortName,
    /*@out@*/ poptCallbackType * callback, /*@out@*/ const void ** callbackData,
    int singleDash)
{
    const struct poptOption * cb = NULL;

    /* This happens when a single - is given */
    if (singleDash && !shortName && !*longName)
	shortName = '-';

    for (; opt->longName || opt->shortName || opt->arg; opt++) {

	if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_INCLUDE_TABLE) {
	    const struct poptOption * opt2;
	    /* Recurse on included sub-tables. */
	    opt2 = findOption(opt->arg, longName, shortName, callback,
			      callbackData, singleDash);
	    if (opt2) {
		/* Sub-table data will be inheirited if no data yet. */
		if (*callback && *callbackData == NULL)
		    *callbackData = opt->descrip;
		return opt2;
	    }
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_CALLBACK) {
	    cb = opt;
	} else if (longName && opt->longName &&
		   (!singleDash || (opt->argInfo & POPT_ARGFLAG_ONEDASH)) &&
		   !strcmp(longName, opt->longName)) {
	    break;
	} else if (shortName && shortName == opt->shortName) {
	    break;
	}
    }

    if (!opt->longName && !opt->shortName)
	return NULL;
    *callbackData = NULL;
    *callback = NULL;
    if (cb) {
	*callback = (poptCallbackType)cb->arg;
	if (!(cb->argInfo & POPT_CBFLAG_INC_DATA))
	    *callbackData = cb->descrip;
    }

    return opt;
}

static const char *findNextArg(poptContext con, unsigned argx, int delete)
{
    struct optionStackEntry * os = con->os;
    const char * arg;

    do {
	int i;
	arg = NULL;
	while (os->next == os->argc && os > con->optionStack) os--;
	if (os->next == os->argc && os == con->optionStack) break;
	for (i = os->next; i < os->argc; i++) {
	    if (os->argb && PBM_ISSET(i, os->argb)) continue;
	    if (*os->argv[i] == '-') continue;
	    if (--argx > 0) continue;
	    arg = os->argv[i];
	    if (delete) {
		if (os->argb == NULL) os->argb = PBM_ALLOC(os->argc);
		PBM_SET(i, os->argb);
	    }
	    break;
	}
	if (os > con->optionStack) os--;
    } while (arg == NULL);
    return arg;
}

static /*@only@*/ const char * expandNextArg(poptContext con, const char * s)
{
    const char *a;
    size_t alen;
    char *t, *te;
    size_t tn = strlen(s) + 1;
    char c;

    te = t = malloc(tn);;
    while ((c = *s++) != '\0') {
	switch (c) {
#if 0	/* XXX can't do this */
	case '\\':	/* escape */
	    c = *s++;
	    break;
#endif
	case '!':
	    if (!(s[0] == '#' && s[1] == ':' && s[2] == '+'))
		break;
	    if ((a = findNextArg(con, 1, 1)) == NULL)
		break;
	    s += 3;

	    alen = strlen(a);
	    tn += alen;
	    *te = '\0';
	    t = realloc(t, tn);
	    te = t + strlen(t);
	    strncpy(te, a, alen); te += alen;
	    continue;
	    /*@notreached@*/ break;
	default:
	    break;
	}
	*te++ = c;
    }
    *te = '\0';
    t = realloc(t, strlen(t)+1);	/* XXX memory leak, hard to plug */
    return t;
}

static void poptStripArg(poptContext con, int which)
{
    if (con->arg_strip == NULL)
	con->arg_strip = PBM_ALLOC(con->optionStack[0].argc);
    PBM_SET(which, con->arg_strip);
}

static int poptSaveLong(const struct poptOption * opt, long aLong) {
    if (opt->argInfo & POPT_ARGFLAG_NOT)
	aLong = ~aLong;
    switch (opt->argInfo & POPT_ARGFLAG_LOGICALOPS) {
    case 0:
	*((long *) opt->arg) = aLong;
	break;
    case POPT_ARGFLAG_OR:
	*((long *) opt->arg) |= aLong;
	break;
    case POPT_ARGFLAG_AND:
	*((long *) opt->arg) &= aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*((long *) opt->arg) ^= aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	break;
    }
    return 0;
}

static int poptSaveInt(const struct poptOption * opt, long aLong) {
    if (opt->argInfo & POPT_ARGFLAG_NOT)
	aLong = ~aLong;
    switch (opt->argInfo & POPT_ARGFLAG_LOGICALOPS) {
    case 0:
	*((int *) opt->arg) = aLong;
	break;
    case POPT_ARGFLAG_OR:
	*((int *) opt->arg) |= aLong;
	break;
    case POPT_ARGFLAG_AND:
	*((int *) opt->arg) &= aLong;
	break;
    case POPT_ARGFLAG_XOR:
	*((int *) opt->arg) ^= aLong;
	break;
    default:
	return POPT_ERROR_BADOPERATION;
	break;
    }
    return 0;
}

#ifdef MYDEBUG
static void prtcon(const char *msg, poptContext con)
{
    if (msg) fprintf(stderr, "%s", msg);
    fprintf(stderr, "\tcon %p os %p nextCharArg \"%s\" nextArg \"%s\" argv[%d] \"%s\"\n",
	con, con->os,
	(con->os->nextCharArg ? con->os->nextCharArg : ""),
	(con->os->nextArg ? con->os->nextArg : ""),
	con->os->next,
	(con->os->argv && con->os->argv[con->os->next]
		? con->os->argv[con->os->next] : ""));
}
#endif

/* returns 'val' element, -1 on last item, POPT_ERROR_* on error */
int poptGetNextOpt(poptContext con)
{
    const struct poptOption * opt = NULL;
    int done = 0;

    while (!done) {
	const char * origOptString = NULL;
	poptCallbackType cb = NULL;
	const void * cbData = NULL;
	const char * longArg = NULL;
	int canstrip = 0;
	int shorty = 0;

	while (!con->os->nextCharArg && con->os->next == con->os->argc
		&& con->os > con->optionStack) {
	    cleanOSE(con->os--);
	}
	if (!con->os->nextCharArg && con->os->next == con->os->argc) {
	    invokeCallbacksPOST(con, con->options);
	    if (con->doExec) execCommand(con);
	    return -1;
	}

	/* Process next long option */
	if (!con->os->nextCharArg) {
	    char * localOptString, * optString;
	    int thisopt;

	    if (con->os->argb && PBM_ISSET(con->os->next, con->os->argb)) {
		con->os->next++;
		continue;
	    }
	    thisopt = con->os->next;
	    origOptString = con->os->argv[con->os->next++];

	    if (con->restLeftover || *origOptString != '-') {
		con->leftovers[con->numLeftovers++] = origOptString;
		if (con->flags & POPT_CONTEXT_POSIXMEHARDER)
		    con->restLeftover = 1;
		continue;
	    }

	    /* Make a copy we can hack at */
	    localOptString = optString =
			strcpy(alloca(strlen(origOptString) + 1),
			origOptString);

	    if (!optString[0])
		return POPT_ERROR_BADOPT;

	    if (optString[1] == '-' && !optString[2]) {
		con->restLeftover = 1;
		continue;
	    } else {
		char *oe;
		int singleDash;

		optString++;
		if (*optString == '-')
		    singleDash = 0, optString++;
		else
		    singleDash = 1;

		/* XXX aliases with arg substitution need "--alias=arg" */
		if (handleAlias(con, optString, '\0', NULL))
		    continue;
		if (handleExec(con, optString, '\0'))
		    continue;

		/* Check for "--long=arg" option. */
		for (oe = optString; *oe && *oe != '='; oe++)
		    ;
		if (*oe == '=') {
		    *oe++ = '\0';
		    /* XXX longArg is mapped back to persistent storage. */
		    longArg = origOptString + (oe - localOptString);
		}

		opt = findOption(con->options, optString, '\0', &cb, &cbData,
				 singleDash);
		if (!opt && !singleDash)
		    return POPT_ERROR_BADOPT;
	    }

	    if (!opt) {
		con->os->nextCharArg = origOptString + 1;
	    } else {
		if (con->os == con->optionStack &&
		   opt->argInfo & POPT_ARGFLAG_STRIP) {
		    canstrip = 1;
		    poptStripArg(con, thisopt);
		}
		shorty = 0;
	    }
	}

	/* Process next short option */
	if (con->os->nextCharArg) {
	    origOptString = con->os->nextCharArg;

	    con->os->nextCharArg = NULL;

	    if (handleAlias(con, NULL, *origOptString,
			    origOptString + 1)) {
		continue;
	    }
	    if (handleExec(con, NULL, *origOptString)) {
		/* Restore rest of short options for further processing */
		origOptString++;
		if (*origOptString) con->os->nextCharArg = origOptString;
		continue;
	    }

	    opt = findOption(con->options, NULL, *origOptString, &cb,
			     &cbData, 0);
	    if (!opt)
		return POPT_ERROR_BADOPT;
	    shorty = 1;

	    origOptString++;
	    if (*origOptString) con->os->nextCharArg = origOptString;
	}

	if (opt->arg && (opt->argInfo & POPT_ARG_MASK) == POPT_ARG_NONE) {
	    if (poptSaveInt(opt, 1L))
		return POPT_ERROR_BADOPERATION;
	} else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_VAL) {
	    if (opt->arg) {
		if (poptSaveInt(opt, (long)opt->val))
		    return POPT_ERROR_BADOPERATION;
	    }
	} else if ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_NONE) {
	    if (con->os->nextArg) {
		xfree(con->os->nextArg);
		con->os->nextArg = NULL;
	    }
	    if (longArg) {
		con->os->nextArg = expandNextArg(con, longArg);
	    } else if (con->os->nextCharArg) {
		con->os->nextArg = expandNextArg(con, con->os->nextCharArg);
		con->os->nextCharArg = NULL;
	    } else {
		while (con->os->next == con->os->argc &&
		       con->os > con->optionStack) {
		    cleanOSE(con->os--);
		}
		if (con->os->next == con->os->argc)
		    return POPT_ERROR_NOARG;

		/* make sure this isn't part of a short arg or the
                   result of an alias expansion */
		if (con->os == con->optionStack &&
		   opt->argInfo & POPT_ARGFLAG_STRIP &&
		   canstrip) {
		    poptStripArg(con, con->os->next);
		}
		
		con->os->nextArg = expandNextArg(con, con->os->argv[con->os->next++]);
	    }

	    if (opt->arg) {
		switch (opt->argInfo & POPT_ARG_MASK) {
		case POPT_ARG_STRING:
		    /* XXX memory leak, hard to plug */
		    *((const char **) opt->arg) = xstrdup(con->os->nextArg);
		    break;

		case POPT_ARG_INT:
		case POPT_ARG_LONG:
		{   long aLong;
		    char *end;

		    aLong = strtol(con->os->nextArg, &end, 0);
		    if (!(end && *end == '\0'))
			return POPT_ERROR_BADNUMBER;

		    if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_LONG) {
			if (aLong == LONG_MIN || aLong == LONG_MAX)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveLong(opt, aLong))
			    return POPT_ERROR_BADOPERATION;
		    } else {
			if (aLong > INT_MAX || aLong < INT_MIN)
			    return POPT_ERROR_OVERFLOW;
			if (poptSaveInt(opt, aLong))
			    return POPT_ERROR_BADOPERATION;
		    }
		}   break;

		case POPT_ARG_FLOAT:
		case POPT_ARG_DOUBLE:
		{   long aDouble;
		    char *end;

		    aDouble = strtod(con->os->nextArg, &end);
		    if (*end)
			return POPT_ERROR_BADNUMBER;

		    if (aDouble == +HUGE_VAL || aDouble == -HUGE_VAL)
			return POPT_ERROR_OVERFLOW;
		    if (aDouble == 0.0 && errno == ERANGE)
			return POPT_ERROR_OVERFLOW;
		    if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_DOUBLE) {
			*((double *) opt->arg) = aDouble;
		    } else {
#ifdef ABS
#undef ABS
#endif
#define ABS(a) (((a) < 0) ? -(a) : (a))
			if (ABS(aDouble) > FLT_MAX)
			    return POPT_ERROR_OVERFLOW;
			if (ABS(aDouble) < FLT_MIN)
			    return POPT_ERROR_OVERFLOW;
			*((float *) opt->arg) = aDouble;
		    }
		}   break;
		default:
		    fprintf(stdout, POPT_("option type (%d) not implemented in popt\n"),
		      opt->argInfo & POPT_ARG_MASK);
		    exit(EXIT_FAILURE);
		}
	    }
	}

	if (cb)
	    invokeCallbacksOPTION(con, con->options, opt, cbData, shorty);
	else if (opt->val && ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_VAL))
	    done = 1;

	if ((con->finalArgvCount + 2) >= (con->finalArgvAlloced)) {
	    con->finalArgvAlloced += 10;
	    con->finalArgv = realloc(con->finalArgv,
			    sizeof(*con->finalArgv) * con->finalArgvAlloced);
	}

	{   char *s = malloc((opt->longName ? strlen(opt->longName) : 0) + 3);
	    if (opt->longName)
		sprintf(s, "%s%s",
		    ((opt->argInfo & POPT_ARGFLAG_ONEDASH) ? "-" : "--"),
		    opt->longName);
	    else
		sprintf(s, "-%c", opt->shortName);
	    con->finalArgv[con->finalArgvCount++] = s;
	}

	if (opt->arg && (opt->argInfo & POPT_ARG_MASK) == POPT_ARG_NONE)
	    /*@-ifempty@*/ ;
	else if ((opt->argInfo & POPT_ARG_MASK) == POPT_ARG_VAL)
	    /*@-ifempty@*/ ;
	else if ((opt->argInfo & POPT_ARG_MASK) != POPT_ARG_NONE) {
	    con->finalArgv[con->finalArgvCount++] = xstrdup(con->os->nextArg);
	}
    }

    return opt->val;
}

const char * poptGetOptArg(poptContext con) {
    const char * ret = con->os->nextArg;
    con->os->nextArg = NULL;
    return ret;
}

const char * poptGetArg(poptContext con) {
    const char * ret = (con->nextLeftover < con->numLeftovers)
	? con->leftovers[con->nextLeftover++] : NULL;
    return ret;
}

const char * poptPeekArg(poptContext con) {
    const char * ret = (con->nextLeftover < con->numLeftovers)
	? con->leftovers[con->nextLeftover] : NULL;
    return ret;
}

const char ** poptGetArgs(poptContext con) {
    if (con->numLeftovers == con->nextLeftover) return NULL;

    /* some apps like [like RPM ;-) ] need this NULL terminated */
    con->leftovers[con->numLeftovers] = NULL;

    return (con->leftovers + con->nextLeftover);
}

void poptFreeContext(poptContext con) {
    int i;

    poptResetContext(con);
    if (con->os->argb) free(con->os->argb);

    for (i = 0; i < con->numAliases; i++) {
	if (con->aliases[i].longName) xfree(con->aliases[i].longName);
	free(con->aliases[i].argv);
    }

    for (i = 0; i < con->numExecs; i++) {
	if (con->execs[i].longName) xfree(con->execs[i].longName);
	xfree(con->execs[i].script);
    }
    if (con->execs) xfree(con->execs);

    free(con->leftovers);
    free(con->finalArgv);
    if (con->appName) xfree(con->appName);
    if (con->aliases) free(con->aliases);
    if (con->otherHelp) xfree(con->otherHelp);
    if (con->execPath) xfree(con->execPath);
    if (con->arg_strip) PBM_FREE(con->arg_strip);
    
    free(con);
}

int poptAddAlias(poptContext con, struct poptAlias newAlias,
		/*@unused@*/ int flags)
{
    int aliasNum = con->numAliases++;
    struct poptAlias * alias;

    /* SunOS won't realloc(NULL, ...) */
    if (!con->aliases)
	con->aliases = malloc(sizeof(newAlias) * con->numAliases);
    else
	con->aliases = realloc(con->aliases,
			       sizeof(newAlias) * con->numAliases);
    alias = con->aliases + aliasNum;

    alias->longName = (newAlias.longName)
	? strcpy(malloc(strlen(newAlias.longName) + 1), newAlias.longName)
	: NULL;
    alias->shortName = newAlias.shortName;
    alias->argc = newAlias.argc;
    alias->argv = newAlias.argv;

    return 0;
}

const char * poptBadOption(poptContext con, int flags) {
    struct optionStackEntry * os;

    if (flags & POPT_BADOPTION_NOALIAS)
	os = con->optionStack;
    else
	os = con->os;

    return os->argv[os->next - 1];
}

const char *const poptStrerror(const int error) {
    switch (error) {
      case POPT_ERROR_NOARG:
	return POPT_("missing argument");
      case POPT_ERROR_BADOPT:
	return POPT_("unknown option");
      case POPT_ERROR_BADOPERATION:
	return POPT_("mutually exclusive logical operations requested");
      case POPT_ERROR_OPTSTOODEEP:
	return POPT_("aliases nested too deeply");
      case POPT_ERROR_BADQUOTE:
	return POPT_("error in paramter quoting");
      case POPT_ERROR_BADNUMBER:
	return POPT_("invalid numeric value");
      case POPT_ERROR_OVERFLOW:
	return POPT_("number too large or too small");
      case POPT_ERROR_ERRNO:
	return strerror(errno);
      default:
	return POPT_("unknown error");
    }
}

int poptStuffArgs(poptContext con, const char ** argv) {
    int argc;

    if ((con->os - con->optionStack) == POPT_OPTION_DEPTH)
	return POPT_ERROR_OPTSTOODEEP;

    for (argc = 0; argv[argc]; argc++)
	;

    con->os++;
    con->os->next = 0;
    con->os->nextArg = NULL;
    con->os->nextCharArg = NULL;
    con->os->currAlias = NULL;
    poptDupArgv(argc, argv, &con->os->argc, &con->os->argv);
    con->os->argb = NULL;
    con->os->stuffed = 1;

    return 0;
}

const char * poptGetInvocationName(poptContext con) {
    return con->os->argv[0];
}

int poptStrippedArgv(poptContext con, int argc, char **argv)
{
    int i,j=1, numargs=argc;
    
    for (i = 1; i < argc; i++) {
	if (PBM_ISSET(i, con->arg_strip))
	    numargs--;
    }
    
    for (i = 1; i < argc; i++) {
	if (PBM_ISSET(i, con->arg_strip))
	    continue;
	argv[j] = (j < numargs) ? argv[i] : '\0';
	j++;
    }
    
    return numargs;
}
