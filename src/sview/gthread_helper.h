/****************************************************************************\
 *  sview.h - definitions used for sview data functions
 *****************************************************************************
 *  Copyright (C) 2012 SchedMD LLC
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\****************************************************************************/

#ifndef _GTHREAD_HELPER_H
#define _GTHREAD_HELPER_H

#include "config.h"

#include <gtk/gtk.h>

void sview_thread_init(gpointer vtable);
GThread *sview_thread_new(GThreadFunc func, gpointer data,
			  gboolean joinable, GError **error);
void sview_mutex_new(GMutex **mutex);
void sview_cond_new(GCond **cond);

#endif
