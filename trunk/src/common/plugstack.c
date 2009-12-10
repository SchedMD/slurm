/*****************************************************************************\
 *  plugstack.c -- stackable plugin architecture for node job kontrol (SPANK)
 *****************************************************************************
 *  Copyright (C) 2005-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <libgen.h>
#include <glob.h>
#include <dlfcn.h>

#include "src/common/plugin.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/common/xassert.h"
#include "src/common/safeopen.h"
#include "src/common/strlcpy.h"
#include "src/common/read_config.h"
#include "src/common/plugstack.h"
#include "src/common/optz.h"
#include "src/common/job_options.h"
#include "src/common/env.h"

#include "src/slurmd/slurmstepd/slurmstepd_job.h"
/*#include "src/srun/srun_job.h"*/

#include <slurm/spank.h>

#define REQUIRED "required"
#define OPTIONAL "optional"
#define INCLUDE  "include"

struct spank_plugin_operations {
	spank_f *init;
	spank_f *init_post_opt;
	spank_f *local_user_init;
	spank_f *user_init;
	spank_f *task_init_privileged;
	spank_f *user_task_init;
	spank_f *task_post_fork;
	spank_f *task_exit;
	spank_f *exit;
};

const int n_spank_syms = 9;
const char *spank_syms[] = {
	"slurm_spank_init",
	"slurm_spank_init_post_opt",
	"slurm_spank_local_user_init",
	"slurm_spank_user_init",
	"slurm_spank_task_init_privileged",
	"slurm_spank_task_init",
	"slurm_spank_task_post_fork",
	"slurm_spank_task_exit",
	"slurm_spank_exit"
};

struct spank_plugin {
	const char *name;
	char *fq_path;
	plugin_handle_t plugin;
	bool required;
	int ac;
	char **argv;
	struct spank_plugin_operations ops;
	struct spank_option *opts;
};

/*
 *  SPANK Plugin options
 */

#define SPANK_OPTION_ENV_PREFIX "_SLURM_SPANK_OPTION_"

struct spank_plugin_opt {
	struct spank_option *opt;   /* Copy of plugin option info           */
	struct spank_plugin *plugin;/* Link back to plugin structure        */
	int optval;                 /* Globally unique value                */
	int found:1;                /* 1 if option was found, 0 otherwise   */
	int disabled:1;             /* 1 if option is cached but disabled   */
	char *optarg;               /* Option argument.                     */
};

/*
 *  Initial value for global optvals for SPANK plugin options
 */
static int spank_optval = 0xfff;

/*
 *  Cache of options provided by spank plugins
 */
static List option_cache = NULL;


/*
 *  SPANK plugin context (local, remote, allocator)
 */
enum spank_context_type {
	S_TYPE_NONE,
	S_TYPE_LOCAL,           /* LOCAL == srun              */
	S_TYPE_REMOTE,          /* REMOTE == slurmd           */
	S_TYPE_ALLOCATOR        /* ALLOCATOR == sbatch/salloc */
};

/*
 *  SPANK plugin hook types:
 */
typedef enum step_fn {
	SPANK_INIT = 0,
	SPANK_INIT_POST_OPT,
	LOCAL_USER_INIT,
	STEP_USER_INIT,
	STEP_TASK_INIT_PRIV,
	STEP_USER_TASK_INIT,
	STEP_TASK_POST_FORK,
	STEP_TASK_EXIT,
	SPANK_EXIT
} step_fn_t;

struct spank_handle {
#   define SPANK_MAGIC 0x00a5a500
	int                  magic;  /* Magic identifier to ensure validity. */
	struct spank_plugin *plugin; /* Current plugin using handle          */
	step_fn_t            phase;  /* Which spank fn are we called from?   */
	void               * job;    /* Reference to current srun|slurmd job */
	slurmd_task_info_t * task;   /* Reference to current task (if valid) */
};

/*
 *  SPANK plugins stack
 */
static List spank_stack = NULL;
static enum spank_context_type spank_ctx = S_TYPE_NONE;

static pthread_mutex_t spank_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
 *  Default plugin dir
 */
static const char * default_spank_path = NULL;

/*
 *  Forward declarations
 */
static int _spank_plugin_options_cache(struct spank_plugin *p);

static void _argv_append(char ***argv, int ac, const char *newarg)
{
	*argv = xrealloc(*argv, (++ac + 1) * sizeof(char *));
	(*argv)[ac] = NULL;
	(*argv)[ac - 1] = xstrdup(newarg);
	return;
}

typedef enum {
   CF_ERROR = 0,
   CF_OPTIONAL,
   CF_REQUIRED,
   CF_INCLUDE,
} cf_line_t;

static cf_line_t _plugin_stack_line_type (const char *str)
{
	if (strcmp(str, REQUIRED) == 0)
		return (CF_REQUIRED);
	else if (strcmp(str, OPTIONAL) == 0)
		return (CF_OPTIONAL);
	else if (strcmp(str, INCLUDE) == 0)
		return (CF_INCLUDE);
	else {
		error("spank: Invalid option \"%s\". Must be %s, %s or %s",
		     str, REQUIRED, OPTIONAL, INCLUDE);
		return (CF_ERROR);
	}
}


static int
_plugin_stack_parse_line(char *line, char **plugin, int *acp, char ***argv,
			 cf_line_t * type)
{
	int ac;
	const char *separators = " \t\n";
	char *path;
	char *option;
	char *s;
	char **av;
	char *sp;

	*plugin = NULL;
	*argv = NULL;
	*acp = 0;

	/* Nullify any comments
	 */
	if ((s = strchr(line, '#')))
		*s = '\0';

	if (!(option = strtok_r(line, separators, &sp)))
		return (0);

	if (((*type) = _plugin_stack_line_type(option)) == CF_ERROR)
		return (-1);

	if (!(path = strtok_r(NULL, separators, &sp)))
		return (-1);

	ac = 0;
	av = NULL;

	while ((s = strtok_r(NULL, separators, &sp)))
		_argv_append(&av, ac++, s);

	*plugin = xstrdup(path);
	*argv = av;
	*acp = ac;

	return (0);
}

static struct spank_plugin *_spank_plugin_create(char *path, int ac,
						 char **av, bool required)
{
	struct spank_plugin *plugin;
	plugin_handle_t p;
	plugin_err_t e;
	struct spank_plugin_operations ops;

	if ((e = plugin_load_from_file(&p, path)) != EPLUGIN_SUCCESS) {
		error ("spank: %s: %s\n", path, plugin_strerror(e));
		return NULL;
	}

	if (plugin_get_syms(p, n_spank_syms, spank_syms, (void **)&ops) == 0) {
		error("spank: \"%s\" exports 0 symbols\n", path);
		return NULL;
	}

	plugin = xmalloc(sizeof(struct spank_plugin));

	plugin->fq_path = path;	/* fq_path is xstrdup'd in *process_line */
	plugin->plugin = p;
	plugin->name = plugin_get_name(p);	/* no need to dup */
	plugin->required = required;
	plugin->ac = ac;
	plugin->argv = av;
	plugin->ops = ops;

	/*
	 *  Do not load static plugin options table in allocator context.
	 */
	if (spank_ctx != S_TYPE_ALLOCATOR)
		plugin->opts = plugin_get_sym(p, "spank_options");

	return (plugin);
}

void _spank_plugin_destroy(struct spank_plugin *sp)
{
	if (sp == NULL)
		return;

	xfree(sp->fq_path);

	/* No need to free "name" it was defined within plugin */
	sp->name = NULL;

	plugin_unload(sp->plugin);
	sp->plugin = NULL;
	if (sp->argv) {
		int i;
		for (i = 0; sp->argv[i]; i++)
			xfree(sp->argv[i]);
		xfree(sp->argv);
	}
	xfree(sp);
	return;
}

static char *
_spank_plugin_find (const char *path, const char *file)
{
	char dir [4096];
	char *p, *entry;
	int pathlen = strlen (path);

	if (strlcpy(dir, path, sizeof (dir)) > sizeof (dir))
		return (NULL);

	/*
	 * Ensure PATH ends with a :
	 */
	if (dir[pathlen - 1] != ':') {
		dir[pathlen] = ':';
		dir[pathlen+1] = '\0';
	}

	entry = dir;
	while ((p = strchr(entry, ':'))) {
		char *fq_path;
		*(p++) = '\0';

		fq_path = xstrdup (entry);
		if (entry [strlen(entry) - 1] != '/')
			xstrcatchar (fq_path, '/');
		xstrcat (fq_path, file);

		if (plugin_peek (fq_path, NULL, 0, NULL) != SLURM_ERROR)
			return (fq_path);

		xfree (fq_path);
		entry = p;
	}

	return (NULL);
}

static int _spank_conf_include (const char *, int, const char *, List *);

static int
_spank_stack_process_line(const char *file, int line, char *buf, List *stackp)
{
	char **argv;
	int ac;
	char *path;
	cf_line_t type = CF_REQUIRED;
	bool required;

	struct spank_plugin *p;

	if (_plugin_stack_parse_line(buf, &path, &ac, &argv, &type) < 0) {
		error("spank: %s:%d: Invalid line. Ignoring.", file, line);
		return (0);
	}

	if (type == CF_INCLUDE)
		return (_spank_conf_include (file, line, path, stackp));

	if (path == NULL)	/* No plugin listed on this line */
		return (0);

	if (path[0] != '/') {
		char *f;

		if ((f = _spank_plugin_find (default_spank_path, path))) {
			xfree (path);
			path = f;
		}
	}

	required = (type == CF_REQUIRED);
	if (!(p = _spank_plugin_create(path, ac, argv, required))) {
		if (required)
			error ("spank: %s:%d:"
			       " Failed to load plugin %s. Aborting.",
			       file, line, path);
		else
			verbose ("spank: %s:%d:"
				 "Failed to load optional plugin %s. Ignored.",
				 file, line, path);
		return (required ? -1 : 0);
	}
	if (*stackp == NULL)
		*stackp = list_create((ListDelF) _spank_plugin_destroy);

	verbose ("spank: %s:%d: Loaded plugin %s",
			file, line, xbasename (p->fq_path));

	list_append (*stackp, p);
	_spank_plugin_options_cache(p);

	return (0);
}


static int _spank_stack_create(const char *path, List * listp)
{
	int line;
	char buf[4096];
	FILE *fp;

	verbose("spank: opening plugin stack %s\n", path);

	if (!(fp = safeopen(path, "r", SAFEOPEN_NOCREATE))) {
		if (errno == ENOENT)
			debug("spank: Failed to open %s: %m\n", path);
		else
			error("spank: Failed to open %s: %m\n", path);
		return -1;
	}

	line = 1;
	while (fgets(buf, sizeof(buf), fp)) {
		if (_spank_stack_process_line(path, line, buf, listp) < 0)
			goto fail_immediately;
		line++;
	}

	fclose(fp);
	return (0);

      fail_immediately:
	if (*listp != NULL) {
		list_destroy(*listp);
		*listp = NULL;
	}
	fclose(fp);
	return (-1);
}

static int
_spank_conf_include (const char *file, int lineno, const char *pattern,
		List *stackp)
{
	int rc = 0;
	glob_t gl;
	size_t i;
	char *copy = NULL;

	if (pattern == NULL) {
		error ("%s: %d: Invalid include directive", file, lineno);
		return (SLURM_ERROR);
	}

	if (pattern[0] != '/') {
		char *dirc = xstrdup (file);
		char *dname = dirname (dirc);

		if (dname != NULL)  {
			xstrfmtcat (copy, "%s/%s", dname, pattern);
			pattern = copy;
		}
		xfree (dirc);
	}

	verbose ("%s: %d: include \"%s\"", file, lineno, pattern);

	rc = glob (pattern, 0, NULL, &gl);
	switch (rc) {
	  case 0:
	  	for (i = 0; i < gl.gl_pathc; i++) {
			rc = _spank_stack_create (gl.gl_pathv[i], stackp);
			if (rc < 0)
				break;
		}
	  	break;
	  case GLOB_NOMATCH:
		break;
	  case GLOB_NOSPACE:
		errno = ENOMEM;
	  case GLOB_ABORTED:
		verbose ("%s:%d: cannot read dir %s: %m",
			file, lineno, pattern);
		break;
	  default:
		error ("Unknown glob(3) return code = %d", rc);
		break;
	}

	xfree (copy);
	globfree (&gl);
	return (rc);
}

static int
_spank_handle_init(struct spank_handle *spank, void * arg,
		   int taskid, step_fn_t fn)
{
	memset(spank, 0, sizeof(*spank));
	spank->magic = SPANK_MAGIC;
	spank->plugin = NULL;

	spank->phase = fn;

	if (arg != NULL) {
		spank->job = arg;
		if (spank_ctx == S_TYPE_REMOTE && taskid >= 0) {
			spank->task = ((slurmd_job_t *) arg)->task[taskid];
		}
	}
	return (0);
}

static const char *_step_fn_name(step_fn_t type)
{
	switch (type) {
	case SPANK_INIT:
		return ("init");
	case SPANK_INIT_POST_OPT:
		return ("init_post_opt");
	case LOCAL_USER_INIT:
		return ("local_user_init");
	case STEP_USER_INIT:
		return ("user_init");
	case STEP_TASK_INIT_PRIV:
		return ("task_init_privileged");
	case STEP_USER_TASK_INIT:
		return ("task_init");
	case STEP_TASK_POST_FORK:
		return ("task_post_fork");
	case STEP_TASK_EXIT:
		return ("task_exit");
	case SPANK_EXIT:
		return ("exit");
	}

	/* NOTREACHED */
	return ("unknown");
}

static int _do_call_stack(step_fn_t type, void * job, int taskid)
{
	int rc = 0;
	ListIterator i;
	struct spank_plugin *sp;
	struct spank_handle spank[1];
	const char *fn_name;

	if (!spank_stack)
		return (0);

	if (_spank_handle_init(spank, job, taskid, type) < 0) {
		error("spank: Failed to initialize handle for plugins");
		return (-1);
	}

	fn_name = _step_fn_name(type);

	i = list_iterator_create(spank_stack);
	while ((sp = list_next(i))) {
		const char *name = xbasename(sp->fq_path);

		spank->plugin = sp;

		switch (type) {
		case SPANK_INIT:
			if (sp->ops.init) {
				rc = (*sp->ops.init) (spank, sp->ac,
						      sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case SPANK_INIT_POST_OPT:
			if (sp->ops.init_post_opt) {
				rc = (*sp->ops.init_post_opt) (spank, sp->ac,
						      sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case LOCAL_USER_INIT:
			if (sp->ops.local_user_init) {
				rc = (*sp->ops.local_user_init) (spank, sp->ac,
			 				         sp->argv);
				debug2("spank: %s: %s = %d\n", name,
						fn_name, rc);
			}
			break;
		case STEP_USER_INIT:
			if (sp->ops.user_init) {
				rc = (*sp->ops.user_init) (spank, sp->ac,
							   sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case STEP_TASK_INIT_PRIV:
			if (sp->ops.task_init_privileged) {
				rc = (*sp->ops.task_init_privileged)
					(spank, sp->ac, sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case STEP_USER_TASK_INIT:
			if (sp->ops.user_task_init) {
				rc = (*sp->ops.user_task_init) (spank,
								sp->ac,
								sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case STEP_TASK_POST_FORK:
			if (sp->ops.task_post_fork) {
				rc = (*sp->ops.task_post_fork) (spank,
								sp->ac,
								sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		case STEP_TASK_EXIT:
			if (sp->ops.task_exit) {
				rc = (*sp->ops.task_exit) (spank, sp->ac,
							   sp->argv);
				debug2("spank: %s: %s = %d", name, fn_name,
				       rc);
			}
			break;
		case SPANK_EXIT:
			if (sp->ops.exit) {
				rc = (*sp->ops.exit) (spank, sp->ac,
						      sp->argv);
				debug2("spank: %s: %s = %d\n", name,
				       fn_name, rc);
			}
			break;
		}

		if ((rc < 0) && sp->required) {
			error("spank: required plugin %s: "
			      "%s() failed with rc=%d", name, fn_name, rc);
			break;
		} else
			rc = 0;
	}

	list_iterator_destroy(i);

	return (rc);
}

int _spank_init(enum spank_context_type context, slurmd_job_t * job)
{
	slurm_ctl_conf_t *conf = slurm_conf_lock();
	const char *path = conf->plugstack;
	default_spank_path = conf->plugindir;
	slurm_conf_unlock();

	spank_ctx = context;

	/*
	 *  A nonexistent spank config is not an error, but
	 *   abort on any other access failures
	 */
	if (access (path, R_OK) < 0) {
		if (errno == ENOENT)
			return (0);
		error ("spank: Unable to open config file `%s': %m", path);
		return (-1);
	}

	if (_spank_stack_create(path, &spank_stack) < 0) {
		error("spank: failed to create plugin stack");
		return (-1);
	}

	if (_do_call_stack(SPANK_INIT, job, -1) < 0)
		return (-1);

	/*
	 *  Nothing more to do unless we are in remote context:
	 */
	if (spank_ctx != S_CTX_REMOTE)
		return (0);

	/*
	 *  Remote-specific code:
	 */
	if (!job) {
		error("spank: spank_init called without job reference!");
		return (-1);
	}

	/*
	 *  Get any remote options from job launch message:
	 */
	if (spank_get_remote_options(job->options) < 0) {
		error("spank: Unable to get remote options");
		return (-1);
	}

	/*
	 *  Get any remote option passed thru environment
	 */
	if (spank_get_remote_options_env(job->env) < 0) {
		error("spank: Unable to get remote options from environment");
		return (-1);
	}

	/*
	 *  Now that all options have been processed, we can
	 *   call the post_opt handlers here in remote context.
	 */
	return (_do_call_stack(SPANK_INIT_POST_OPT, job, -1) < 0);
}

int spank_init (slurmd_job_t * job)
{
	if (job)
		return _spank_init (S_TYPE_REMOTE, job);
	else
		return _spank_init (S_TYPE_LOCAL, NULL);
}

int spank_init_allocator (void)
{
	return _spank_init (S_TYPE_ALLOCATOR, NULL);
}

int spank_init_post_opt (void)
{
	/*
	 *  In allocator context, set remote options in env here.
	 */
	if (spank_ctx == S_TYPE_ALLOCATOR)
		spank_set_remote_options_env();

	return (_do_call_stack(SPANK_INIT_POST_OPT, NULL, -1));
}

int spank_user(slurmd_job_t * job)
{
	return (_do_call_stack(STEP_USER_INIT, job, -1));
}

int spank_local_user(struct spank_launcher_job_info *job)
{
	return (_do_call_stack(LOCAL_USER_INIT, job, -1));
}

int spank_task_privileged(slurmd_job_t *job, int taskid)
{
	return (_do_call_stack(STEP_TASK_INIT_PRIV, job, taskid));
}

int spank_user_task(slurmd_job_t * job, int taskid)
{
	return (_do_call_stack(STEP_USER_TASK_INIT, job, taskid));
}

int spank_task_post_fork(slurmd_job_t * job, int taskid)
{
	return (_do_call_stack(STEP_TASK_POST_FORK, job, taskid));
}

int spank_task_exit(slurmd_job_t * job, int taskid)
{
	return (_do_call_stack(STEP_TASK_EXIT, job, taskid));
}

int spank_fini(slurmd_job_t * job)
{
	int rc = _do_call_stack(SPANK_EXIT, job, -1);

	if (option_cache)
		list_destroy(option_cache);
	if (spank_stack)
		list_destroy(spank_stack);

	return (rc);
}

/*
 *  SPANK options functions
 */

static int _spank_next_option_val(void)
{
	int optval;
	slurm_mutex_lock(&spank_mutex);
	optval = spank_optval++;
	slurm_mutex_unlock(&spank_mutex);
	return (optval);
}

static struct spank_option * _spank_option_copy(struct spank_option *opt)
{
	struct spank_option *copy = xmalloc (sizeof (*copy));

	memset (copy, 0, sizeof (*copy));

	copy->name = xstrdup (opt->name);
	copy->has_arg = opt->has_arg;
	copy->val = opt->val;
	copy->cb = opt->cb;

	if (opt->arginfo)
		copy->arginfo = xstrdup (opt->arginfo);
	if (opt->usage)
		copy->usage = xstrdup (opt->usage);

	return (copy);
}

static void _spank_option_destroy(struct spank_option *opt)
{
	xfree (opt->name);
	xfree (opt->arginfo);
	xfree (opt->usage);
	xfree (opt);
}

static struct spank_plugin_opt *_spank_plugin_opt_create(struct
							 spank_plugin *p,
							 struct
							 spank_option *opt,
							 int disabled)
{
	struct spank_plugin_opt *spopt = xmalloc(sizeof(*spopt));
	spopt->opt = _spank_option_copy (opt);
	spopt->plugin = p;
	spopt->optval = _spank_next_option_val();
	spopt->found = 0;
	spopt->optarg = NULL;

	spopt->disabled = disabled;

	return (spopt);
}

void _spank_plugin_opt_destroy(struct spank_plugin_opt *spopt)
{
	_spank_option_destroy (spopt->opt);
	xfree(spopt->optarg);
	xfree(spopt);
}

static int _opt_by_val(struct spank_plugin_opt *opt, int *optvalp)
{
	return (opt->optval == *optvalp);
}

static int _opt_by_name(struct spank_plugin_opt *opt, char *optname)
{
	return (strcmp(opt->opt->name, optname) == 0);
}

static int
_spank_option_register(struct spank_plugin *p, struct spank_option *opt)
{
	int disabled = 0;
	struct spank_plugin_opt *spopt;

	if (!option_cache) {
		option_cache =
		    list_create((ListDelF) _spank_plugin_opt_destroy);
	}

	spopt = list_find_first(option_cache,
			(ListFindF) _opt_by_name, opt->name);
	if (spopt) {
		struct spank_plugin *q = spopt->plugin;
		info("spank: option \"%s\" provided by both %s and %s",
				opt->name, xbasename(p->fq_path),
				xbasename(q->fq_path));
		/*
		 *  Disable this option, but still cache it, in case
		 *    options are loaded in a different order on the
		 *    remote side.
		 */
		disabled = 1;
	}

	if ((strlen(opt->name) > SPANK_OPTION_MAXLEN)) {
		error("spank: option \"%s\" provided by %s too long. Ignoring.",
			       	opt->name, p->name);
		return (ESPANK_NOSPACE);
	}

	verbose("SPANK: appending plugin option \"%s\"\n", opt->name);
	list_append(option_cache, _spank_plugin_opt_create(p, opt, disabled));

	return (ESPANK_SUCCESS);
}

spank_err_t spank_option_register(spank_t sp, struct spank_option *opt)
{
	if (sp->phase != SPANK_INIT)
		return (ESPANK_BAD_ARG);

	if (!sp->plugin)
		error ("Uh, oh, no current plugin!");

	if (!opt || !opt->name || !opt->usage)
		return (ESPANK_BAD_ARG);

	return (_spank_option_register(sp->plugin, opt));
}

static int _spank_plugin_options_cache(struct spank_plugin *p)
{
	struct spank_option *opt = p->opts;

	if ((opt == NULL) || opt->name == NULL)
		return (0);

	for (; opt && opt->name != NULL; opt++)
		_spank_option_register(p, opt);

	return (0);
}

static int _add_one_option(struct option **optz, struct spank_plugin_opt *spopt)
{
	struct option opt;

	opt.name = spopt->opt->name;
	opt.has_arg = spopt->opt->has_arg;
	opt.flag = NULL;
	opt.val = spopt->optval;

	if (optz_add(optz, &opt) < 0) {
		if (errno == EEXIST) {
			error ("Ingoring conflicting option \"%s\" "
			       "in plugin \"%s\"",
			       opt.name, spopt->plugin->name);
		} else {
			error("Unable to add option \"%s\" "
			      "from plugin \"%s\"",
			      opt.name, spopt->plugin->name);
		}

		return (-1);
	}

	return (0);
}


struct option *spank_option_table_create(const struct option *orig)
{
	struct spank_plugin_opt *spopt;
	struct option *opts = NULL;
	ListIterator i = NULL;

	opts = optz_create();

	/*
	 *  Start with original options:
	 */
	if ((orig != NULL) && (optz_append(&opts, orig) < 0)) {
		optz_destroy(opts);
		return (NULL);
	}

	if (option_cache == NULL || (list_count(option_cache) == 0))
		return (opts);

	i = list_iterator_create(option_cache);
	while ((spopt = list_next(i))) {
		if (!spopt->disabled && (_add_one_option (&opts, spopt) < 0))
			spopt->disabled = 1;
	}

	list_iterator_destroy(i);

	return (opts);
}

void spank_option_table_destroy(struct option *optz)
{
	optz_destroy(optz);
}

int spank_process_option(int optval, const char *arg)
{
	struct spank_plugin_opt *opt;
	int rc = 0;

	if (option_cache == NULL || (list_count(option_cache) == 0))
		return (-1);

	opt =
	    list_find_first(option_cache, (ListFindF) _opt_by_val,
			    &optval);

	if (!opt)
		return (-1);

	/*
	 *  Call plugin callback if such a one exists
	 */
	if (opt->opt->cb
	    && (rc = ((*opt->opt->cb) (opt->opt->val, arg, 0))) < 0)
		return (rc);

	/*
	 *  Set optarg and "found" so that option will be forwarded
	 *    to remote side.
	 */
	if (opt->opt->has_arg)
		opt->optarg = xstrdup(arg);
	opt->found = 1;

	return (0);
}

static char *
_find_word_boundary(char *str, char *from, char **next)
{
	char *p = from;

	/*
	 * Back up past any non-whitespace if we are pointing in
	 *  the middle of a word.
	 */
	while ((p != str) && !isspace (*p))
		--p;

	/*
	 * Next holds next word boundary
	 */
	*next = p+1;

	/*
	 * Now move back to the end of the previous word
	 */
	while ((p != str) && isspace (*p))
		--p;

	if (p == str) {
		*next = str;
		return (NULL);
	}

	return (p+1);
}

static char *
_get_next_segment (char **from, int width, char *buf, int bufsiz)
{
	int len;
	char * seg = *from;
	char *p;

	if (**from == '\0')
		return (NULL);

	if ((len = strlen (*from)) <= width) {
		*from = *from + len;
		return (seg);
	}

	if (!(p = _find_word_boundary (seg, *from + width, from))) {
		/*
		 *	Need to break up a word. Use user-supplied buffer.
		 */
		strlcpy (buf, seg, width+1);
		buf [width - 1]  = '-';
		/*
		 * Adjust from to character eaten by '-'
		 *  And return pointer to buf.
		 */
		*from = seg + width - 1;
		return (buf);
	}

	*p = '\0';

	return (seg);
}

static int
_term_columns ()
{
	char *val;
	int  cols = 80;

	if ((val = getenv ("COLUMNS"))) {
		char *p;
		long lval = strtol (val, &p, 10);

		if (p && (*p == '\0'))
			cols = (int) lval;
	}

	return (cols);
}

static void
_spank_opt_print(struct spank_option *opt, FILE * fp, int left_pad, int width)
{
	int n;
	char *equals = "";
	char *arginfo = "";
	char *p, *q;
	char info [81];
	char seg [81];
	char buf [4096];

	int  columns = _term_columns ();
	int  descrsiz = columns - width;

	if (opt->arginfo) {
		equals = "=";
		arginfo = opt->arginfo;
	}

	n = snprintf(info, sizeof(info), "%*s--%s%s%s",
		     left_pad, "", opt->name, equals, arginfo);

	if ((n < 0) || (n > columns)) {
		const char trunc[] = "+";
		int len = strlen(trunc);
		p = info + columns - len - 1;
		snprintf(p, len + 1, "%s", trunc);
	}


	q = buf;
	strlcpy (buf, opt->usage, sizeof (buf));

	p = _get_next_segment (&q, descrsiz, seg, sizeof (seg));

	if (n < width)
		fprintf(fp, "%-*s%s\n", width, info, p);
	else
		fprintf(fp, "\n%s\n%*s%s\n", info, width, "", p);

	/* Get remaining line-wrapped lines.
	 */
	while ((p = _get_next_segment (&q, descrsiz, seg, sizeof (seg))))
		fprintf(fp, "%*s%s\n", width, "", p);

	return;
}

int spank_print_options(FILE * fp, int left_pad, int width)
{
	struct spank_plugin_opt *p;
	ListIterator i;

	if ((option_cache == NULL) || (list_count(option_cache) == 0))
		return (0);

	fprintf(fp, "\nOptions provided by plugins:\n");

	i = list_iterator_create(option_cache);
	while ((p = list_next(i))) {
		if (p->disabled)
			continue;
		_spank_opt_print(p->opt, fp, left_pad, width);
	}
	list_iterator_destroy(i);

	return (0);
}

#define OPT_TYPE_SPANK 0x4400

static char _canonical_char (char c)
{
	if (!isalnum (c))
		return '_';
	else
		return c;
}

/*
 *  Create spank option environment variable name from option name.
 */
static char * _opt_env_name (struct spank_plugin_opt *p, char *buf, size_t siz)
{
	const char * name = p->opt->name;
	const char * pname = p->plugin->name;
	int i, n;

	strlcpy (buf, SPANK_OPTION_ENV_PREFIX, siz);

	/*
	 *  First append the plugin name associated with this option:
	 */
	n = 0;
	for (i = strlen (buf); i < siz - 1 && n < strlen (pname); i++)
	    buf[i] = _canonical_char (pname[n++]);

	/*
	 *  Append _
	 */
	buf[i] = '_';
	buf[i+1] = '\0';

	/*
	 *  Now incorporate the option name:
	 */
	n = 0;
	for (i = strlen (buf); i < siz - 1 && n < strlen (name); i++)
	    buf[i] = _canonical_char (name[n++]);
	buf[i] = '\0';

	return (buf);
}

static int _option_setenv (struct spank_plugin_opt *option)
{
	char var [1024];

	_opt_env_name (option, var, sizeof (var));

	if (setenv (var, option->optarg, 1) < 0)
	    error ("failed to set %s=%s in env", var, option->optarg);

	return (0);
}

int spank_set_remote_options_env(void)
{
	struct spank_plugin_opt *p;
	ListIterator i;

	if ((option_cache == NULL) || (list_count(option_cache) == 0))
		return (0);

	i = list_iterator_create(option_cache);
	while ((p = list_next(i))) {
		if (p->found)
			_option_setenv (p);
	}
	list_iterator_destroy(i);
	return (0);
}

int spank_set_remote_options(job_options_t opts)
{
	struct spank_plugin_opt *p;
	ListIterator i;

	if ((option_cache == NULL) || (list_count(option_cache) == 0))
		return (0);

	i = list_iterator_create(option_cache);
	while ((p = list_next(i))) {
		char optstr[1024];

		if (!p->found)
			continue;

		snprintf(optstr, sizeof(optstr), "%s:%s",
			 p->opt->name, p->plugin->name);

		job_options_append(opts, OPT_TYPE_SPANK, optstr,
				   p->optarg);
	}
	list_iterator_destroy(i);
	return (0);
}

struct opt_find_args {
	const char *optname;
	const char *plugin_name;
};

static int _opt_find(struct spank_plugin_opt *p,
		     struct opt_find_args *args)
{
	if (strcmp(p->plugin->name, args->plugin_name) != 0)
		return (0);
	if (strcmp(p->opt->name, args->optname) != 0)
		return (0);
	return (1);
}

static struct spank_plugin_opt *_find_remote_option_by_name(const char
							    *str)
{
	struct spank_plugin_opt *opt;
	struct opt_find_args args;
	char buf[256];
	char *name;

	if (strlcpy(buf, str, sizeof(buf)) >= sizeof(buf)) {
		error("plugin option \"%s\" too big. Ignoring.", str);
		return (NULL);
	}

	if (!(name = strchr(buf, ':'))) {
		error("Malformed plugin option \"%s\" received. Ignoring",
		      str);
		return (NULL);
	}

	*(name++) = '\0';

	args.optname = buf;
	args.plugin_name = name;

	opt = list_find_first(option_cache, (ListFindF) _opt_find, &args);

	if (opt == NULL) {
		error("warning: plugin \"%s\" option \"%s\" not found.",
		      name, buf);
		return (NULL);
	}

	return (opt);
}

int spank_get_remote_options_env (char **env)
{
	char var [1024];
	const char *arg;
	struct spank_plugin_opt *option;
	ListIterator i;

	if (!option_cache)
		return (0);

	i = list_iterator_create (option_cache);
	while ((option = list_next (i))) {
		struct spank_option *p = option->opt;

		if (!(arg = getenvp (env, _opt_env_name (option, var, sizeof(var)))))
			continue;

		if (p->cb && (((*p->cb) (p->val, arg, 1)) < 0))
			error ("spank: failed to process option %s=%s", p->name, arg);

		/*
		 *  Now remove the environment variable.
		 *   It is no longer needed.
		 */
		unsetenvp (env, var);

	}
	list_iterator_destroy (i);

	return (0);
}

int spank_get_remote_options(job_options_t opts)
{
	const struct job_option_info *j;

	job_options_iterator_reset(opts);
	while ((j = job_options_next(opts))) {
		struct spank_plugin_opt *opt;
		struct spank_option *p;

		if (j->type != OPT_TYPE_SPANK)
			continue;

		if (!(opt = _find_remote_option_by_name(j->option)))
			continue;

		p = opt->opt;

		if (p->cb && (((*p->cb) (p->val, j->optarg, 1)) < 0)) {
			error("spank: failed to process option %s=%s",
			      p->name, j->optarg);
		}
	}

	return (0);
}

static int tasks_execd (spank_t spank)
{
	return ( (spank->phase == STEP_TASK_POST_FORK)
	      || (spank->phase == STEP_TASK_EXIT)
	      || (spank->phase == SPANK_EXIT) );
}

static spank_err_t
global_to_local_id (slurmd_job_t *job, uint32_t gid, uint32_t *p2uint32)
{
	int i;
	*p2uint32 = (uint32_t) -1;
	if (gid >= job->nprocs)
		return (ESPANK_BAD_ARG);
	for (i = 0; i < job->ntasks; i++) {
		if (job->task[i]->gtid == gid) {
			*p2uint32 = job->task[i]->id;
			return (ESPANK_SUCCESS);
		}
	}
	return (ESPANK_NOEXIST);
}


/*
 *  Return 1 if spank_item_t is valid for S_TYPE_LOCAL
 */
static int _valid_in_local_context (spank_item_t item)
{
	int rc = 0;
	switch (item) {
	case S_JOB_UID:
	case S_JOB_GID:
	case S_JOB_ID:
	case S_JOB_STEPID:
	case S_JOB_ARGV:
	case S_JOB_ENV:
	case S_JOB_TOTAL_TASK_COUNT:
	case S_JOB_NNODES:
		rc = 1;
		break;
	default:
		rc = 0;
	}
	return (rc);
}

static int _valid_in_allocator_context (spank_item_t item)
{
	switch (item) {
	  case S_JOB_UID:
	  case S_JOB_GID:
		  return 1;
	  default:
		  return 0;
	}
}

static spank_err_t _check_spank_item_validity (spank_item_t item, void *job)
{
	/*
	 *  Valid in all contexts:
	 */
	switch (item) {
	  case S_SLURM_VERSION:
	  case S_SLURM_VERSION_MAJOR:
	  case S_SLURM_VERSION_MINOR:
	  case S_SLURM_VERSION_MICRO:
		  return ESPANK_SUCCESS;
	  default:
		  break; /* fallthru */
	}

	if (spank_ctx == S_TYPE_LOCAL) {
		if (!_valid_in_local_context (item))
			return ESPANK_NOT_REMOTE;
		else if (job == NULL)
			return ESPANK_NOT_AVAIL;
	}
	else if (spank_ctx == S_TYPE_ALLOCATOR) {
		if (_valid_in_allocator_context (item)) {
			if (job)
				return ESPANK_SUCCESS;
			else
				return ESPANK_NOT_AVAIL;
		}
		else if (_valid_in_local_context (item))
			return ESPANK_BAD_ARG;
		else
			return ESPANK_NOT_REMOTE;
	}

	/* All items presumably valid in remote context */
	return ESPANK_SUCCESS;
}

/*
 *  Global functions for SPANK plugins
 */

const char * spank_strerror (spank_err_t err)
{
	switch (err) {
	case ESPANK_SUCCESS:
		return "Success";
	case ESPANK_ERROR:
		return "Generic error";
	case ESPANK_BAD_ARG:
		return "Bad argument";
	case ESPANK_NOT_TASK:
		return "Not in task context";
	case ESPANK_ENV_EXISTS:
		return "Environment variable exists";
	case ESPANK_ENV_NOEXIST:
		return "No such environment variable";
	case ESPANK_NOSPACE:
		return "Buffer too small";
	case ESPANK_NOT_REMOTE:
		return "Valid only in remote context";
	case ESPANK_NOEXIST:
		return "Id/PID does not exist on this node";
	case ESPANK_NOT_EXECD:
		return "Lookup by PID requested, but no tasks running";
	case ESPANK_NOT_AVAIL:
		return "Item not available from this callback";
	case ESPANK_NOT_LOCAL:
		return "Valid only in local or allocator context";
	}

	return "Unknown";
}

int spank_symbol_supported (const char *name)
{
	int i;

	if ((name == NULL))
		return (-1);

	for (i = 0; i < n_spank_syms; i++) {
		if (strcmp (spank_syms [i], name) == 0)
			return (1);
	}

	return (0);
}

int spank_remote(spank_t spank)
{
	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (-1);
	if (spank_ctx == S_TYPE_REMOTE)
		return (1);
	else
		return (0);
}

spank_context_t spank_context (void)
{
	switch (spank_ctx) {
	  case S_TYPE_REMOTE:
		  return S_CTX_REMOTE;
	  case S_TYPE_LOCAL:
		  return S_CTX_LOCAL;
	  case S_TYPE_ALLOCATOR:
		  return S_CTX_ALLOCATOR;
	  default:
		  return S_CTX_ERROR;
	}

	return S_CTX_ERROR;
}

spank_err_t spank_get_item(spank_t spank, spank_item_t item, ...)
{
	int *p2int;
	uint32_t *p2uint32;
	uint32_t  uint32;
	uint16_t *p2uint16;
	uid_t *p2uid;
	gid_t *p2gid;
	gid_t **p2gids;
	pid_t *p2pid;
	pid_t  pid;
	char ***p2argv;
	char **p2str;
	char **p2vers;
	slurmd_task_info_t *task;
	slurmd_job_t  *slurmd_job = NULL;
	struct spank_launcher_job_info *launcher_job = NULL;
	va_list vargs;
	spank_err_t rc = ESPANK_SUCCESS;

	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	/*
	 *  Check for validity of the given item in the current context
	 */
	rc = _check_spank_item_validity (item, spank->job);
	if (rc != ESPANK_SUCCESS)
		return (rc);

	if (spank_ctx == S_TYPE_LOCAL)
		launcher_job = spank->job;
	else if (spank_ctx == S_TYPE_REMOTE)
		slurmd_job = spank->job;

	va_start(vargs, item);
	switch (item) {
	case S_JOB_UID:
		p2uid = va_arg(vargs, uid_t *);
		if (spank_ctx == S_TYPE_LOCAL)
			*p2uid = launcher_job->uid;
		else if (spank_ctx == S_TYPE_REMOTE)
			*p2uid = slurmd_job->uid;
		else
			*p2uid = getuid();
		break;
	case S_JOB_GID:
		p2gid = va_arg(vargs, gid_t *);
		if (spank_ctx == S_TYPE_LOCAL)
			*p2gid = launcher_job->gid;
		else if (spank_ctx == S_TYPE_REMOTE)
			*p2gid = slurmd_job->gid;
		else
			*p2gid = getgid();
		break;
	case S_JOB_SUPPLEMENTARY_GIDS:
		p2gids = va_arg(vargs, gid_t **);
		p2int = va_arg(vargs, int *);
		*p2gids = slurmd_job->gids;
		*p2int = slurmd_job->ngids;
		break;
	case S_JOB_ID:
		p2uint32 = va_arg(vargs, uint32_t *);
		if (spank_ctx == S_TYPE_LOCAL)
			*p2uint32 = launcher_job->jobid;
		else
			*p2uint32 = slurmd_job->jobid;
		break;
	case S_JOB_STEPID:
		p2uint32 = va_arg(vargs, uint32_t *);
		if (spank_ctx == S_TYPE_LOCAL)
			*p2uint32 = launcher_job->stepid;
		else
			*p2uint32 = slurmd_job->stepid;
		break;
	case S_JOB_NNODES:
		p2uint32 = va_arg(vargs, uint32_t *);
		if (spank_ctx == S_TYPE_LOCAL) {
			if (launcher_job->step_layout)
				*p2uint32 = launcher_job->step_layout->
					    node_cnt;
			else {
				*p2uint32 = 0;
				rc = ESPANK_ENV_NOEXIST;
			}
		} else
			*p2uint32 = slurmd_job->nnodes;
		break;
	case S_JOB_NODEID:
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = slurmd_job->nodeid;
		break;
	case S_JOB_LOCAL_TASK_COUNT:
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = slurmd_job->ntasks;
		break;
	case S_JOB_TOTAL_TASK_COUNT:
		p2uint32 = va_arg(vargs, uint32_t *);
		if (spank_ctx == S_TYPE_LOCAL) {
			if (launcher_job->step_layout)
				*p2uint32 = launcher_job->step_layout->
					    task_cnt;
			else {
				*p2uint32 = 0;
				rc = ESPANK_ENV_NOEXIST;
			}
		} else
			*p2uint32 = slurmd_job->nprocs;
		break;
	case S_JOB_NCPUS:
		p2uint16 = va_arg(vargs, uint16_t *);
		*p2uint16 = slurmd_job->cpus;
		break;
	case S_STEP_CPUS_PER_TASK:
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = slurmd_job->cpus_per_task;
		break;
	case S_JOB_ARGV:
		p2int = va_arg(vargs, int *);
		p2argv = va_arg(vargs, char ***);
		if (spank_ctx == S_TYPE_LOCAL) {
			*p2int = launcher_job->argc;
			*p2argv = launcher_job->argv;
		} else {
			*p2int = slurmd_job->argc;
			*p2argv = slurmd_job->argv;
		}
		break;
	case S_JOB_ENV:
		p2argv = va_arg(vargs, char ***);
		*p2argv = slurmd_job->env;
		break;
	case S_TASK_ID:
		p2int = va_arg(vargs, int *);
		if (!spank->task) {
			*p2int = -1;
			rc = ESPANK_NOT_TASK;
		} else {
			*p2int = spank->task->id;
		}
		break;
	case S_TASK_GLOBAL_ID:
		p2uint32 = va_arg(vargs, uint32_t *);
		if (!spank->task) {
			rc = ESPANK_NOT_TASK;
		} else {
			*p2uint32 = spank->task->gtid;
		}
		break;
	case S_TASK_EXIT_STATUS:
		p2int = va_arg(vargs, int *);
		if (!spank->task || !spank->task->exited) {
			rc = ESPANK_NOT_TASK;
		} else {
			*p2int = spank->task->estatus;
		}
		break;
	case S_TASK_PID:
		p2pid = va_arg(vargs, pid_t *);
		if (!spank->task) {
			rc = ESPANK_NOT_TASK;
			*p2pid = 0;
		} else {
			*p2pid = spank->task->pid;
		}
		break;
	case S_JOB_PID_TO_GLOBAL_ID:
		pid = va_arg(vargs, pid_t);
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = (uint32_t) -1;

		if (!tasks_execd(spank))
			rc = ESPANK_NOT_EXECD;
		else if (!(task = job_task_info_by_pid (slurmd_job, pid)))
			rc = ESPANK_NOEXIST;
		else
			*p2uint32 = task->gtid;
		break;
	case S_JOB_PID_TO_LOCAL_ID:
		pid = va_arg(vargs, pid_t);
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = (uint32_t) -1;

		if (!tasks_execd(spank))
			rc = ESPANK_NOT_EXECD;
		else if (!(task = job_task_info_by_pid (slurmd_job, pid)))
			rc = ESPANK_NOEXIST;
		else
			*p2uint32 = task->id;
		break;
	case S_JOB_LOCAL_TO_GLOBAL_ID:
		uint32 = va_arg(vargs, uint32_t);
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = (uint32_t) -1;

		if ((uint32 <= slurmd_job->ntasks) &&
		    slurmd_job->task && slurmd_job->task[uint32]) {
			*p2uint32 = slurmd_job->task[uint32]->gtid;
		} else
			rc = ESPANK_NOEXIST;
		break;
	case S_JOB_GLOBAL_TO_LOCAL_ID:
		uint32 = va_arg(vargs, uint32_t);
		p2uint32 = va_arg(vargs, uint32_t *);
		rc = global_to_local_id (slurmd_job, uint32, p2uint32);
		break;
	case S_JOB_ALLOC_CORES:
		p2str = va_arg(vargs, char **);
		*p2str = slurmd_job->alloc_cores;
		break;
	case S_JOB_ALLOC_MEM:
		p2uint32 = va_arg(vargs, uint32_t *);
		*p2uint32 = slurmd_job->job_mem;
		break;
	case S_SLURM_VERSION:
		p2vers = va_arg(vargs, char  **);
		*p2vers = SLURM_VERSION;
		break;
	case S_SLURM_VERSION_MAJOR:
		p2vers = va_arg(vargs, char  **);
		*p2vers = SLURM_MAJOR;
		break;
	case S_SLURM_VERSION_MINOR:
		p2vers = va_arg(vargs, char  **);
		*p2vers = SLURM_MINOR;
		break;
	case S_SLURM_VERSION_MICRO:
		p2vers = va_arg(vargs, char  **);
		*p2vers = SLURM_MICRO;
		break;
	default:
		rc = ESPANK_BAD_ARG;
		break;
	}
	va_end(vargs);
	return (rc);
}

spank_err_t spank_getenv(spank_t spank, const char *var, char *buf,
			 int len)
{
	char *val;

	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if (spank_ctx != S_TYPE_REMOTE)
		return (ESPANK_NOT_REMOTE);

	if (spank->job == NULL)
		return (ESPANK_BAD_ARG);

	if (len < 0)
		return (ESPANK_BAD_ARG);

	if (!(val = getenvp(((slurmd_job_t *) spank->job)->env, var)))
		return (ESPANK_ENV_NOEXIST);

	if (strlcpy(buf, val, len) >= len)
		return (ESPANK_NOSPACE);

	return (ESPANK_SUCCESS);
}

spank_err_t spank_setenv(spank_t spank, const char *var, const char *val,
			 int overwrite)
{
	slurmd_job_t * job;

	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if (spank_ctx != S_TYPE_REMOTE)
		return (ESPANK_NOT_REMOTE);

	if (spank->job == NULL)
		return (ESPANK_BAD_ARG);

	if ((var == NULL) || (val == NULL))
		return (ESPANK_BAD_ARG);

	job = spank->job;

	if (getenvp(job->env, var) && !overwrite)
		return (ESPANK_ENV_EXISTS);

	if (setenvf(&job->env, var, "%s", val) < 0)
		return (ESPANK_ERROR);

	return (ESPANK_SUCCESS);
}

spank_err_t spank_unsetenv (spank_t spank, const char *var)
{
	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if (spank_ctx != S_TYPE_REMOTE)
		return (ESPANK_NOT_REMOTE);

	if (spank->job == NULL)
		return (ESPANK_BAD_ARG);

	if (var == NULL)
		return (ESPANK_BAD_ARG);

	unsetenvp(((slurmd_job_t *) spank->job)->env, var);

	return (ESPANK_SUCCESS);
}


/*
 *  Dynamically loaded versions of spank_*_job_env
 */
const char *dyn_spank_get_job_env (const char *name)
{
	void *h = dlopen (NULL, 0);
	char * (*fn)(const char *n);

	fn = dlsym (h, "spank_get_job_env");
	if (fn == NULL)
		return NULL;

	return ((*fn) (name));
}

int dyn_spank_set_job_env (const char *n, const char *v, int overwrite)
{
	void *h = dlopen (NULL, 0);
	int (*fn)(const char *n, const char *v, int overwrite);

	fn = dlsym (h, "spank_set_job_env");
	if (fn == NULL)
		return (-1);

	return ((*fn) (n, v, overwrite));
}

extern int dyn_spank_unset_job_env (const char *n)
{
	void *h = dlopen (NULL, 0);
	int (*fn)(const char *n);

	fn = dlsym (h, "spank_unset_job_env");
	if (fn == NULL)
		return (-1);

	return ((*fn) (n));
}


spank_err_t spank_job_control_getenv (spank_t spank, const char *var,
			char *buf, int len)
{
	const char *val;
	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if ((var == NULL) || (buf == NULL) || (len <= 0))
		return (ESPANK_BAD_ARG);

	if (spank_remote (spank))
		return (ESPANK_NOT_LOCAL);

	val = dyn_spank_get_job_env (var);
	if (val == NULL)
		return (ESPANK_ENV_NOEXIST);

	if (strlcpy (buf, val, len) >= len)
		return (ESPANK_NOSPACE);

	return (ESPANK_SUCCESS);
}

spank_err_t spank_job_control_setenv (spank_t spank, const char *var,
			const char *val, int overwrite)
{
	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if ((var == NULL) || (val == NULL))
		return (ESPANK_BAD_ARG);

	if (spank_remote (spank))
		return (ESPANK_NOT_LOCAL);

	if (dyn_spank_set_job_env (var, val, overwrite) < 0)
		return (ESPANK_BAD_ARG);

	return (ESPANK_SUCCESS);
}

spank_err_t spank_job_control_unsetenv (spank_t spank, const char *var)
{
	if ((spank == NULL) || (spank->magic != SPANK_MAGIC))
		return (ESPANK_BAD_ARG);

	if (var == NULL)
		return (ESPANK_BAD_ARG);

	if (spank_remote (spank))
		return (ESPANK_NOT_LOCAL);

	if (dyn_spank_unset_job_env (var) < 0)
		return (ESPANK_BAD_ARG);

	return (ESPANK_SUCCESS);
}
