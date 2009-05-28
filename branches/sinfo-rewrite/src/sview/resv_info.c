/*****************************************************************************\
 *  resv_info.c - Functions related to advanced reservation display 
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette@llnl.gov>
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "src/common/uid.h"
#include "src/sview/sview.h"
#include "src/common/parse_time.h"
 
#define _DEBUG 0

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	reserve_info_t *resv_ptr;
	char *color;
} sview_resv_info_t;

enum { 
	EDIT_REMOVE = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum { 
	SORTID_POS = POS_LOC,
	SORTID_ACCOUNTS,
	SORTID_ACTION,
	SORTID_DURATION,
	SORTID_END_TIME,
	SORTID_FEATURES,
	SORTID_FLAGS,
	SORTID_NAME,
	SORTID_NODE_CNT,
	SORTID_NODE_LIST,
	SORTID_NODE_INX,
	SORTID_PARTITION,
	SORTID_START_TIME,
	SORTID_UPDATED,
	SORTID_USERS,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.  
 */

static display_data_t display_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NAME,       "Name", TRUE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACTION,     "Action", FALSE, EDIT_MODEL,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODE_CNT,   "Nodes", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_NODE_LIST,  "NodeList", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_START_TIME, "StartTime", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_END_TIME,   "EndTime", TRUE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_DURATION, "Duration", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_ACCOUNTS,   "Accounts", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_USERS,      "Users", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_PARTITION,  "Partition", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FEATURES,   "Features", FALSE, EDIT_TEXTBOX, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_STRING, SORTID_FLAGS,      "Flags", FALSE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_POINTER, SORTID_NODE_INX,  NULL, FALSE, EDIT_NONE, 
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_INT,    SORTID_UPDATED,    NULL, FALSE, EDIT_NONE,
	 refresh_resv, create_model_resv, admin_edit_resv},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_resv[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, RESV_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Remove", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Edit Reservation", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, RESV_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partitions", TRUE, RESV_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, RESV_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, RESV_PAGE},
#else
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, RESV_PAGE},
#endif
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};


static display_data_t *local_display_data = NULL;

static char *got_edit_signal = NULL;

/*
 *  _parse_flags  is used to parse the Flags= option.  It handles
 *  daily, weekly, and maint, optionally preceded by + or -, 
 *  separated by a comma but no spaces.
 */
static uint32_t _parse_flags(const char *flagstr)
{
	int flip;
	uint32_t outflags = 0;
	const char *curr = flagstr;
	int taglen = 0;

	while (*curr != '\0') {
		flip = 0;
		if (*curr == '+') {
			curr++;
		} else if (*curr == '-') {
			flip = 1;
			curr++;
		}
		taglen = 0;
		while (curr[taglen] != ',' && curr[taglen] != '\0')
			taglen++;

		if (strncasecmp(curr, "Maintenance", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_MAINT;
			else 
				outflags |= RESERVE_FLAG_MAINT;
		} else if (strncasecmp(curr, "Daily", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_DAILY;
			else 
				outflags |= RESERVE_FLAG_DAILY;
		} else if (strncasecmp(curr, "Weekly", MAX(taglen,1)) == 0) {
			curr += taglen;
			if (flip)
				outflags |= RESERVE_FLAG_NO_WEEKLY;
			else 
				outflags |= RESERVE_FLAG_WEEKLY;
		} else {
			char *temp = g_strdup_printf("Error parsing flags %s.",
						     flagstr);
			display_edit_note(temp);
			g_free(temp);
			outflags = (uint32_t)NO_VAL;
			break;
		}

		if (*curr == ',') 
			curr++;
	}
	return outflags;
}

static void _set_active_combo_resv(GtkComboBox *combo, 
				   GtkTreeModel *model, GtkTreeIter *iter,
				   int type)
{
	char *temp_char = NULL;
	int action = 0;

	gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if(!temp_char)
		goto end_it;
	switch(type) {
	case SORTID_ACTION:
		if(!strcmp(temp_char, "none"))
			action = 0;
		else if(!strcmp(temp_char, "remove"))
			action = 1;
		else 
			action = 0;
				
		break;
	default:
		break;
	}
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);
	
}

/* don't free this char */
static const char *_set_resv_msg(resv_desc_msg_t *resv_msg,
				 const char *new_text,
				 int column)
{
	char *type = NULL;
	int temp_int = 0;
	uint32_t f;
	
	/* need to clear errno here (just in case) */
	errno = 0;

	if(!resv_msg)
		return NULL;
	
	switch(column) {
	case SORTID_ACCOUNTS:		
		resv_msg->accounts = xstrdup(new_text);
		type = "accounts";
		break;
	case SORTID_ACTION:
		xfree(got_edit_signal);
		if(!strcasecmp(new_text, "None"))
			got_edit_signal = NULL;
		else
			got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_DURATION:
		temp_int = time_str2mins((char *)new_text);
		if(temp_int <= 0)
			goto return_error;
		resv_msg->duration = temp_int;
		type = "duration";
		break;
	case SORTID_END_TIME:
		resv_msg->end_time = parse_time((char *)new_text, 0);
		type = "end time";
		break;
	case SORTID_FEATURES:
		resv_msg->features = xstrdup(new_text);
		type = "features";
		break;
	case SORTID_FLAGS:
		f = _parse_flags(new_text);
		type = "flags";
		if(f == (uint32_t)NO_VAL) 
			goto return_error;
		resv_msg->flags = f;
		break;
	case SORTID_NAME:
		resv_msg->name = xstrdup(new_text);
		type = "name";
		break;
	case SORTID_NODE_CNT:
		temp_int = strtol(new_text, (char **)NULL, 10);
		
		type = "Node Count";
		if(temp_int <= 0)
			goto return_error;
		resv_msg->node_cnt = temp_int;
		break;
	case SORTID_NODE_LIST:
		resv_msg->node_list = xstrdup(new_text);
		type = "node list";
		break;
	case SORTID_PARTITION:
		resv_msg->partition = xstrdup(new_text);
		type = "partition";
		break;
	case SORTID_START_TIME:
		resv_msg->start_time = parse_time((char *)new_text, 0);
		type = "start time";
		break;
	case SORTID_USERS:		
		resv_msg->users = xstrdup(new_text);
		type = "users";
		break;
	default:
		type = "unknown";
		break;
	}

	return type;

return_error:
	errno = 1;
	return type;
}

static void _resv_info_list_del(void *object)
{
	sview_resv_info_t *sview_resv_info = (sview_resv_info_t *)object;

	if (sview_resv_info) {
		xfree(sview_resv_info);
	}
}

static void _admin_edit_combo_box_resv(GtkComboBox *combo,
				       resv_desc_msg_t *resv_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;
	
	if(!resv_msg)
		return;

	if(!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if(!model) {
		g_print("nothing selected\n");
		return;
	}

	gtk_tree_model_get(model, &iter, 0, &name, -1);
	gtk_tree_model_get(model, &iter, 1, &column, -1);

	_set_resv_msg(resv_msg, name, column);

	g_free(name);
}



static gboolean _admin_focus_out_resv(GtkEntry *entry,
				      GdkEventFocus *event, 
				      resv_desc_msg_t *resv_msg)
{
	int type = gtk_entry_get_max_length(entry);
	const char *name = gtk_entry_get_text(entry);
	type -= DEFAULT_ENTRY_LENGTH;
	_set_resv_msg(resv_msg, name, type);
	
	return false;
}

static GtkWidget *_admin_full_edit_resv(resv_desc_msg_t *resv_msg, 
					GtkTreeModel *model, GtkTreeIter *iter)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkTreeModel *model2 = NULL; 
	GtkCellRenderer *renderer = NULL;
	int i = 0, row = 0;
	char *temp_char = NULL;

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);
	
	gtk_table_set_homogeneous(table, FALSE);	

	for(i = 0; i < SORTID_CNT; i++) {
		if(display_data_resv[i].extra == EDIT_MODEL) {
			/* edittable items that can only be known
			   values */
			model2 = GTK_TREE_MODEL(
				create_model_resv(display_data_resv[i].id));
			if(!model2) {
				g_print("no model set up for %d(%s)\n",
					display_data_resv[i].id,
					display_data_resv[i].name);
				continue;
			}
			entry = gtk_combo_box_new_with_model(model2);
			g_object_unref(model2);
			
			_set_active_combo_resv(GTK_COMBO_BOX(entry), model,
					      iter, display_data_resv[i].id);
			
			g_signal_connect(entry, "changed",
					 G_CALLBACK(
						 _admin_edit_combo_box_resv),
					 resv_msg);
			
			renderer = gtk_cell_renderer_text_new();
			gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(entry),
						   renderer, TRUE);
			gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(entry),
						      renderer, "text", 0);
		} else if(display_data_resv[i].extra == EDIT_TEXTBOX) {
			/* other edittable items that are unknown */
			entry = create_entry();
			gtk_tree_model_get(model, iter,
					   display_data_resv[i].id,
					   &temp_char, -1);
			gtk_entry_set_max_length(GTK_ENTRY(entry), 
						 (DEFAULT_ENTRY_LENGTH +
						  display_data_resv[i].id));
			
			if(temp_char) {
				gtk_entry_set_text(GTK_ENTRY(entry),
						   temp_char);
				g_free(temp_char);
			}
			g_signal_connect(entry, "focus-out-event",
					 G_CALLBACK(_admin_focus_out_resv),
					 resv_msg);
		} else /* others can't be altered by the user */
			continue;
		label = gtk_label_new(display_data_resv[i].name);
		gtk_table_attach(table, label, 0, 1, row, row+1,
				 GTK_FILL | GTK_EXPAND, GTK_SHRINK, 
				 0, 0);
		gtk_table_attach(table, entry, 1, 2, row, row+1,
				 GTK_FILL, GTK_SHRINK,
				 0, 0);
		row++;
	}
	gtk_table_resize(table, row, 2);
	
	return GTK_WIDGET(window);
}

static void _layout_resv_record(GtkTreeView *treeview, 
				sview_resv_info_t *sview_resv_info,
				int update)
{
	GtkTreeIter iter;
	char time_buf[20];
	reserve_info_t *resv_ptr = sview_resv_info->resv_ptr;
	char *temp_char = NULL;

	GtkTreeStore *treestore = 
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_NAME),
				   resv_ptr->name);

	convert_num_unit((float)resv_ptr->node_cnt, 
			 time_buf, sizeof(time_buf), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_NODE_CNT), 
				   time_buf);

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_NODE_LIST),
				   resv_ptr->node_list);

	slurm_make_time_str((time_t *)&resv_ptr->start_time, time_buf,
			    sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_START_TIME), 
				   time_buf);
	slurm_make_time_str((time_t *)&resv_ptr->end_time, time_buf,
			    sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_END_TIME), 
				   time_buf);

	secs2time_str((uint32_t)difftime(resv_ptr->end_time,
					 resv_ptr->start_time),
		      time_buf, sizeof(time_buf));
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_DURATION), 
				   time_buf);

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_ACCOUNTS),
				   resv_ptr->accounts);

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_USERS),
				   resv_ptr->users);

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_PARTITION),
				   resv_ptr->partition);

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_FEATURES),
				   resv_ptr->features);

	temp_char = reservation_flags_string(resv_ptr->flags);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_resv,
						 SORTID_FLAGS),
				   temp_char);
	xfree(temp_char);
}

static void _update_resv_record(sview_resv_info_t *sview_resv_info_ptr, 
				GtkTreeStore *treestore,
				GtkTreeIter *iter)
{
	char *tmp_ptr = NULL;
	char tmp_char[50];
	reserve_info_t *resv_ptr = sview_resv_info_ptr->resv_ptr;
      
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_ACCOUNTS, resv_ptr->accounts, -1);

	secs2time_str((uint32_t)difftime(resv_ptr->end_time, 
					  resv_ptr->start_time), 
		      tmp_char, sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_DURATION, tmp_char, -1);

	slurm_make_time_str((time_t *)&resv_ptr->end_time, tmp_char,
			    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_END_TIME, tmp_char, -1);

	gtk_tree_store_set(treestore, iter, SORTID_FEATURES,
			   resv_ptr->features, -1);

	tmp_ptr = reservation_flags_string(resv_ptr->flags);
	gtk_tree_store_set(treestore, iter, SORTID_FLAGS,
			   tmp_ptr, -1);
	xfree(tmp_ptr);

	gtk_tree_store_set(treestore, iter, SORTID_NAME, resv_ptr->name, -1);

	convert_num_unit((float)resv_ptr->node_cnt,
			 tmp_char, sizeof(tmp_char), UNIT_NONE);
	gtk_tree_store_set(treestore, iter, 
			   SORTID_NODE_CNT, tmp_char, -1);

	gtk_tree_store_set(treestore, iter, 
			   SORTID_NODE_LIST, resv_ptr->node_list, -1);

	gtk_tree_store_set(treestore, iter, 
			   SORTID_NODE_INX, resv_ptr->node_inx, -1);

	gtk_tree_store_set(treestore, iter, 
			   SORTID_PARTITION, resv_ptr->partition, -1);

	slurm_make_time_str((time_t *)&resv_ptr->start_time, tmp_char,
			    sizeof(tmp_char));
	gtk_tree_store_set(treestore, iter, SORTID_START_TIME, tmp_char, -1);

	gtk_tree_store_set(treestore, iter,
			   SORTID_USERS, resv_ptr->users, -1);
		
	return;
}

static void _append_resv_record(sview_resv_info_t *sview_resv_info_ptr, 
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_resv_record(sview_resv_info_ptr, treestore, iter);	
}

static void _update_info_resv(List info_list, 
			      GtkTreeView *tree_view)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	reserve_info_t *resv_ptr = NULL;
	int line = 0;
	char *host = NULL, *resv_name = NULL;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info = NULL;

	/* get the iter, or find out the list is empty goto add */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		/* make sure all the reserves are still here */
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	itr = list_iterator_create(info_list);
	while ((sview_resv_info = (sview_resv_info_t*) list_next(itr))) {
		resv_ptr = sview_resv_info->resv_ptr;
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		} 
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_NAME, 
					   &resv_name, -1);
			if(!strcmp(resv_name, resv_ptr->name)) {
				/* update with new info */
				g_free(resv_name);
				_update_resv_record(sview_resv_info, 
						    GTK_TREE_STORE(model), 
						    &iter);
				goto found;
			}
			g_free(resv_name);
				
			/* see what line we were on to add the next one 
			   to the list */
			gtk_tree_model_get(model, &iter, SORTID_POS, 
					   &line, -1);
			if(!gtk_tree_model_iter_next(model, &iter)) {
				line++;
				break;
			}
		}
	adding:
		_append_resv_record(sview_resv_info, GTK_TREE_STORE(model), 
				    &iter, line);
	found:
		;
	}
	list_iterator_destroy(itr);
	if(host)
		free(host);

	gtk_tree_path_free(path);
	/* remove all old reservations */
	remove_old(model, SORTID_UPDATED);
	return;
}

static int _sview_resv_sort_aval_dec(sview_resv_info_t* rec_a,
				     sview_resv_info_t* rec_b)
{
	int size_a = rec_a->resv_ptr->node_cnt;
	int size_b = rec_b->resv_ptr->node_cnt;

	if (size_a > size_b)
		return -1;
	else if (size_a < size_b)
		return 1;

	if(rec_a->resv_ptr->node_list && rec_b->resv_ptr->node_list) {
		size_a = strcmp(rec_a->resv_ptr->node_list, 
				rec_b->resv_ptr->node_list);
		if (size_a > 0)
			return -1;
		else if (size_a < 0)
			return 1;
	}
	return 0;
}

static List _create_resv_info_list(reserve_info_msg_t *resv_info_ptr,
				   int changed)
{
	static List info_list = NULL;
	int i = 0;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	reserve_info_t *resv_ptr = NULL;

	if(!changed && info_list) 
		goto update_color;
	
	if(info_list) 
		list_flush(info_list);
	else
		info_list = list_create(_resv_info_list_del);

	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}
	
	for(i=0; i<resv_info_ptr->record_count; i++) {
		resv_ptr = &(resv_info_ptr->reservation_array[i]);
		sview_resv_info_ptr = xmalloc(sizeof(sview_resv_info_t));
		sview_resv_info_ptr->resv_ptr = resv_ptr;
		list_append(info_list, sview_resv_info_ptr);
	}

	list_sort(info_list,
		  (ListCmpF)_sview_resv_sort_aval_dec);

update_color:
	return info_list;
}

void _display_info_resv(List info_list,	popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0;
	reserve_info_t *resv_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info = NULL;
	int update = 0;
	int i = -1, j = 0;

	if(!spec_info->search_info->gchar_data) {
		//info = xstrdup("No pointer given!");
		goto finished;
	}

need_refresh:
	if(!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget = 
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	itr = list_iterator_create(info_list);
	while ((sview_resv_info = (sview_resv_info_t*) list_next(itr))) {
		resv_ptr = sview_resv_info->resv_ptr;
		i++;
		if(!strcmp(resv_ptr->name, name)) {
			j=0;
			while(resv_ptr->node_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					resv_ptr->node_inx[j],
					resv_ptr->node_inx[j+1], i, true, 0);
				j += 2;
			}
			_layout_resv_record(treeview, sview_resv_info, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	
	if(!found) {
		if(!popup_win->not_found) { 
			char *temp = "RESERVATION DOESN'T EXSIST\n";
			GtkTreeIter iter;
			GtkTreeModel *model = NULL;
	
			/* only time this will be run so no update */
			model = gtk_tree_view_get_model(treeview);
			add_display_treestore_line(0, 
						   GTK_TREE_STORE(model), 
						   &iter,
						   temp, "");
		}
		popup_win->not_found = true;
	} else {
		if(popup_win->not_found) { 
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);
			
			goto need_refresh;
		}
	}
	gtk_widget_show(spec_info->display_widget);
		
finished:
		
	return;
}

extern void refresh_resv(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_resv(popup_win);
}

extern int get_new_info_resv(reserve_info_msg_t **info_ptr, 
			     int force)
{
	static reserve_info_msg_t *resv_info_ptr = NULL, *new_resv_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
		
	if(!force && ((now - last) < global_sleep_time)) {
		if(*info_ptr != resv_info_ptr) 
			error_code = SLURM_SUCCESS;
		*info_ptr = resv_info_ptr;
		if(changed) 
			return SLURM_SUCCESS;
		return error_code;
	}
	last = now;
	if (resv_info_ptr) {
		error_code = slurm_load_reservations(resv_info_ptr->last_update,
						     &new_resv_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_reservation_info_msg(resv_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_resv_ptr = resv_info_ptr;
			changed = 0;
		}
	} else {
		error_code = slurm_load_reservations((time_t) NULL, 
						     &new_resv_ptr);
		changed = 1;
	}

	resv_info_ptr = new_resv_ptr;

	if(*info_ptr != resv_info_ptr) 
		error_code = SLURM_SUCCESS;

	*info_ptr = new_resv_ptr;
	return error_code;
}

extern GtkListStore *create_model_resv(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	switch(type) {
	case SORTID_ACTION:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "None",
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   1, SORTID_ACTION,
				   0, "Remove",
				   -1);	
		break;
	default:
		break;
	}
	return model;
}

extern void admin_edit_resv(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	resv_desc_msg_t *resv_msg = xmalloc(sizeof(resv_desc_msg_t));
	
	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;
	
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), 
						       "column"));

	if(!new_text || !strcmp(new_text, ""))
		goto no_input;
	
	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	slurm_init_resv_desc_msg(resv_msg);	
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
			   SORTID_NAME, &temp, 
			   column, &old_text,
			   -1);
	resv_msg->name = xstrdup(temp);
	g_free(temp);

	type = _set_resv_msg(resv_msg, new_text, column);
	if(errno) 
		goto print_error;
	
	if(got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		admin_resv(GTK_TREE_MODEL(treestore), &iter, temp);
		xfree(temp);
		goto no_input;
	}
			
	if(old_text && !strcmp(old_text, new_text)) {
		temp = g_strdup_printf("No change in value.");
	} else if(slurm_update_reservation(resv_msg) == SLURM_SUCCESS) {
		gtk_tree_store_set(treestore, &iter, column, new_text, -1);
		temp = g_strdup_printf("Reservation %s %s changed to %s",
				       resv_msg->name,
				       type,
				       new_text);
	} else if(errno == ESLURM_DISABLED) {
		temp = g_strdup_printf(
			"Can only edit %s on reservations not yet started.",
			type);
	} else {
	print_error:
		temp = g_strdup_printf("Reservation %s %s can't be "
				       "set to %s",
				       resv_msg->name,
				       type,
				       new_text);
	}
	
	display_edit_note(temp);
	g_free(temp);

no_input:
	slurm_free_resv_desc_msg(resv_msg);
			  
	gtk_tree_path_free (path);
	g_free(old_text);
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_resv(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	List info_list = NULL;
	static int view = -1;
	static reserve_info_msg_t *resv_info_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	int i = 0, j = 0;
	int changed = 1;
	ListIterator itr = NULL;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	reserve_info_t *resv_ptr = NULL;
	time_t now = time(NULL);
		
	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_resv->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	error_code = get_new_info_resv(&resv_info_ptr, force_refresh);
	if(error_code == SLURM_NO_CHANGE_IN_DATA) {
		changed = 0;
	} else if (error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_reservations: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		goto end_it;
	}

display_it:
	info_list = _create_resv_info_list(resv_info_ptr, changed);
	if(!info_list)
		return;
	/* set up the grid */
	itr = list_iterator_create(info_list);
	while ((sview_resv_info_ptr = list_next(itr))) {
		resv_ptr = sview_resv_info_ptr->resv_ptr;
		if ((resv_ptr->start_time > now) ||
		    (resv_ptr->end_time   < now))
			continue;	/* only map current reservations */
		j=0;
		while(resv_ptr->node_inx[j] >= 0) {
			sview_resv_info_ptr->color = 
				change_grid_color(grid_button_list,
						  resv_ptr->node_inx[j],
						  resv_ptr->node_inx[j+1],
						  i, true, 0);
			j += 2;
		}
		i++;
	}
	list_iterator_destroy(itr);
		
	if(view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if(!display_widget) {
		tree_view = create_treeview(local_display_data);

		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view 
		   to the treestore we don't really care about 
		   the return value */
		create_treestore(tree_view, display_data_resv, SORTID_CNT);
	}
	view = INFO_VIEW;
	_update_info_resv(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
	
	return;
}

extern void specific_info_resv(popup_info_t *popup_win)
{
	int resv_error_code = SLURM_SUCCESS;
	static reserve_info_msg_t *resv_info_ptr = NULL;
	static reserve_info_t *resv_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List resv_list = NULL;
	List send_resv_list = NULL;
	int changed = 1;
	sview_resv_info_t *sview_resv_info_ptr = NULL;
	int j=0, i=-1;
	hostset_t hostset = NULL;
	ListIterator itr = NULL;
	
	if(!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_resv, SORTID_CNT);
	}

	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}
	
	if((resv_error_code = 
	    get_new_info_resv(&resv_info_ptr, popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if(!spec_info->display_widget || spec_info->view == ERROR_VIEW)
			goto display_it;
		changed = 0;		
	} else if (resv_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "get_new_info_resv: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		spec_info->display_widget = gtk_widget_ref(label);
		goto end_it;
	}
	
display_it:
	
	resv_list = _create_resv_info_list(resv_info_ptr, changed);

	if(!resv_list)
		return;

	if(spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	if(spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data);
		spec_info->display_widget = 
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table, 
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1); 
		/* since this function sets the model of the tree_view 
		   to the treestore we don't really care about 
		   the return value */
		create_treestore(tree_view, 
				 popup_win->display_data, SORTID_CNT);
	}

	setup_popup_grid_list(popup_win);

	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_resv(resv_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_resv_list = list_create(NULL);	
	itr = list_iterator_create(resv_list);
	i = -1;
	while ((sview_resv_info_ptr = list_next(itr))) {
		i++;
		resv_ptr = sview_resv_info_ptr->resv_ptr;	
		switch(spec_info->type) {
		case PART_PAGE:
		case BLOCK_PAGE:
		case NODE_PAGE:
			if(!resv_ptr->node_list)
				continue;
			
			if(!(hostset = hostset_create(search_info->gchar_data)))
				continue;
			if(!hostset_intersects(hostset, resv_ptr->node_list)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);				
			break;
		case JOB_PAGE:
			if(strcmp(resv_ptr->name, 
				  search_info->gchar_data)) 
				continue;
			break;
		case RESV_PAGE:
			switch(search_info->search_type) {
			case SEARCH_RESERVATION_NAME:
				if(!search_info->gchar_data)
					continue;
				
				if(strcmp(resv_ptr->name, 
					  search_info->gchar_data)) 
					continue;
				break;
			default:
				continue;
			}
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			continue;
		}
		list_push(send_resv_list, sview_resv_info_ptr);
		j=0;
		while(resv_ptr->node_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					resv_ptr->node_inx[j],
					resv_ptr->node_inx[j+1], i, false, 0);
			j += 2;
		}
	}
	list_iterator_destroy(itr);

	_update_info_resv(send_resv_list, 
			  GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_resv_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
	
	return;
}

extern void set_menus_resv(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_resv, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_resv);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_resv(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
				
	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);

	switch(id) {
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) with reservation %s", name);
		break;
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in reservation %s", name);
		break;
	case NODE_PAGE:
#ifdef HAVE_BG
		snprintf(title, 100, 
			 "Base partitions(s) in reservation %s",
			 name);
#else
		snprintf(title, 100, "Node(s) in reservation %s ",
			 name);
#endif
		break;
	case BLOCK_PAGE: 
		snprintf(title, 100, "Block(s) in reservation %s", name);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job in reservation %s", name);
		break;
	case INFO_PAGE: 
		snprintf(title, 100, "Full info for reservation %s", name);
		break;
	default:
		g_print("resv got %d\n", id);
	}
	
	itr = list_iterator_create(popup_list);
	while((popup_win = list_next(itr))) {
		if(popup_win->spec_info)
			if(!strcmp(popup_win->spec_info->title, title)) {
				break;
			} 
	}
	list_iterator_destroy(itr);

	if(!popup_win) {
		if(id == INFO_PAGE)
			popup_win = create_popup_info(id, RESV_PAGE, title);
		else
			popup_win = create_popup_info(RESV_PAGE, id, title);
	} else {
		g_free(name);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	/* Pass the model and the structs from the iter so we can always get
	   the current node_inx.
	*/
	popup_win->model = model;
	popup_win->iter = *iter;
	popup_win->node_inx_id = SORTID_NODE_INX;

	switch(id) {
	case JOB_PAGE:
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		//specific_info_job(popup_win);
		break;
	case BLOCK_PAGE: 
	case NODE_PAGE:
	case PART_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODE_LIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		popup_win->spec_info->search_info->search_type = 
			SEARCH_NODE_NAME;
		//specific_info_node(popup_win);
		break;
	case SUBMIT_PAGE: 
		break;
	default:
		g_print("resv got unknown type %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create resv popup thread: %s\n", 
			    error->message);
		return;
	}		
}

extern void admin_resv(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	resv_desc_msg_t *resv_msg = xmalloc(sizeof(resv_desc_msg_t));
	reservation_name_msg_t resv_name_msg;
	char *resvid = NULL;
	char tmp_char[100];
	char *temp = NULL;
	int edit_type = 0;
	int response = 0;	
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_tree_model_get(model, iter, SORTID_NAME, &resvid, -1);

	slurm_init_resv_desc_msg(resv_msg);
	memset(&resv_name_msg, 0, sizeof(reservation_name_msg_t));
	
	resv_msg->name = xstrdup(resvid);
		
	if(!strcasecmp("Remove", type)) {
		resv_name_msg.name = resvid;

		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		
			snprintf(tmp_char, sizeof(tmp_char), 
				 "Are you sure you want to remove "
				 "reservation %s?",
				 resvid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_REMOVE;
	} else {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char), 
			 "Editing reservation %s think before you type",
			 resvid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_EDIT;
		entry = _admin_full_edit_resv(resv_msg, model, iter);
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, FALSE, FALSE, 0);
	if(entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
				   entry, TRUE, TRUE, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		switch(edit_type) {
		case EDIT_REMOVE:
			if(slurm_delete_reservation(&resv_name_msg)
			   == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Reservation %s removed successfully",
					resvid);
			} else {
				temp = g_strdup_printf(
					"Problem removing reservation %s.",
					resvid);
			}			
			display_edit_note(temp);
			g_free(temp);
			break;
		case EDIT_EDIT:
			if(got_edit_signal) 
				goto end_it;
			if(slurm_update_reservation(resv_msg)
			   == SLURM_SUCCESS) {
				temp = g_strdup_printf(
					"Reservation %s updated successfully",
					resvid);
			} else {
				temp = g_strdup_printf(
					"Problem updating reservation %s.",
					resvid);
			}
			display_edit_note(temp);
			g_free(temp);
			break;
		default:
			break;
		}
	}
end_it:
		
	g_free(resvid);
	slurm_free_resv_desc_msg(resv_msg);
	gtk_widget_destroy(popup);
	if(got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		admin_resv(model, iter, type);
		xfree(type);
	}			
	return;
}

