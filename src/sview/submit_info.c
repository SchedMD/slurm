/*****************************************************************************\
 *  submit_info.c - Functions related to submit display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
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
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/sview/sview.h"

#define _DEBUG 0

enum {
	SORTID_POS = POS_LOC,
	SORTID_PARTITION,
	SORTID_AVAIL,
	SORTID_TIMELIMIT,
	SORTID_NODES,
	SORTID_NODELIST,
	SORTID_CNT
};

static display_data_t display_data_submit[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, -1},
	{G_TYPE_STRING, SORTID_PARTITION, "PARTITION", TRUE, -1},
	{G_TYPE_STRING, SORTID_AVAIL, "AVAIL", TRUE, -1},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "TIMELIMIT", TRUE, -1},
	{G_TYPE_STRING, SORTID_NODES, "NODES", TRUE, -1},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "MIDPLANELIST", TRUE, -1},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NODELIST", TRUE, -1},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t options_data_submit[] = {
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, -1},
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, -1},
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", TRUE, -1},
	{G_TYPE_NONE, -1, NULL, FALSE, -1}
};

static display_data_t *local_display_data = NULL;

extern void get_info_submit(GtkTable *table, display_data_t *display_data)
{
	local_display_data = display_data;
}


extern void set_menus_submit(void *arg, void *arg2,
			     GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	/* List button_list = (List)arg2; */

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_submit, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_submit);
		break;
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void row_clicked_submit(GtkTreeView *tree_view,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       gpointer user_data)
{
	/* job_info_msg_t *job_info_ptr = (job_info_msg_t *)user_data; */
/* 	job_info_t *job_ptr = NULL; */
	int line = get_row_number(tree_view, path);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	char *info = NULL;
	if (line == -1) {
		g_error("problem getting line number");
		return;
	}

/* 	part_ptr = &new_part_ptr->partition_array[line]; */
	/* if (!(info = slurm_sprint_partition_info(part_ptr, 0))) { */
/* 		info = xmalloc(100); */
/* 		sprintf(info, "Problem getting partition info for %s",  */
/* 			part_ptr->name); */
/* 	}  */

	popup = gtk_dialog_new();

	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox),
			 label, TRUE, TRUE, 0);
	xfree(info);
	gtk_widget_show(label);

	gtk_widget_show(popup);

}

