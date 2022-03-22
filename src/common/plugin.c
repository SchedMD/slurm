/*****************************************************************************\
 *  plugin.c - plugin architecture implementation.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
 *  CODE-OCEC-09-009. All rights reserved.
 *  Portions Copyright (C) 2012 SchedMD LLC.
 *  Written by Danny Auble <da@schedmd.com>
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

#include "config.h"

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "src/common/xmalloc.h"
#include "src/common/log.h"
#include "src/common/plugrack.h"
#include "src/common/read_config.h"
#include "src/common/strlcpy.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_api.h"
#include "slurm/slurm_errno.h"

strong_alias(plugin_get_syms,         slurm_plugin_get_syms);
strong_alias(plugin_load_and_link,    slurm_plugin_load_and_link);
strong_alias(plugin_strerror,         slurm_plugin_strerror);
strong_alias(plugin_unload,           slurm_plugin_unload);

/* dlerror() on AIX sometimes fails, revert to strerror() as needed */
static char *_dlerror(void)
{
	int error_code = errno;
	char *rc = dlerror();

	if ((rc == NULL) || (rc[0] == '\0'))
		rc = strerror(error_code);

	return rc;
}

const char * plugin_strerror(plugin_err_t e)
{
	switch (e) {
		case EPLUGIN_SUCCESS:
			return ("Success");
		case EPLUGIN_NOTFOUND:
			return ("Plugin file not found");
		case EPLUGIN_ACCESS_ERROR:
			return ("Plugin access denied");
		case EPLUGIN_DLOPEN_FAILED:
			return ("Dlopen of plugin file failed");
		case EPLUGIN_INIT_FAILED:
			return ("Plugin init() callback failed");
		case EPLUGIN_MISSING_NAME:
			return ("Plugin name/type/version symbol missing");
		case EPLUGIN_BAD_VERSION:
			return ("Incompatible plugin version");
	}
	error("%s: Unknown plugin error: %d", __func__, e);
	return ("Unknown error");
}

static plugin_err_t _verify_syms(plugin_handle_t plug, char *plugin_type,
				 const size_t type_len, const char *caller,
				 const char *fq_path)
{
	char *type, *name;
	uint32_t *version;
	uint32_t mask = 0xffffff;

	if (!(name = dlsym(plug, PLUGIN_NAME))) {
		verbose("%s: %s is not a Slurm plugin: %s",
			caller, fq_path, _dlerror());
		return EPLUGIN_MISSING_NAME;
	}

	if (!(type = dlsym(plug, PLUGIN_TYPE))) {
		verbose("%s: %s is not a Slurm plugin: %s",
			caller, fq_path, _dlerror());
		return EPLUGIN_MISSING_NAME;
	}

	if (plugin_type) {
		strlcpy(plugin_type, type, type_len);
	}

	version = dlsym(plug, PLUGIN_VERSION);
	if (!version) {
		verbose("%s: %s symbol not found in %s: %s",
			caller, PLUGIN_VERSION, fq_path, _dlerror());
		return EPLUGIN_MISSING_NAME;
	}

	debug3("%s->%s: found Slurm plugin name:%s type:%s version:0x%x",
	       caller, __func__, name, type, *version);

	/* SPANK plugins need to only match major and minor */
	if (!xstrcmp(type, "spank"))
		mask = 0xffff00;

	if ((*version & mask) != (SLURM_VERSION_NUMBER & mask)) {
		int plugin_major, plugin_minor, plugin_micro;
		plugin_major = SLURM_VERSION_MAJOR(*version);
		plugin_minor = SLURM_VERSION_MINOR(*version);
		plugin_micro = SLURM_VERSION_MICRO(*version);

		info("%s: Incompatible Slurm plugin %s version (%d.%02d.%d)",
		     caller, fq_path, plugin_major, plugin_minor, plugin_micro);
		return EPLUGIN_BAD_VERSION;
	}

	return EPLUGIN_SUCCESS;
}

extern plugin_err_t plugin_peek(const char *fq_path, char *plugin_type,
				const size_t type_len, uint32_t *plugin_version)
{
	plugin_err_t rc;
	plugin_handle_t plug;

	if (!(plug = dlopen(fq_path, RTLD_LAZY))) {
		debug3("%s: dlopen(%s): %s", __func__, fq_path, _dlerror());
		return EPLUGIN_DLOPEN_FAILED;
	}

	rc = _verify_syms(plug, plugin_type, type_len, __func__, fq_path);
	dlclose(plug);
	return rc;
}

plugin_err_t
plugin_load_from_file(plugin_handle_t *p, const char *fq_path)
{
	plugin_err_t rc;
	plugin_handle_t plug;
	int (*init)(void);

	*p = PLUGIN_INVALID_HANDLE;

	/*
	 * Try to open the shared object.
	 *
	 * Use RTLD_LAZY to allow plugins to use symbols that may be
	 * defined in only one slurm entity (e.g. srun and not slurmd),
	 * when the use of that symbol is restricted to within the
	 * entity from which it is available. (i.e. srun symbols are only
	 * used in the context of srun, not slurmd.)
	 *
	 */
	plug = dlopen(fq_path, RTLD_LAZY);
	if (plug == NULL) {
		error("plugin_load_from_file: dlopen(%s): %s",
		      fq_path,
		      _dlerror());
		return EPLUGIN_DLOPEN_FAILED;
	}

	rc = _verify_syms(plug, NULL, 0, __func__, fq_path);
	if (rc != EPLUGIN_SUCCESS) {
		dlclose(plug);
		return rc;
	}

	/*
	 * Now call its init() function, if present.  If the function
	 * returns nonzero, unload the plugin and signal an error.
	 */
	if ((init = dlsym(plug, "init")) != NULL) {
		if ((*init)() != 0) {
			dlclose(plug);
			return EPLUGIN_INIT_FAILED;
		}
	}

	*p = plug;
	return EPLUGIN_SUCCESS;
}

/*
 * Load plugin and setup linking
 * IN type_name - name of plugin
 * IN n_syms - number of pointers in ptrs
 * IN names - pointer list of symbols to link
 * IN ptr - list of pointers to set with pointers given in names
 * RET opaque ptr to handler or PLUGIN_INVALID_HANDLE on error
 */
plugin_handle_t
plugin_load_and_link(const char *type_name, int n_syms,
		     const char *names[], void *ptrs[])
{
	plugin_handle_t plug = PLUGIN_INVALID_HANDLE;
	struct stat st;
	char *head = NULL, *dir_array = NULL, *so_name = NULL;
	char *file_name = NULL;
	int i = 0;
	plugin_err_t err = EPLUGIN_NOTFOUND;

	if (!type_name)
		return plug;
	so_name = xstrdup_printf("%s.so", type_name);
	while (so_name[i]) {
		if (so_name[i] == '/')
			so_name[i] = '_';
		i++;
	}
	if (!(dir_array = xstrdup(slurm_conf.plugindir))) {
		error("plugin_load_and_link: No plugin dir given");
		xfree(so_name);
		return plug;
	}

	head = dir_array;
	for (i = 0; ; i++) {
		bool got_colon = 0;
		if (dir_array[i] == ':') {
			dir_array[i] = '\0';
			got_colon = 1;
		} else if (dir_array[i] != '\0')
			continue;

		file_name = xstrdup_printf("%s/%s", head, so_name);
		debug3("Trying to load plugin %s", file_name);
		if ((stat(file_name, &st) < 0) || (!S_ISREG(st.st_mode))) {
			debug4("%s: Does not exist or not a regular file.",
			       file_name);
			xfree(file_name);
			err = EPLUGIN_NOTFOUND;
		} else {
			if ((err = plugin_load_from_file(&plug, file_name))
			   == EPLUGIN_SUCCESS) {
				if (plugin_get_syms(plug, n_syms,
						    names, ptrs) >= n_syms) {
					debug3("Success.");
					xfree(file_name);
					break;
				} else {
					/*
					 * Plugin loading failed part way
					 * through loading, it is unknown what
					 * actually happened but now process
					 * memory is suspect and we are going to
					 * abort since this should only ever
					 * happen during development.
					 */
					fatal("%s: Plugin loading failed due to missing symbols. Plugin is corrupted.",
					      __func__);
				}
			} else
				plug = PLUGIN_INVALID_HANDLE;
			xfree(file_name);
		}

		if (got_colon) {
			head = dir_array + i + 1;
		} else
			break;
	}

	xfree(dir_array);
	xfree(so_name);
	errno = err;
	return plug;
}
/*
 * Must test plugin validity before doing dlopen() and dlsym()
 * operations because some implementations of these functions
 * crash if the library handle is not valid.
 */

void
plugin_unload( plugin_handle_t plug )
{
	void (*fini)(void);

	if ( plug != PLUGIN_INVALID_HANDLE ) {
		if ( ( fini = dlsym( plug, "fini" ) ) != NULL ) {
			(*fini)();
		}
#ifndef MEMORY_LEAK_DEBUG
/**************************************************************************\
 * To test for memory leaks, set MEMORY_LEAK_DEBUG to 1 using
 * "configure --enable-memory-leak-debug" then execute
 *
 * Note that without --enable-memory-leak-debug the daemon will
 * unload the shared objects at exit thus preventing valgrind
 * to display the stack where the eventual leaks may be.
 * It is always best to test with and without --enable-memory-leak-debug.
\**************************************************************************/
		(void) dlclose( plug );
#endif
	}
}


void *
plugin_get_sym( plugin_handle_t plug, const char *name )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return dlsym( plug, name );
	else
		return NULL;
}

const char *
plugin_get_name( plugin_handle_t plug )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return (const char *) dlsym( plug, PLUGIN_NAME );
	else
		return NULL;
}

const char *
plugin_get_type( plugin_handle_t plug )
{
	if ( plug != PLUGIN_INVALID_HANDLE )
		return (const char *) dlsym( plug, PLUGIN_TYPE );
	else
		return NULL;
}

uint32_t
plugin_get_version( plugin_handle_t plug )
{
	uint32_t *ptr;

	if (plug == PLUGIN_INVALID_HANDLE)
		return 0;
	ptr = (uint32_t *) dlsym(plug, PLUGIN_VERSION);
	return ptr ? *ptr : 0;
}

int
plugin_get_syms( plugin_handle_t plug,
		 int n_syms,
		 const char *names[],
		 void *ptrs[] )
{
	int i, count;

	count = 0;
	for ( i = 0; i < n_syms; ++i ) {
		ptrs[ i ] = dlsym( plug, names[ i ] );
		if ( ptrs[ i ] )
			++count;
		else
			debug3("Couldn't find sym '%s' in the plugin",
			       names[ i ]);
	}

	return count;
}

/*
 * Create a priority context
 */
extern plugin_context_t *plugin_context_create(
	const char *plugin_type, const char *uler_type,
	void *ptrs[], const char *names[], size_t names_size)
{
	plugin_context_t *c;
	int n_names;

	if (!uler_type) {
		debug3("plugin_context_create: no uler type");
		return NULL;
	} else if (!plugin_type) {
		debug3("plugin_context_create: no plugin type");
		return NULL;
	} else if (!names) {
		error("plugin_context_create: no symbols given for plugin %s",
		      plugin_type);
		return NULL;
	} else if (!ptrs) {
		error("plugin_context_create: no ptrs given for plugin %s",
		      plugin_type);
		return NULL;
	}

	c = xmalloc(sizeof(plugin_context_t));
	c->type = xstrdup(uler_type);
	c->cur_plugin = PLUGIN_INVALID_HANDLE;

	n_names = names_size / sizeof(char *);

	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->type, n_names, names, ptrs);
	if (c->cur_plugin != PLUGIN_INVALID_HANDLE)
		return c;

	if (errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->type, plugin_strerror(errno));
		goto fail;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->type);

	/* Get plugin list. */
	if (!c->plugin_list) {
		c->plugin_list = plugrack_create(plugin_type);
		plugrack_read_dir(c->plugin_list, slurm_conf.plugindir);
	}

	c->cur_plugin = plugrack_use_by_type(c->plugin_list, c->type);
	if (c->cur_plugin == PLUGIN_INVALID_HANDLE) {
		error("cannot find %s plugin for %s", plugin_type, c->type);
		goto fail;
	}

	/* Dereference the API. */
	if (plugin_get_syms(c->cur_plugin, n_names, names, ptrs) < n_names) {
		error("incomplete %s plugin detected", plugin_type);
		goto fail;
	}

	return c;
fail:
	plugin_context_destroy(c);
	return NULL;
}

/*
 * Destroy a context
 */
extern int plugin_context_destroy(plugin_context_t *c)
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if (c->plugin_list) {
		if (plugrack_destroy(c->plugin_list) != SLURM_SUCCESS)
			rc = SLURM_ERROR;
	} else
		plugin_unload(c->cur_plugin);

	xfree(c->type);
	xfree(c);

	return rc;
}

/*
 * Return a list of plugin names that match the given type.
 *
 * IN plugin_type - Type of plugin to search for in the plugin_dir.
 * RET list of plugin names, NULL if none found.
 */
extern List plugin_get_plugins_of_type(char *plugin_type)
{
	List plugin_names = NULL;
	char *plugin_dir = NULL, *dir = NULL, *save_ptr = NULL;
	char *type_under = NULL, *type_slash = NULL;
	DIR *dirp;
	struct dirent *e;
	int len;

	if (!(plugin_dir = xstrdup(slurm_conf.plugindir))) {
		error("%s: No plugin dir given", __func__);
		goto done;
	}

	type_under = xstrdup_printf("%s_", plugin_type);
	type_slash = xstrdup_printf("%s/", plugin_type);

	dir = strtok_r(plugin_dir, ":", &save_ptr);
	while (dir) {
		/* Open the directory. */
		if (!(dirp = opendir(dir))) {
			error("cannot open plugin directory %s", dir);
			goto done;
		}

		while (1) {
			char full_name[128];

			if (!(e = readdir( dirp )))
				break;
			/* Check only files with "plugintype_" in them. */
			if (xstrncmp(e->d_name, type_under, strlen(type_under)))
				continue;

			len = strlen(e->d_name);
			len -= 3;
			/* Check only shared object files */
			if (xstrcmp(e->d_name+len, ".so"))
				continue;
			/* add one for the / */
			len++;
			xassert(len < sizeof(full_name));
			snprintf(full_name, len, "%s%s",
				 type_slash, e->d_name + strlen(type_slash));

			if (!plugin_names)
				plugin_names = list_create(xfree_ptr);
			if (!list_find_first(plugin_names,
					     slurm_find_char_in_list,
					     full_name))
				list_append(plugin_names, xstrdup(full_name));
		}
		closedir(dirp);

		dir = strtok_r(NULL, ":", &save_ptr);
	}

done:
	xfree(plugin_dir);
	xfree(type_under);
	xfree(type_slash);

	return plugin_names;
}
