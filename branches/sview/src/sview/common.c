/*****************************************************************************\
 *  common.c - common functions used by tabs in sview
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  UCRL-CODE-217948.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/sview/sview.h"

extern int sort_iter_compare_func(GtkTreeModel *model,
				  GtkTreeIter  *a,
				  GtkTreeIter  *b,
				  gpointer      userdata)
{
	int sortcol = GPOINTER_TO_INT(userdata);
	int ret = 0;
	gchar *name1 = NULL, *name2 = NULL;

	gtk_tree_model_get(model, a, sortcol, &name1, -1);
	gtk_tree_model_get(model, b, sortcol, &name2, -1);
	
	if (name1 == NULL || name2 == NULL)
	{
		if (name1 == NULL && name2 == NULL)
			goto cleanup; /* both equal => ret = 0 */
		
		ret = (name1 == NULL) ? -1 : 1;
	}
	else
	{
		ret = g_utf8_collate(name1,name2);
	}
cleanup:
	g_free(name1);
	g_free(name2);
	
	return ret;
}

extern void snprint_time(char *buf, size_t buf_size, time_t time)
{
	if (time == INFINITE) {
		snprintf(buf, buf_size, "UNLIMITED");
	} else {
		long days, hours, minutes, seconds;
		seconds = time % 60;
		minutes = (time / 60) % 60;
		hours = (time / 3600) % 24;
		days = time / 86400;

		if (days)
			snprintf(buf, buf_size,
				"%ld-%2.2ld:%2.2ld:%2.2ld",
				days, hours, minutes, seconds);
		else if (hours)
			snprintf(buf, buf_size,
				"%ld:%2.2ld:%2.2ld", 
				hours, minutes, seconds);
		else
			snprintf(buf, buf_size,
				"%ld:%2.2ld", minutes,seconds);
	}
}

extern void add_col_to_treeview(GtkTreeView *tree_view, int id, char *name)
{
	GtkTreeViewColumn   *col;
	GtkCellRenderer     *renderer;
  
	renderer = gtk_cell_renderer_text_new();
	col = gtk_tree_view_column_new();	
	gtk_tree_view_column_pack_start (col, renderer, TRUE);
	gtk_tree_view_column_add_attribute (col, renderer, 
					    "text", id);
	gtk_tree_view_column_set_title (col, name);
	gtk_tree_view_append_column(tree_view, col);
	gtk_tree_view_column_set_sort_column_id(col, id);

}

extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path)
{
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	int line = 0;
	
	if(!model) {
		g_error("error getting the model from the tree_view");
		return -1;
	}
	
	if (!gtk_tree_model_get_iter(model, &iter, path)) {
		g_error("error getting iter from model");
		return -1;
	}	
	gtk_tree_model_get(model, &iter, POS_LOC, &line, -1);
	return line;
}
