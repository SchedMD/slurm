/*****************************************************************************\
 *  opt.c - Parsing logic for smd command
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *  Written by David Bigagli <david@schedmd.com>
 *  All rights reserved
\*****************************************************************************/

#include "src/smd/smd.h"

#define OPT_LONG_HELP   0x100
#define OPT_LONG_USAGE  0x101

/* How to use this command.
 */
static inline void
_usage(void)
{
	fprintf(stderr,"\
Usage: smd [OPTIONS...] job_id\n\
options:  -f --faulty-nodes node_name -d --drain-node node_name \
--help -r --replace-node node_name -e --extend-time -v --verbose \
-D --drop_node node_name -j --job-info -c --show-config -E --env vars \
-R reason\n");
};

static inline void
_help(void)
{
	fprintf(stderr,"\
Usage: smd [-v][-E][--help][--usage] [COMMAND] [OPTIONS]\n"
"  -v|--verbose       provide detailed event logging\n"
"  -E|--env-vars      show environment variables that need to change\n"
"  --help             show this help message\n"
"  --usage            display brief usage message\n"
"COMMANDS:\n"
"  -d|--drain-node=<node_names> -R|--reason=<reason> <job_id>\n"
"  -D|--drop-node=<node_names> <job_id>\n"
"  -e|--extend-time=<minutes> <job_id>\n"
"  -f|--faulty-nodes <job_id>  (show faulty nodes)\n"
"  -j|--job-info <job_id>      (show job information)\n"
"  -r|--replace-node=<node_name> <job_id>\n"
"  -c|--show-config\n"
"  -v|--vebose\n"
"  -H|--handle-failed <job_id>\n"
"  -G|--handle-failing <job_id>\n"
"Environment variables to handle failures automatically:\n"
"  SMD_NONSTOP_FAILED=\"REPLACE|DROP|TIME_LIMIT_DELAY=Xmin\
|TIME_LIMIT_EXTEND=YMIN|TIME_LIMIT_DROP=Zmin|EXIT_JOB\"\n"
"  SMD_NONSTOP_FAILING=\"REPLACE|DROP|TIME_LIMIT_DELAY=Xmin\
|TIME_LIMIT_EXTEND=YMIN|TIME_LIMIT_DROP=Zmin|EXIT_JOB\"\n");

};

/* Options for this command.
 */
static struct option long_options[] = {
	{"drain-node", required_argument, NULL, 'd'},
	{"drop-node", required_argument, NULL, 'D'},
	{"env-vars", no_argument, NULL, 'E'},
	{"extend-time", required_argument, NULL, 'e'},
	{"faulty-nodes", no_argument, NULL, 'f'},
	{"help", no_argument, NULL, OPT_LONG_HELP},
	{"job-info", no_argument, NULL, 'j'},
	{"reason", required_argument, NULL, 'R'},
	{"replace-node", required_argument, NULL, 'r'},
	{"show-config", no_argument, NULL, 'c'},
	{"usage", no_argument, NULL, OPT_LONG_USAGE},
	{"verbose", no_argument, NULL, 'v'},
	{"handle-failed", required_argument, NULL, 'F'},
	{"handle-failing", required_argument, NULL, 'G'},
	{NULL, 0, NULL, 0}
};

/* Set of actions to be performed when a host
 * has failed.
 */
static struct key_val failed[] =
{
	{"failed", UINT32_MAX},
	{"replace", UINT32_MAX},
	{"drop", UINT32_MAX},
	{"time_limit_delay", UINT32_MAX},
	{"time_limit_extend", UINT32_MAX},
	{"time_limit_drop", UINT32_MAX},
	{"exit_job", UINT32_MAX},
	{NULL, UINT32_MAX}
};

/* Set of actions to be performed when host
 * is failing.
 */
static struct key_val failing[] =
{
	{"failing", UINT32_MAX},
	{"replace", UINT32_MAX},
	{"drop", UINT32_MAX},
	{"time_limit_delay", UINT32_MAX},
	{"time_limit_extend", UINT32_MAX},
	{"time_limit_drop", UINT32_MAX},
	{"exit_job", UINT32_MAX},
	{NULL, UINT32_MAX}
};

static struct key_val *fk[2];

static int _init_and_check_keyval(const char *, struct key_val *);
static char *_tokenize(const char *);
static void _write_keyval(struct key_val *);
static char *_to_upper(char *);

/* set_params()
 */
int
set_params(int argc, char **argv, struct nonstop_params *params)
{
	int cc;
	char *job_env;
	char *nstp;

	fk[0] = failed;
	fk[1] = failing;

	while ((cc = getopt_long(argc,
	                         argv,
	                         "cd:D:e:Efjr:R:v",
	                         long_options,
	                         NULL)) != EOF) {
		switch (cc) {
			case 'c':
				params->sconfig = 1;
				break;
			case 'd':
				params->drain = 1;
				params->node = strdup(optarg);
				break;
			case 'D':
				params->drop = 1;
				params->node = strdup(optarg);
				break;
			case 'e':
				params->extend = atoi(optarg);
				break;
			case 'E':
				params->env_vars = 1;
				break;
			case 'f':
				params->failed = 1;
				break;
			case 'j':
				params->jinfo = 1;
				break;
			case 'r':
				params->replace = 1;
				params->node = strdup(optarg);
				break;
			case 'R':
				params->reason = strdup(optarg);
				break;
			case 'v':
				params->verbose = 1;
				break;
			case (int)OPT_LONG_HELP:
				_help();
				return -1;
			case 'H':
				params->handle_failed = strdup(optarg);
				break;
			case 'G':
				params->handle_failing = strdup(optarg);
				break;
			case '?':
			case (int)OPT_LONG_USAGE:
				_usage();
				return -1;
		}
	}

	/* Skip the jobID checking for all those
	 * options that don't need it.
	 */
	if (params->sconfig)
		return 0;

	if (argv[argc - 1] && (optind != argc)) {
		params->job_id = atoi(argv[argc - 1]);
	} else if ((job_env = getenv("SLURM_JOBID"))
	           || (job_env = getenv("SLURM_JOB_ID"))) {
		params->job_id = atoi(job_env);
	} else {
		smd_log(stderr, "%s: Job ID must be specified", __func__);
		_usage();
		return -1;
	}

	if ((nstp = getenv("SMD_NONSTOP_FAILING"))) {
		freeit(params->handle_failing);
		params->handle_failing = strdup(nstp);
	}

	if ((nstp = getenv("SMD_NONSTOP_FAILED"))) {
		freeit(params->handle_failed);
		params->handle_failed = strdup(nstp);
	}

	if ((nstp = getenv("SMD_NONSTOP_DEBUG")))
		params->verbose = 1;

	return 0;
}

/* check_params()
 */
int
check_params(struct nonstop_params *params)
{
	int cc;

	/* Check for some possible yahoo situations.
	 */
	if (params->failed == 0 && params->drain == 0
	    && params->replace == 0 && params->extend == 0
	    && params->drop == 0 && params->jinfo == 0
	    && params->sconfig == 0
	    && params->handle_failed == NULL
		&& params->handle_failing == NULL) {
		smd_log(stderr, "%s: No valid parameters specified", __func__);
		_usage();
		return -1;
	}

	if (params->drain) {
		if (!params->reason) {
			smd_log(stderr, "%s: Reason must be specified.", __func__);
			_usage();
			return -1;
		}
	}

	if (params->handle_failed) {
		/* Set the failed value to one as the
		 * user is interested in handling failed nodes.
		 */
		failed[failure_type].val = failed_hosts;
		cc = _init_and_check_keyval(params->handle_failed, fk[0]);
		if (cc < 0) {
			smd_log(stderr, "\
%s: failed initializing automatic parameters for failed nodes", __func__);
			return -1;
		}
	}

	if (params->handle_failing) {
		/* Set the failed value to one as the
		 * user is interested in handling failed nodes.
		 */
		failing[0].val = failing_hosts;
		cc = _init_and_check_keyval(params->handle_failing, fk[1]);
		if (cc < 0) {
			smd_log(stderr, "\
%s: failed initializing automatic parameters for failing nodes", __func__);
			return -1;
		}
	}

	if (params->verbose
		&& params->handle_failed)
		_write_keyval(failed);
	if (params->verbose
	    && params->handle_failing)
		_write_keyval(failing);

	return 0;
}

/* free_params()
 */
void
free_params(struct nonstop_params *params)
{
	if (params == NULL)
		return;

	freeit(params->node);
	freeit(params->reason);
	freeit(params->handle_failed);
	freeit(params->handle_failing);
}

/* get_keyval()
 */
struct key_val **
get_key_val(void)
{
	return fk;
}

/* _init_and_check_keyval()
 *
 * replace_wait,    replace or wait up to
 * replace_extend,  extend runtime after replace node
 * drop_extend,     extend runtime after drop node
 * exit_job         abort the job
 *
 */
static int
_init_and_check_keyval(const char *str,
                       struct key_val *keyval)
{
	char *buf;
	char *t;
	char *p;

	/* tokenize then parse
	 */
	p = buf = _tokenize(str);
	if (buf == NULL)
		return -1;

	/* Go and match dictionary keys
	 * one by one.
	 */
	while ((t = smd_get_token(&buf))) {
		int cc;

		/* replace failed or failing nodes
		 */
		if (strcmp(t, keyval[replace].key) == 0) {
			keyval[replace].val = 1;
			continue;
		}

		/* drop failed or failing nodes
		 */
		if (strcmp(t, keyval[drop].key) == 0) {
			keyval[drop].val = 1;
			continue;
		}

		/* TimeLimitDelay
		 */
		if (strcmp(t, keyval[time_limit_delay].key) == 0) {
			/* next token is the time value.
			 */
			t = smd_get_token(&buf);
			keyval[time_limit_delay].val = atoi(t);
			continue;
		}

		/* time_limit_extend
		 */
		if (strcmp(t, keyval[time_limit_extend].key) == 0) {
			/* next token is the time value.
			 */
			t = smd_get_token(&buf);
			keyval[time_limit_extend].val = atoi(t);
			continue;
		}

		/* time_limit_drop
		 */
		if (strcmp(t, keyval[time_limit_drop].key) == 0) {
			/* next token is the time value.
			 */
			t = smd_get_token(&buf);
			keyval[time_limit_drop].val = atoi(t);
			continue;
		}

		/* exit_job upon failure
		 */
		if (strcmp(t, keyval[exit_job].key) == 0) {
			keyval[exit_job].val = 1;
			continue;
		}

		/* Handle the error situation.
		 */
		smd_log(stderr, "\
%s: Unknown key name <%s>, bailing out.", __func__, t);
		smd_log(stderr, "%s: valid keys are:", __func__);
		cc = 0;
		while (keyval[cc].key) {
			char *p = _to_upper(keyval[cc].key);
			smd_log(stderr, "  key: \"%s\"", p);
			++cc;
		}

		free(p);

		return -1;

	} /* while (t = smd_get_token()) */

	freeit(p);

	return 0;
}

/* _tokenize()
 */
static char *
_tokenize(const char *buf)
{
	char *p;
	int i;

	p = calloc(strlen(buf) + 1, sizeof(char));
	if (p == NULL)
		return NULL;

	/* First turn all into lower case
	 * to avoid ambiguity
	 */
	i = 0;
	while (buf[i]) {

		if (isspace(buf[i])) {
			++i;
			continue;
		}
		p[i] = tolower(buf[i]);
		++i;
	}

	/* Then tokenize by spaces
	 */
	i = 0;
	while (p[i]) {

		if (p[i] == '='
		    || p[i] == ':')
			p[i] = ' ';
		++i;
	}

	return p;
}

/* _write_keyval()
 *
 * Write the keyval configuration to stderr.
 *
 */
static void
_write_keyval(struct key_val *k)
{
	int cc;

	if (k == fk[0])
		smd_log(stderr, "%s: parameters for failed nodes", __func__);
	else
		smd_log(stderr, "%s: parameters for failing nodes", __func__);

	cc = 0;
	while (k[cc].key) {
		smd_log(stderr, "\
key: %-10s value: %u", k[cc].key, k[cc].val);
		++cc;
	}
}

/* _to_upper()
 */
static char *
_to_upper(char *buf)
{
	static char p[128];
	int i;

	i = 0;
	while (buf[i] != 0) {
		p[i] = toupper(buf[i]);
		++i;
	}
	p[i] = 0;

	return p;
}
