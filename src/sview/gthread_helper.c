/****************************************************************************\
 *  sview.h - definitions used for sview data functions
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\****************************************************************************/

#include "config.h"

#include <stdbool.h>

#include "gthread_helper.h"

void sview_thread_init(gpointer vtable)
{
#ifndef GLIB_NEW_THREADS
	g_thread_init(vtable);
#endif
}

bool sview_thread_new(GThreadFunc func, gpointer data, GError **error)
{
#ifndef GLIB_NEW_THREADS
	return g_thread_create(func, data, false, error);
#else
	GThread *new;
	if ((new = g_thread_try_new(NULL, func, data, error))) {
		g_thread_unref(new);
		return true;
	}
	return false;
#endif
}

void sview_mutex_new(GMutex **mutex)
{
#ifndef GLIB_NEW_THREADS
	*mutex = g_mutex_new();
#else
	*mutex = g_new(GMutex, 1);
	g_mutex_init(*mutex);
#endif
}

void sview_cond_new(GCond **cond)
{
#ifndef GLIB_NEW_THREADS
	*cond = g_cond_new();
#else
	*cond = g_new(GCond, 1);
	g_cond_init(*cond);
#endif
}
