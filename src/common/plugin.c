/*****************************************************************************\
 *  plugin.h - plugin architecture implementation.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>.
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
#  include "config.h"
#endif

#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
#include <dlfcn.h>	/* don't know if there's an autoconf for this. */
#include <string.h>

#include "src/common/xmalloc.h"
#include "src/common/log.h"
#include "src/common/plugin.h"
#include "src/common/xstring.h"
#include "src/common/slurm_protocol_api.h"
#include <slurm/slurm_errno.h>

#  if HAVE_UNISTD_H
#    include <unistd.h>
#  endif /* HAVE_UNISTD_H */
#  if HAVE_SYS_TYPES_H
#    include <sys/types.h>
#  endif
#  if HAVE_SYS_STAT_H
#    include <sys/stat.h>
#  endif

#  if HAVE_STDLIB_H
#    include <stdlib.h>
#  endif

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
		case EPLUGIN_MISSING_SYMBOL:
			return ("Plugin name/type/version symbol missing");
	}
	return ("Unknown error");
}

int
plugin_peek( const char *fq_path,
			 char *plugin_type,
			 const size_t type_len,
			 uint32_t *plugin_version )
{
	plugin_handle_t plug;
	char *type;
	uint32_t *version;
	
	plug = dlopen( fq_path, RTLD_LAZY );
	if ( plug == NULL ) {
		debug3( "plugin_peek: dlopen(%s): %s", fq_path, _dlerror() );
		return SLURM_ERROR;
	}
	if ( ( type = dlsym( plug, PLUGIN_TYPE ) ) != NULL ) {
		if ( plugin_type != NULL ) {
			strncpy( plugin_type, type, type_len );
		}
	} else {	
		dlclose( plug );
		/* could be vestigial library, don't treat as an error */
		verbose( "%s: not a SLURM plugin", fq_path );
		return SLURM_ERROR;
	}
	
	if ( ( version = (uint32_t *) dlsym( plug, PLUGIN_VERSION ) ) != NULL ) {
		if ( plugin_version != NULL ) {
			*plugin_version = *version;
		}
	} else {
		dlclose( plug );
		/* could be vestigial library, don't treat as an error */
		verbose( "%s: not a SLURM plugin", fq_path );
		return SLURM_ERROR;
	}

	dlclose( plug );
	return SLURM_SUCCESS;
}

plugin_err_t
plugin_load_from_file(plugin_handle_t *p, const char *fq_path)
{
	plugin_handle_t plug;
	int (*init)(void);

	*p = PLUGIN_INVALID_HANDLE;

	/*
	 *  Check for file existence and access permissions
	 */
	if (access(fq_path, R_OK) < 0) {
		if (errno == ENOENT)
			return EPLUGIN_NOTFOUND;
		else
			return EPLUGIN_ACCESS_ERROR;
	}

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

	/* Now see if our required symbols are defined. */
	if ((dlsym(plug, PLUGIN_NAME) == NULL) ||
	    (dlsym(plug, PLUGIN_TYPE) == NULL) ||
	    (dlsym(plug, PLUGIN_VERSION) == NULL)) {
		dlclose (plug);
		return EPLUGIN_MISSING_SYMBOL;
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

plugin_handle_t
plugin_load_and_link(const char *type_name, int n_syms,
		    const char *names[], void *ptrs[])
{
	plugin_handle_t plug = PLUGIN_INVALID_HANDLE;
	struct stat st;
	char *head=NULL, *dir_array=NULL, *so_name = NULL,
		*file_name=NULL;
	int i=0;
	
	if (!type_name)
		return plug;

	so_name = xstrdup_printf("%s.so", type_name);

	while(so_name[i]) {
		if(so_name[i] == '/')
			so_name[i] = '_';
		i++;
	}
	if(!(dir_array = slurm_get_plugin_dir())) {
		error("plugin_load_and_link: No plugin dir given");
		xfree(so_name);
		return plug;
	}
	
	head = dir_array;
	for (i=0; ; i++) {
		bool got_colon = 0;
		if (dir_array[i] == ':') {
			dir_array[i] = '\0';
			got_colon = 1;
		} else if(dir_array[i] != '\0') 
			continue;
		
		file_name = xstrdup_printf("%s/%s", head, so_name);
		debug3("Trying to load plugin %s", file_name);
		if ((stat(file_name, &st) < 0) || (!S_ISREG(st.st_mode))) {
			debug4("No Good.");
			xfree(file_name);
		} else {
			plugin_load_from_file(&plug, file_name);
			xfree(file_name);
			if (plugin_get_syms(plug, n_syms, names, ptrs) >= 
			    n_syms) {
				debug3("Success.");
				break;
			} else 
				plug = PLUGIN_INVALID_HANDLE;
		}

		if (got_colon) {
			head = dir_array + i + 1;
		} else 
			break;
	}
	
	xfree(dir_array);
	xfree(so_name);
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
		(void) dlclose( plug );
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

	if ( plug == PLUGIN_INVALID_HANDLE ) return 0;	
	ptr = (uint32_t *) dlsym( plug, PLUGIN_VERSION );
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

