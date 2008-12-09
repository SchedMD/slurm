/*****************************************************************************\
 *  part_info.c - Functions related to partition display 
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  LLNL-CODE-402394.
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
#include "src/common/parse_time.h"

#define _DEBUG 0

typedef struct {
	partition_info_t* part_ptr;
	uint16_t node_state;

	uint32_t node_cnt;
	uint16_t min_cpus;
	uint16_t max_cpus;
	uint32_t min_disk;
	uint32_t max_disk;
	uint32_t min_mem;
	uint32_t max_mem;
	uint32_t min_weight;
	uint32_t max_weight;

	char *features;
	char *reason;

	hostlist_t hl;
	List node_ptr_list;
} sview_part_sub_t;

/* Collection of data for printing reports. Like data is combined here */
typedef struct {
	/* part_info contains partition, avail, max_time, job_size, 
	 * root, share, groups */
	partition_info_t* part_ptr;
	char *color;
	List sub_list;
} sview_part_info_t;

enum { 
	EDIT_AVAIL = 1,
	EDIT_EDIT
};

/* These need to be in alpha order (except POS and CNT) */
enum { 
	SORTID_POS = POS_LOC,
	SORTID_AVAIL, 
#ifdef HAVE_BG
	SORTID_NODELIST, 
#endif
	SORTID_CPUS, 
	SORTID_DEFAULT,
	SORTID_FEATURES, 
	SORTID_GROUPS,
	SORTID_HIDDEN,
	SORTID_JOB_SIZE,
	SORTID_MAX_NODES,
	SORTID_MEM, 
	SORTID_MIN_NODES,
	SORTID_NAME, 
#ifndef HAVE_BG
	SORTID_NODELIST, 
#endif
	SORTID_NODES, 
	SORTID_ONLY_LINE, 
	SORTID_PRIORITY,
	SORTID_REASON,
	SORTID_ROOT, 
	SORTID_SHARE, 
	SORTID_STATE,
	SORTID_STATE_NUM,
	SORTID_TMP_DISK, 
	SORTID_TIMELIMIT, 
	SORTID_UPDATED, 
	SORTID_WEIGHT,
	SORTID_CNT
};

static display_data_t display_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, refresh_part},
	{G_TYPE_STRING, SORTID_NAME, "Partition", TRUE,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_DEFAULT, "Default", TRUE,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_HIDDEN, "Hidden", FALSE,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_AVAIL, "Availablity", TRUE,
	 EDIT_MODEL, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_TIMELIMIT, "Time Limit", 
	 TRUE, EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_NODES, "Nodes", TRUE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_STATE, "State", TRUE, EDIT_MODEL, refresh_part,
	 create_model_part, admin_edit_part},
#ifdef HAVE_BG
	{G_TYPE_STRING, SORTID_NODELIST, "BP List", TRUE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
#else
	{G_TYPE_STRING, SORTID_NODELIST, "NodeList", TRUE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
#endif
	{G_TYPE_STRING, SORTID_JOB_SIZE, "Job Size", FALSE,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_PRIORITY, "Priority", FALSE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MIN_NODES, "Min Nodes", FALSE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MAX_NODES, "Max Nodes", FALSE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_ROOT, "Root", FALSE, EDIT_MODEL, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_SHARE, "Share", FALSE, EDIT_MODEL, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_GROUPS, "Groups", FALSE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_CPUS, "CPUs", FALSE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_TMP_DISK, "Temp Disk", FALSE,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_MEM, "Memory", FALSE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_WEIGHT, "Weight", FALSE,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_FEATURES, "Features", FALSE,
	 EDIT_TEXTBOX, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_STRING, SORTID_REASON, "Reason", FALSE,
	 EDIT_NONE, refresh_part, create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_STATE_NUM, NULL, FALSE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_ONLY_LINE, NULL, FALSE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_part,
	 create_model_part, admin_edit_part},

	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_part[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, PART_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, PART_PAGE, "Drain Base Partitions", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Resume Base Partitions", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Put Base Partitions Down",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Make Base Partitions Idle",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Update Base Partition Features",
	 TRUE, ADMIN_PAGE},
#else
	{G_TYPE_STRING, PART_PAGE, "Drain Nodes", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Resume Nodes", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Put Nodes Down", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Make Nodes Idle", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Update Node Features", TRUE, ADMIN_PAGE},
#endif
	{G_TYPE_STRING, PART_PAGE, "Change Availablity Up/Down",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Edit Part", TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, PART_PAGE},
#ifdef HAVE_BG
	{G_TYPE_STRING, BLOCK_PAGE, "Blocks", TRUE, PART_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Base Partitions", TRUE, PART_PAGE},
#else
	{G_TYPE_STRING, NODE_PAGE, "Nodes", TRUE, PART_PAGE},
#endif
	{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", FALSE, PART_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

#ifdef HAVE_BG
static void _update_nodes_for_bg(int node_scaling,
				 node_info_msg_t *node_msg,
				 bg_info_record_t *bg_info_record);
enum {
	SVIEW_BG_IDLE_STATE,
	SVIEW_BG_ERROR_STATE,
	SVIEW_BG_ALLOC_STATE
};
#endif

static display_data_t *local_display_data = NULL;

static char *got_edit_signal = NULL;
static char *got_features_edit_signal = NULL;

static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore,
				    GtkTreeIter *iter);
static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line);
static node_info_t *_find_node(char *node_name, node_info_msg_t *node_msg);

#ifdef HAVE_BG

static void _update_nodes_for_bg(int node_scaling,
				 node_info_msg_t *node_msg,
				 bg_info_record_t *bg_info_record)
{
	node_info_t *node_ptr = NULL;
	hostlist_t hl;
	char *node_name = NULL;

	/* we are using less than one node */
	if(bg_info_record->conn_type == SELECT_SMALL) 
		node_scaling = bg_info_record->node_cnt;
       		   
	hl = hostlist_create(bg_info_record->nodes);
	while (1) {
		if (node_name)
			free(node_name);
		node_name = hostlist_shift(hl);
		if (!node_name)
			break;
		node_ptr = _find_node(node_name, node_msg);
		if (!node_ptr)
			continue;
		/* cores is overloaded to be the cnodes in an error
		 * state and used_cpus is overloaded to be the nodes in
		 * use.  No block should be sent in here if it isn't
		 * in use (that doesn't mean in a free state, it means
		 * the user isn't slurm or the block is in an error state.  
		 */
		if(bg_info_record->state == RM_PARTITION_ERROR) 
			node_ptr->cores += node_scaling;
		else
			node_ptr->used_cpus += node_scaling;
	}
	hostlist_destroy(hl);
	
}
#endif

static int 
_build_min_max_16_string(char *buffer, int buf_size, 
	uint16_t min, uint16_t max, bool range)
{
	char tmp_min[8];
	char tmp_max[8];
	convert_num_unit((float)min, tmp_min, sizeof(tmp_min), UNIT_NONE);
	if(max != (uint16_t) INFINITE) {
		convert_num_unit((float)max, tmp_max, sizeof(tmp_max), 
				 UNIT_NONE);
	}
	
	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == (uint16_t) INFINITE)
			return snprintf(buffer, buf_size, "%s-infinite", 
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s", 
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static int 
_build_min_max_32_string(char *buffer, int buf_size, 
	uint32_t min, uint32_t max, bool range)
{
	char tmp_min[8];
	char tmp_max[8];
	convert_num_unit((float)min, tmp_min, sizeof(tmp_min), UNIT_NONE);
	convert_num_unit((float)max, tmp_max, sizeof(tmp_max), UNIT_NONE);
	
	if (max == min)
		return snprintf(buffer, buf_size, "%s", tmp_max);
	else if (range) {
		if (max == (uint32_t) INFINITE)
			return snprintf(buffer, buf_size, "%s-infinite", 
					tmp_min);
		else
			return snprintf(buffer, buf_size, "%s-%s", 
					tmp_min, tmp_max);
	} else
		return snprintf(buffer, buf_size, "%s+", tmp_min);
}

static void _set_active_combo_part(GtkComboBox *combo, 
				   GtkTreeModel *model, GtkTreeIter *iter,
				   int type)
{
	char *temp_char = NULL;
	int action = 0;
	int i = 0, unknown_found = 0;
	char *upper = NULL;

	gtk_tree_model_get(model, iter, type, &temp_char, -1);
	if(!temp_char)
		goto end_it;
	switch(type) {
	case SORTID_DEFAULT:
	case SORTID_HIDDEN:
	case SORTID_ROOT:
		if(!strcmp(temp_char, "yes"))
			action = 0;
		else if(!strcmp(temp_char, "no"))
			action = 1;
		else 
			action = 0;
		
		break;
	case SORTID_SHARE:
		if(!strcmp(temp_char, "yes"))
			action = 0;
		else if(!strcmp(temp_char, "no"))
			action = 1;
		else if(!strcmp(temp_char, "force"))
			action = 2;
		else if(!strcmp(temp_char, "exclusive"))
			action = 3;
		else 
			action = 0;
		break;
	case SORTID_AVAIL:
		if(!strcmp(temp_char, "up"))
			action = 0;
		else if(!strcmp(temp_char, "down"))
			action = 1;
		else 
			action = 0;
		break;
	case SORTID_STATE:
		if(!strcasecmp(temp_char, "drain"))
			action = 0;
		else if(!strcasecmp(temp_char, "resume"))
			action = 1;
		else
			for(i = 0; i < NODE_STATE_END; i++) {
				upper = node_state_string(i);
				if(!strcmp(upper, "UNKNOWN")) {
					unknown_found++;
					continue;
				}
				
				if(!strcasecmp(temp_char, upper)) {
					action = i + 2 - unknown_found;
					break;
				}
			}
						
		break;
	default:
		break;
	}
	g_free(temp_char);
end_it:
	gtk_combo_box_set_active(combo, action);
	
}

/* don't free this char */
static const char *_set_part_msg(update_part_msg_t *part_msg,
				 const char *new_text,
				 int column)
{
	char *type = NULL;
	int temp_int = 0;

	errno = 0;

	if(!part_msg)
		return NULL;
	
	switch(column) {
	case SORTID_DEFAULT:
		if (!strcasecmp(new_text, "yes")) 
			part_msg->default_part = 1;
		else
			part_msg->default_part = 0;
		
		type = "default";
		break;
	case SORTID_HIDDEN:
		if (!strcasecmp(new_text, "yes")) 
			part_msg->hidden = 1;
		else
			part_msg->hidden = 0;
		
		type = "hidden";
		break;
	case SORTID_TIMELIMIT:
		if ((strcasecmp(new_text,"infinite") == 0))
			temp_int = INFINITE;
		else
			temp_int = time_str2mins((char *)new_text);
		
		type = "timelimit";
		if(temp_int <= 0 && temp_int != INFINITE)
			goto return_error;
		part_msg->max_time = (uint32_t)temp_int;
		break;
	case SORTID_PRIORITY:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "priority";
		part_msg->priority = (uint16_t)temp_int;
		break;
	case SORTID_MIN_NODES:
		temp_int = strtol(new_text, (char **)NULL, 10);
		type = "min_nodes";
		
		if(temp_int <= 0)
			goto return_error;
		part_msg->min_nodes = (uint32_t)temp_int;
		break;
	case SORTID_MAX_NODES:
		if (!strcasecmp(new_text, "infinite")) {
			temp_int = INFINITE;
		} else {
			temp_int = strtol(new_text, (char **)NULL, 10);
		}
		
		type = "max_nodes";
		if(temp_int <= 0 && temp_int != INFINITE)
			goto return_error;
		part_msg->max_nodes = (uint32_t)temp_int;
		break;
	case SORTID_ROOT:
		if (!strcasecmp(new_text, "yes")) {
			part_msg->root_only = 1;
		} else {
			part_msg->root_only = 0;
		}
		
		type = "root";
		break;
	case SORTID_SHARE:
		if (!strcasecmp(new_text, "yes")) {
			 part_msg->max_share = 4;
		} else if (!strcasecmp(new_text, "exclusive")) {
			part_msg->max_share = 0;
		} else if (!strcasecmp(new_text, "force")) {
			part_msg->max_share = SHARED_FORCE | 4;
		} else {	/* "no" */
			part_msg->max_share = 1;
		}
		type = "share";
		break;
	case SORTID_GROUPS:
		type = "groups";
		part_msg->allow_groups = xstrdup(new_text);
		break;
	case SORTID_NODELIST:
		part_msg->nodes = xstrdup(new_text);
		type = "nodelist";
		break;
	case SORTID_AVAIL:
		if (!strcasecmp(new_text, "up"))
			part_msg->state_up = 1;
		else
			part_msg->state_up = 0;
		type = "availability";
		break;
	case SORTID_STATE:
		type = (char *)new_text;
		got_edit_signal = xstrdup(new_text);
		break;
	case SORTID_FEATURES:
		type = "Update Features";
		got_features_edit_signal = xstrdup(new_text);
		break;
	}
	
	return type;

return_error:
	errno = 1;
	return type;
	
}

static void _admin_edit_combo_box_part(GtkComboBox *combo,
				       update_part_msg_t *part_msg)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;
	
	if(!part_msg)
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

	_set_part_msg(part_msg, name, column);

	g_free(name);
}

static gboolean _admin_focus_out_part(GtkEntry *entry,
				      GdkEventFocus *event, 
				      update_part_msg_t *part_msg)
{
	int type = gtk_entry_get_max_length(entry);
	const char *name = gtk_entry_get_text(entry);
	type -= DEFAULT_ENTRY_LENGTH;
	_set_part_msg(part_msg, name, type);
	
	return false;
}

static GtkWidget *_admin_full_edit_part(update_part_msg_t *part_msg, 
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
		if(display_data_part[i].extra == EDIT_MODEL) {
			/* edittable items that can only be known
			   values */
			model2 = GTK_TREE_MODEL(
				create_model_part(display_data_part[i].id));
			if(!model2) {
				g_print("no model set up for %d(%s)\n",
					display_data_part[i].id,
					display_data_part[i].name);
				continue;
			}
			entry = gtk_combo_box_new_with_model(model2);
			g_object_unref(model2);
			
			_set_active_combo_part(GTK_COMBO_BOX(entry), model,
					      iter, display_data_part[i].id);
			
			g_signal_connect(entry, "changed",
					 G_CALLBACK(
						 _admin_edit_combo_box_part),
					 part_msg);
			
			renderer = gtk_cell_renderer_text_new();
			gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(entry),
						   renderer, TRUE);
			gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(entry),
						      renderer, "text", 0);
		} else if(display_data_part[i].extra == EDIT_TEXTBOX) {
			/* other edittable items that are unknown */
			entry = create_entry();
			gtk_tree_model_get(model, iter,
					   display_data_part[i].id,
					   &temp_char, -1);
			gtk_entry_set_max_length(GTK_ENTRY(entry), 
						 (DEFAULT_ENTRY_LENGTH +
						  display_data_part[i].id));
			
			if(temp_char) {
				gtk_entry_set_text(GTK_ENTRY(entry),
						   temp_char);
				g_free(temp_char);
			}
			g_signal_connect(entry, "focus-out-event",
					 G_CALLBACK(_admin_focus_out_part),
					 part_msg);
		} else /* others can't be altered by the user */
			continue;
		label = gtk_label_new(display_data_part[i].name);
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

static void _subdivide_part(sview_part_info_t *sview_part_info,
			    GtkTreeModel *model, 
			    GtkTreeIter *sub_iter,
			    GtkTreeIter *iter)
{
	GtkTreeIter first_sub_iter;
	ListIterator itr = NULL;
	uint16_t state;
	int i = 0, line = 0;
	sview_part_sub_t *sview_part_sub = NULL;
	int set = 0;

	memset(&first_sub_iter, 0, sizeof(GtkTreeIter));

	/* make sure all the steps are still here */
	if (sub_iter) {
		first_sub_iter = *sub_iter;
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), sub_iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
		memcpy(sub_iter, &first_sub_iter, sizeof(GtkTreeIter));
		set = 1;
	}
	itr = list_iterator_create(sview_part_info->sub_list);
	if(list_count(sview_part_info->sub_list) == 1) {
		gtk_tree_store_set(GTK_TREE_STORE(model), iter,
				   SORTID_ONLY_LINE, 1, -1);
		sview_part_sub = list_next(itr);
		_update_part_sub_record(sview_part_sub,
					GTK_TREE_STORE(model), 
					iter);
	} else {
		while((sview_part_sub = list_next(itr))) {
			if (!sub_iter) {
				i = NO_VAL;
				goto adding;
			} else {
				memcpy(sub_iter, &first_sub_iter, 
				       sizeof(GtkTreeIter));		
			}
			
			while(1) {
				/* search for the state number and
				   check to see if it is in the list */
				gtk_tree_model_get(model, sub_iter, 
						   SORTID_STATE_NUM, 
						   &state, -1);
				if(state == sview_part_sub->node_state) {
					/* update with new info */
					_update_part_sub_record(
						sview_part_sub,
						GTK_TREE_STORE(model), 
						sub_iter);
					goto found;
				}			
				
				/* see what line we were on to add the
				   next one to the list */
				gtk_tree_model_get(model, sub_iter, 
						   SORTID_POS, 
						   &line, -1);
				if(!gtk_tree_model_iter_next(model, 
							     sub_iter)) {
					line++;
					break;
				}
			}
		adding:
			_append_part_sub_record(sview_part_sub, 
						GTK_TREE_STORE(model), 
						iter, line);
			if(i == NO_VAL)
				line++;
		found:
			;
		}
	}
	list_iterator_destroy(itr);

	if(set) {
		sub_iter = &first_sub_iter;
		/* clear all steps that aren't active */
		while(1) {
			gtk_tree_model_get(model, sub_iter, 
					   SORTID_UPDATED, &i, -1);
			if(!i) {
				if(!gtk_tree_store_remove(
					   GTK_TREE_STORE(model), 
					   sub_iter))
					break;
				else
					continue;
			}
			if(!gtk_tree_model_iter_next(model, sub_iter)) {
				break;
			}
		}
	}
	return;
}

static void _layout_part_record(GtkTreeView *treeview, 
				sview_part_info_t *sview_part_info,
				int update)
{
	GtkTreeIter iter;
	ListIterator itr = NULL;
	char time_buf[20], tmp_buf[20];
	char tmp_cnt[8];
	char tmp_cnt1[8];
	char tmp_cnt2[8];
	partition_info_t *part_ptr = sview_part_info->part_ptr;
	sview_part_sub_t *sview_part_sub = NULL;
	sview_part_sub_t *temp_part_sub = NULL;
	sview_part_sub_t alloc_part_sub;
	sview_part_sub_t idle_part_sub;
	sview_part_sub_t other_part_sub;
	char tmp[1024];
	char *temp_char = NULL;
	int global_set = 0;

	GtkTreeStore *treestore = 
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));
	
	memset(&alloc_part_sub, 0, sizeof(sview_part_sub_t));
	memset(&idle_part_sub, 0, sizeof(sview_part_sub_t));
	memset(&other_part_sub, 0, sizeof(sview_part_sub_t));
	
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_NAME),
				   part_ptr->name);

	if(part_ptr->default_part) 
		temp_char = "yes";
	else 
		temp_char = "no";
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_DEFAULT),
				   temp_char);
	
	if(part_ptr->hidden) 
		temp_char = "yes";
	else 
		temp_char = "no";
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_HIDDEN),
				   temp_char);
	
	if (part_ptr->state_up) 
		temp_char = "up";
	else 
		temp_char = "down";
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_AVAIL),
				   temp_char);
			
	if (part_ptr->max_time == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		secs2time_str((part_ptr->max_time * 60), 
			      time_buf, sizeof(time_buf));
	}

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_TIMELIMIT),
				   time_buf);
	
	_build_min_max_32_string(time_buf, sizeof(time_buf), 
			      part_ptr->min_nodes, 
			      part_ptr->max_nodes, true);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_JOB_SIZE),
				   time_buf);

	convert_num_unit((float)part_ptr->priority,
			 time_buf, sizeof(time_buf), UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_part,
						 SORTID_PRIORITY),
				   time_buf);
				   
	if (part_ptr->min_nodes == (uint32_t) INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->min_nodes, 
				 time_buf, sizeof(time_buf), UNIT_NONE);
	}
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_MIN_NODES), 
				   time_buf);
	if (part_ptr->max_nodes == (uint32_t) INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->max_nodes, 
				 time_buf, sizeof(time_buf), UNIT_NONE);
	}
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_MAX_NODES), 
				   time_buf);

	if(part_ptr->root_only)
		temp_char = "yes";
	else 
		temp_char = "no";
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_part,
						 SORTID_ROOT),
				   temp_char);
		
	if(part_ptr->max_share & SHARED_FORCE) {
		snprintf(tmp_buf, sizeof(tmp_buf), "force:%u", 
			 (part_ptr->max_share & ~(SHARED_FORCE))); 
		temp_char = tmp_buf;
	} else if(part_ptr->max_share == 0)
		temp_char = "exclusive";
	else if(part_ptr->max_share > 1) {
		snprintf(tmp_buf, sizeof(tmp_buf), "yes:%u", 
			 part_ptr->max_share);
		temp_char = tmp_buf;
	} else 
		temp_char = "no";
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_part,
						 SORTID_SHARE),
				   temp_char);
		
	if(part_ptr->allow_groups)
		temp_char = part_ptr->allow_groups;
	else
		temp_char = "all";
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_GROUPS),
				   temp_char);
	
#ifdef HAVE_BG
	convert_num_unit((float)part_ptr->total_nodes, tmp_cnt, 
			 sizeof(tmp_cnt), UNIT_NONE);
#else
	sprintf(tmp_cnt, "%u", part_ptr->total_nodes);
#endif
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_NODES), 
				   tmp_cnt);
	convert_num_unit((float)part_ptr->total_cpus, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_CPUS), 
				   tmp_cnt);
	

	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_NODELIST), 
				   part_ptr->nodes);
	itr = list_iterator_create(sview_part_info->sub_list);
	while((sview_part_sub = list_next(itr))) {
		if(sview_part_sub->node_state == NODE_STATE_IDLE)
			temp_part_sub = &idle_part_sub;
		else if(sview_part_sub->node_state == NODE_STATE_ALLOCATED)
			temp_part_sub = &alloc_part_sub;
		else
			temp_part_sub = &other_part_sub;
		temp_part_sub->node_cnt += sview_part_sub->node_cnt;
		temp_part_sub->min_cpus += sview_part_sub->min_cpus;
		temp_part_sub->max_cpus += sview_part_sub->max_cpus;
		
		temp_part_sub->min_disk += sview_part_sub->min_disk;
		temp_part_sub->max_disk += sview_part_sub->max_disk;
		
		temp_part_sub->min_mem += sview_part_sub->min_mem;
		temp_part_sub->max_mem += sview_part_sub->max_mem;
		
		temp_part_sub->min_weight += sview_part_sub->min_weight;
		temp_part_sub->max_weight += sview_part_sub->max_weight;
		if(!global_set) {
			global_set = 1;
			/* store features and reasons in the others
			   group */
			other_part_sub.features = temp_part_sub->features;
			other_part_sub.reason = temp_part_sub->reason;
		}
	}
	convert_num_unit((float)alloc_part_sub.node_cnt, 
			 tmp_cnt, sizeof(tmp_cnt), UNIT_NONE);
	convert_num_unit((float)idle_part_sub.node_cnt, 
			 tmp_cnt1, sizeof(tmp_cnt1), UNIT_NONE);
	convert_num_unit((float)other_part_sub.node_cnt, 
			 tmp_cnt2, sizeof(tmp_cnt2), UNIT_NONE);
	snprintf(tmp, sizeof(tmp), "%s/%s/%s",
		 tmp_cnt, tmp_cnt1, tmp_cnt2);
	add_display_treestore_line(update, treestore, &iter,
				   "Nodes (Allocated/Idle/Other)",
				   tmp);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_FEATURES), 
				   other_part_sub.features);
	add_display_treestore_line(update, treestore, &iter, 
				   find_col_name(display_data_part,
						 SORTID_REASON), 
				   other_part_sub.reason);

}

static void _update_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore, 
				GtkTreeIter *iter)
{
	char time_buf[20], tmp_buf[20];
	char tmp_cnt[8];
	char *temp_char = NULL;
	partition_info_t *part_ptr = sview_part_info->part_ptr;
	GtkTreeIter sub_iter;
	int childern = 0;
	
	gtk_tree_store_set(treestore, iter, SORTID_NAME, part_ptr->name, -1);

	if(part_ptr->default_part)
		temp_char = "yes";
	else
		temp_char = "no";
	gtk_tree_store_set(treestore, iter, SORTID_DEFAULT, temp_char, -1);
	
	if(part_ptr->hidden)
		temp_char = "yes";
	else
		temp_char = "no";
	gtk_tree_store_set(treestore, iter, SORTID_HIDDEN, temp_char, -1);
	
	if (part_ptr->state_up) 
		temp_char = "up";
	else
		temp_char = "down";
	gtk_tree_store_set(treestore, iter, SORTID_AVAIL, temp_char, -1);
		
	if (part_ptr->max_time == INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		secs2time_str((part_ptr->max_time * 60), 
			      time_buf, sizeof(time_buf));
	}
	
	gtk_tree_store_set(treestore, iter, SORTID_TIMELIMIT, time_buf, -1);
	
	_build_min_max_32_string(time_buf, sizeof(time_buf), 
			      part_ptr->min_nodes, 
			      part_ptr->max_nodes, true);
	gtk_tree_store_set(treestore, iter, SORTID_JOB_SIZE, time_buf, -1);
	
	convert_num_unit((float)part_ptr->priority,
			 time_buf, sizeof(time_buf), UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_PRIORITY,
			   time_buf, -1);

	if (part_ptr->min_nodes == (uint32_t) INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->min_nodes, 
				 time_buf, sizeof(time_buf), UNIT_NONE);
	}
	gtk_tree_store_set(treestore, iter, SORTID_MIN_NODES, 
			   time_buf, -1);
	if (part_ptr->max_nodes == (uint32_t) INFINITE)
		snprintf(time_buf, sizeof(time_buf), "infinite");
	else {
		convert_num_unit((float)part_ptr->max_nodes, 
				 time_buf, sizeof(time_buf), UNIT_NONE);
	}
	gtk_tree_store_set(treestore, iter, SORTID_MAX_NODES, 
			   time_buf, -1);

	if(part_ptr->root_only)
		temp_char = "yes";
	else
		temp_char = "no";
	gtk_tree_store_set(treestore, iter, SORTID_ROOT, temp_char, -1);
	
	if(part_ptr->max_share & SHARED_FORCE) {
		snprintf(tmp_buf, sizeof(tmp_buf), "force:%u", 
			 (part_ptr->max_share & ~(SHARED_FORCE))); 
		temp_char = tmp_buf;
	} else if(part_ptr->max_share == 0)
		temp_char = "exclusive";
	else if(part_ptr->max_share > 1) {
		snprintf(tmp_buf, sizeof(tmp_buf), "yes:%u", 
			 part_ptr->max_share);
		temp_char = tmp_buf;
	} else 
		temp_char = "no";
	gtk_tree_store_set(treestore, iter, SORTID_SHARE, temp_char, -1);
	
	if(part_ptr->allow_groups)
		temp_char = part_ptr->allow_groups;
	else
		temp_char = "all";
	gtk_tree_store_set(treestore, iter, SORTID_GROUPS, temp_char, -1);
	
#ifdef HAVE_BG
	convert_num_unit((float)part_ptr->total_nodes, tmp_cnt, 
			 sizeof(tmp_cnt), UNIT_NONE);
#else
	sprintf(tmp_cnt, "%u", part_ptr->total_nodes);
#endif
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);

	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, 
			   part_ptr->nodes, -1);
	
	gtk_tree_store_set(treestore, iter, SORTID_ONLY_LINE, 0, -1);
	/* clear out info for the main listing */
	gtk_tree_store_set(treestore, iter, SORTID_STATE, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_STATE_NUM, -1, -1);
	gtk_tree_store_set(treestore, iter, SORTID_CPUS, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_TMP_DISK, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_MEM, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_WEIGHT, "", -1);
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);	
	gtk_tree_store_set(treestore, iter, SORTID_FEATURES, "", -1);	
	gtk_tree_store_set(treestore, iter, SORTID_REASON, "", -1);	
	
	childern = gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
						&sub_iter, iter);
	if(gtk_tree_model_iter_children(GTK_TREE_MODEL(treestore),
					&sub_iter, iter))
		_subdivide_part(sview_part_info, 
				GTK_TREE_MODEL(treestore), &sub_iter, iter);
	else
		_subdivide_part(sview_part_info, 
				GTK_TREE_MODEL(treestore), NULL, iter);

	return;
}

static void _update_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter)
{
	char time_buf[20];
	char tmp_cnt[8];
	partition_info_t *part_ptr = sview_part_sub->part_ptr;
	char *upper = NULL, *lower = NULL;		     
	char tmp[MAXHOSTRANGELEN];

	gtk_tree_store_set(treestore, iter, SORTID_NAME, part_ptr->name, -1);
	
	upper = node_state_string(sview_part_sub->node_state);
	lower = str_tolower(upper);
	gtk_tree_store_set(treestore, iter, SORTID_STATE, 
			   lower, -1);
	xfree(lower);
	
	gtk_tree_store_set(treestore, iter, SORTID_STATE_NUM,
			   sview_part_sub->node_state, -1);
	
	_build_min_max_16_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_cpus, 
			      sview_part_sub->max_cpus, false);
	gtk_tree_store_set(treestore, iter, SORTID_CPUS, time_buf, -1);
	
	_build_min_max_32_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_disk, 
			      sview_part_sub->max_disk, false);
	gtk_tree_store_set(treestore, iter, SORTID_TMP_DISK, time_buf, -1);

	_build_min_max_32_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_mem, 
			      sview_part_sub->max_mem, false);
	gtk_tree_store_set(treestore, iter, SORTID_MEM, time_buf, -1);

	_build_min_max_32_string(time_buf, sizeof(time_buf), 
			      sview_part_sub->min_weight, 
			      sview_part_sub->max_weight, false);
	gtk_tree_store_set(treestore, iter, SORTID_WEIGHT, time_buf, -1);

	convert_num_unit((float)sview_part_sub->node_cnt, tmp_cnt, 
			 sizeof(tmp_cnt), UNIT_NONE);
	gtk_tree_store_set(treestore, iter, SORTID_NODES, tmp_cnt, -1);
	
	hostlist_ranged_string(sview_part_sub->hl, sizeof(tmp), tmp);
	gtk_tree_store_set(treestore, iter, SORTID_NODELIST, 
			   tmp, -1);
	gtk_tree_store_set(treestore, iter, SORTID_UPDATED, 1, -1);	
	
	gtk_tree_store_set(treestore, iter, SORTID_FEATURES,
			   sview_part_sub->features, -1);
	gtk_tree_store_set(treestore, iter, SORTID_REASON,
			   sview_part_sub->reason, -1);
		
	return;
}

static void _append_part_record(sview_part_info_t *sview_part_info,
				GtkTreeStore *treestore, GtkTreeIter *iter,
				int line)
{
	gtk_tree_store_append(treestore, iter, NULL);
	gtk_tree_store_set(treestore, iter, SORTID_POS, line, -1);
	_update_part_record(sview_part_info, treestore, iter);
}

static void _append_part_sub_record(sview_part_sub_t *sview_part_sub,
				    GtkTreeStore *treestore, GtkTreeIter *iter,
				    int line)
{
	GtkTreeIter sub_iter;
	
	gtk_tree_store_append(treestore, &sub_iter, iter);
	gtk_tree_store_set(treestore, &sub_iter, SORTID_POS, line, -1);
	_update_part_sub_record(sview_part_sub, treestore, &sub_iter);
}

static void _update_info_part(List info_list, 
			      GtkTreeView *tree_view)
{
	GtkTreePath *path = gtk_tree_path_new_first();
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	GtkTreeIter iter;
	partition_info_t *part_ptr = NULL;
	int line = 0;
	char *host = NULL, *part_name = NULL;
	ListIterator itr = NULL;
	sview_part_info_t *sview_part_info = NULL;

	/* get the iter, or find out the list is empty goto add */
	if (gtk_tree_model_get_iter(model, &iter, path)) {
		/* make sure all the partitions are still here */
		while(1) {
			gtk_tree_store_set(GTK_TREE_STORE(model), &iter, 
					   SORTID_UPDATED, 0, -1);	
			if(!gtk_tree_model_iter_next(model, &iter)) {
				break;
			}
		}
	}

	itr = list_iterator_create(info_list);
	while ((sview_part_info = (sview_part_info_t*) list_next(itr))) {
		part_ptr = sview_part_info->part_ptr;
		/* get the iter, or find out the list is empty goto add */
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			goto adding;
		} 
		while(1) {
			/* search for the jobid and check to see if 
			   it is in the list */
			gtk_tree_model_get(model, &iter, SORTID_NAME, 
					   &part_name, -1);
			if(!strcmp(part_name, part_ptr->name)) {
				/* update with new info */
				g_free(part_name);
				_update_part_record(sview_part_info, 
						    GTK_TREE_STORE(model), 
						    &iter);
				goto found;
			}
			g_free(part_name);
				
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
		_append_part_record(sview_part_info, GTK_TREE_STORE(model), 
				    &iter, line);
	found:
		;
	}
	list_iterator_destroy(itr);
	if(host)
		free(host);

	gtk_tree_path_free(path);
	/* remove all old partitions */
	remove_old(model, SORTID_UPDATED);
	return;
}

static void _part_info_list_del(void *object)
{
	sview_part_info_t *sview_part_info = (sview_part_info_t *)object;

	if (sview_part_info) {
		if(sview_part_info->sub_list)
			list_destroy(sview_part_info->sub_list);
		xfree(sview_part_info);
	}
}

static void _destroy_part_sub(void *object)
{
	sview_part_sub_t *sview_part_sub = (sview_part_sub_t *)object;

	if (sview_part_sub) {
		xfree(sview_part_sub->features);
		xfree(sview_part_sub->reason);
		if(sview_part_sub->hl)
			hostlist_destroy(sview_part_sub->hl);
		if(sview_part_sub->node_ptr_list)
			list_destroy(sview_part_sub->node_ptr_list);
		xfree(sview_part_sub);
	}
}

/* like strcmp, but works with NULL pointers */
static int _strcmp(char *data1, char *data2) 
{
	static char null_str[] = "(null)";

	if (data1 == NULL)
		data1 = null_str;
	if (data2 == NULL)
		data2 = null_str;
	return strcmp(data1, data2);
}

/* 
 * _find_node - find a node by name
 * node_name IN     - name of node to locate
 * node_msg IN      - node information message from API
 */
static node_info_t *_find_node(char *node_name, node_info_msg_t *node_msg)
{
	int i;
	if (node_name == NULL)
		return NULL;

	for (i=0; i<node_msg->record_count; i++) {
		if (_strcmp(node_name, node_msg->node_array[i].name))
			continue;
		return &(node_msg->node_array[i]);
	}

	/* not found */
	return NULL;
}

static void _update_sview_part_sub(sview_part_sub_t *sview_part_sub, 
				   node_info_t *node_ptr, 
				   int node_scaling)
{
	list_append(sview_part_sub->node_ptr_list, node_ptr);

#ifdef HAVE_BG
	node_scaling = node_ptr->threads;
	if(!node_scaling)
		return;
#else
	if(!node_scaling)
		node_scaling = 1;
#endif	
	if (sview_part_sub->node_cnt == 0) {	/* first node added */
		sview_part_sub->node_state = node_ptr->node_state;
		sview_part_sub->features   = xstrdup(node_ptr->features);
		sview_part_sub->reason     = xstrdup(node_ptr->reason);
		sview_part_sub->min_cpus   = node_ptr->cpus;
		sview_part_sub->max_cpus   = node_ptr->cpus;
		sview_part_sub->min_disk   = node_ptr->tmp_disk;
		sview_part_sub->max_disk   = node_ptr->tmp_disk;
		sview_part_sub->min_mem    = node_ptr->real_memory;
		sview_part_sub->max_mem    = node_ptr->real_memory;
		sview_part_sub->min_weight = node_ptr->weight;
		sview_part_sub->max_weight = node_ptr->weight;
	} else if (hostlist_find(sview_part_sub->hl, 
				 node_ptr->name) != -1) {
		/* we already have this node in this record,
		 * just return, don't duplicate */
		return;
	} else {
		if (sview_part_sub->min_cpus > node_ptr->cpus)
			sview_part_sub->min_cpus = node_ptr->cpus;
		if (sview_part_sub->max_cpus < node_ptr->cpus)
			sview_part_sub->max_cpus = node_ptr->cpus;

		if (sview_part_sub->min_disk > node_ptr->tmp_disk)
			sview_part_sub->min_disk = node_ptr->tmp_disk;
		if (sview_part_sub->max_disk < node_ptr->tmp_disk)
			sview_part_sub->max_disk = node_ptr->tmp_disk;

		if (sview_part_sub->min_mem > node_ptr->real_memory)
			sview_part_sub->min_mem = node_ptr->real_memory;
		if (sview_part_sub->max_mem < node_ptr->real_memory)
			sview_part_sub->max_mem = node_ptr->real_memory;

		if (sview_part_sub->min_weight> node_ptr->weight)
			sview_part_sub->min_weight = node_ptr->weight;
		if (sview_part_sub->max_weight < node_ptr->weight)
			sview_part_sub->max_weight = node_ptr->weight;
	}

	sview_part_sub->node_cnt += node_scaling;
	hostlist_push(sview_part_sub->hl, node_ptr->name);
}

/* 
 * _create_sview_part_sub - create an sview_part_sub record for 
 *                          the given partition
 * sview_part_sub OUT     - ptr to an inited sview_part_sub_t
 */
static sview_part_sub_t *_create_sview_part_sub(partition_info_t *part_ptr,
						node_info_t *node_ptr, 
						int node_scaling)
{
	sview_part_sub_t *sview_part_sub_ptr = 
		xmalloc(sizeof(sview_part_sub_t));
	
#ifdef HAVE_BG
	node_scaling = node_ptr->threads;
	if(!node_scaling)
		return NULL;
#else
	if(!node_scaling)
		node_scaling = 1;
#endif

	if (!part_ptr) {
		g_print("got no part_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	if (!node_ptr) {
		g_print("got no node_ptr!\n");
		xfree(sview_part_sub_ptr);
		return NULL;
	}
	sview_part_sub_ptr->part_ptr = part_ptr;
	sview_part_sub_ptr->node_state = node_ptr->node_state;
	sview_part_sub_ptr->node_cnt = node_scaling;
	
	sview_part_sub_ptr->min_cpus = node_ptr->cpus;
	sview_part_sub_ptr->max_cpus = node_ptr->cpus;

	sview_part_sub_ptr->min_disk = node_ptr->tmp_disk;
	sview_part_sub_ptr->max_disk = node_ptr->tmp_disk;

	sview_part_sub_ptr->min_mem = node_ptr->real_memory;
	sview_part_sub_ptr->max_mem = node_ptr->real_memory;

	sview_part_sub_ptr->min_weight = node_ptr->weight;
	sview_part_sub_ptr->max_weight = node_ptr->weight;

	sview_part_sub_ptr->features = xstrdup(node_ptr->features);
	sview_part_sub_ptr->reason   = xstrdup(node_ptr->reason);

	sview_part_sub_ptr->hl = hostlist_create(node_ptr->name);
	
	sview_part_sub_ptr->node_ptr_list = list_create(NULL);

	list_push(sview_part_sub_ptr->node_ptr_list, node_ptr);

	return sview_part_sub_ptr;
}

/* 
 * _create_sview_part_info - create an sview_part_info record for 
 *                           the given partition
 * part_ptr IN             - pointer to partition record to add
 * sview_part_info OUT     - ptr to an inited sview_part_info_t
 */
static sview_part_info_t *_create_sview_part_info(partition_info_t* part_ptr)
{
	sview_part_info_t *sview_part_info = 
		xmalloc(sizeof(sview_part_info_t));
		
	
	sview_part_info->part_ptr = part_ptr;
	sview_part_info->sub_list = list_create(_destroy_part_sub);
	return sview_part_info;
}

static List _create_part_info_list(partition_info_msg_t *part_info_ptr,
				   node_info_msg_t *node_info_ptr,
				   node_select_info_msg_t *node_select_ptr,
				   int changed)
{
	sview_part_info_t *sview_part_info = NULL;
	sview_part_sub_t *sview_part_sub = NULL;
	partition_info_t *part_ptr = NULL;
	node_info_t *node_ptr = NULL;
	static List info_list = NULL;
	char *node_name = NULL;
	int i, found = 0;
	ListIterator itr = NULL;
	hostlist_t hl;
#ifdef HAVE_BG
	int j;
	bg_info_record_t *bg_info_record = NULL;
	int node_scaling = part_info_ptr->partition_array[0].node_scaling;
	char *slurm_user = NULL;
#endif
	if(!changed && info_list) {
		return info_list;
	}
	
	if(info_list) {
		list_destroy(info_list);
	}
	info_list = list_create(_part_info_list_del);
	if (!info_list) {
		g_print("malloc error\n");
		return NULL;
	}

#ifdef HAVE_BG
	slurm_user = xstrdup(slurmctld_conf.slurm_user_name);

	for (i=0; i<node_info_ptr->record_count; i++) {
		node_ptr = &(node_info_ptr->node_array[i]);
		/* in each node_ptr we overload the threads var
		 * with the number of cnodes in the used_cpus var
		 * will be used to tell how many cnodes are
		 * allocated and the cores will represent the cnodes
		 * in an error state. So we can get an idle count by
		 * subtracting those 2 numbers from the total possible
		 * cnodes (which are the idle cnodes).
		 */
		node_ptr->threads = node_scaling;
		node_ptr->cores = 0;
		node_ptr->used_cpus = 0;
	}

	for (i=0; i<node_select_ptr->record_count; i++) {
		bg_info_record = &(node_select_ptr->bg_info_array[i]);
		
		/* this block is idle we won't mark it */
		if (bg_info_record->state != RM_PARTITION_ERROR
		    && !strcmp(slurm_user, bg_info_record->owner_name))
			continue;
		_update_nodes_for_bg(node_scaling, node_info_ptr,
				     bg_info_record);
	}
	xfree(slurm_user);

#endif


	for (i=0; i<part_info_ptr->record_count; i++) {
		part_ptr = &(part_info_ptr->partition_array[i]);
		if (!part_ptr->nodes || (part_ptr->nodes[0] == '\0'))
			continue;	/* empty partition */
		
		sview_part_info = _create_sview_part_info(part_ptr);
		hl = hostlist_create(part_ptr->nodes);
		while((node_name = hostlist_shift(hl))) {
			node_ptr = _find_node(node_name, node_info_ptr);
			free(node_name);
#ifdef HAVE_BG
			for(j=0; j<3; j++) {
				int norm = 0;
				switch(j) {
				case SVIEW_BG_IDLE_STATE:
					/* get the idle node count if
					 * we don't have any error or
					 * allocated nodes then we set
					 * the norm flag and add it
					 * as it's current state 
					 */
					node_ptr->threads -=
						(node_ptr->cores
						 + node_ptr->used_cpus);
					if(node_ptr->threads == node_scaling)
						norm = 1;
					else {
						node_ptr->node_state &=
							NODE_STATE_FLAGS;
						node_ptr->node_state |=
							NODE_STATE_IDLE;
					}
					break;
				case SVIEW_BG_ERROR_STATE:
					/* get the error node count */
					if(!node_ptr->cores) 
						continue;
					node_ptr->node_state |= 
						NODE_STATE_DRAIN;
					node_ptr->threads = node_ptr->cores;
					break;
				case SVIEW_BG_ALLOC_STATE:
					/* get the allocated node count */
					if(!node_ptr->used_cpus) 
						continue;
					node_ptr->node_state &=
						NODE_STATE_FLAGS;
					node_ptr->node_state |=
						NODE_STATE_ALLOCATED;
					
					node_ptr->threads =
						node_ptr->used_cpus;
					break;
				default:
					error("unknown state");
					break;
				}
#endif
			itr = list_iterator_create(sview_part_info->sub_list);
			while((sview_part_sub = list_next(itr))) {
				if(sview_part_sub->node_state
				   == node_ptr->node_state) {
					_update_sview_part_sub(
						sview_part_sub, 
						node_ptr,
						part_ptr->node_scaling);
					found = 1;
					break;
				}
			}
			list_iterator_destroy(itr);

			if(!found) {
				sview_part_sub = 
					_create_sview_part_sub(
						part_ptr,
						node_ptr,
						part_ptr->node_scaling);
				if(sview_part_sub)
					list_push(sview_part_info->sub_list, 
						  sview_part_sub);
			}

			found = 0;
#ifdef HAVE_BG
			/* if we used the current state of
			 * the node then we just continue.
			 */
			if(norm) 
				break;
			}
#endif
		}
		hostlist_destroy(hl);
		list_append(info_list, sview_part_info);
	}
	return info_list;
}

void _display_info_part(List info_list,	popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int found = 0;
	partition_info_t *part_ptr = NULL;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;
	sview_part_info_t *sview_part_info = NULL;
	int update = 0;
	int i = -1, j = 0;
	int first_time = 0;

	if(!spec_info->search_info->gchar_data) {
		//info = xstrdup("No pointer given!");
		goto finished;
	}
	if(!list_count(popup_win->grid_button_list)) 
		first_time = 1;

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
	while ((sview_part_info = (sview_part_info_t*) list_next(itr))) {
		part_ptr = sview_part_info->part_ptr;
		i++;
		if(!strcmp(part_ptr->name, name)) {
			j=0;
			while(part_ptr->node_inx[j] >= 0) {
				if(!first_time)
					change_grid_color(
						popup_win->grid_button_list,
						part_ptr->node_inx[j],
						part_ptr->node_inx[j+1], i);
				else
					get_button_list_from_main(
						&popup_win->grid_button_list,
						part_ptr->node_inx[j],
						part_ptr->node_inx[j+1],
						i);
				j += 2;
			}
			_layout_part_record(treeview, sview_part_info, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	
	if(!found) {
		if(!popup_win->not_found) { 
			char *temp = "PARTITION DOESN'T EXSIST\n";
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
		put_buttons_in_table(popup_win->grid_table,
				     popup_win->grid_button_list);

	}
	gtk_widget_show(spec_info->display_widget);
		
finished:
		
	return;
}

extern void refresh_part(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win != NULL);
	xassert(popup_win->spec_info != NULL);
	xassert(popup_win->spec_info->title != NULL);
	popup_win->force_refresh = 1;
	specific_info_part(popup_win);
}

extern int get_new_info_part(partition_info_msg_t **part_ptr, int force)
{
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
		
	if(!force && ((now - last) < global_sleep_time)) {
		*part_ptr = part_info_ptr;
		if(changed) 
			return SLURM_SUCCESS;
		return error_code;
	}
	last = now;
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_partition_info_msg(part_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_part_ptr = part_info_ptr;
				changed = 0;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
		changed = 1;
	}
	
	part_info_ptr = new_part_ptr;
	*part_ptr = new_part_ptr;
	return error_code;
}

extern GtkListStore *create_model_part(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;
	char *upper = NULL, *lower = NULL;		     
	int i=0;
	switch(type) {
	case SORTID_DEFAULT:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, SORTID_DEFAULT,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, SORTID_DEFAULT,
				   -1);	

		break;
	case SORTID_HIDDEN:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, SORTID_HIDDEN,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, SORTID_HIDDEN,
				   -1);	

		break;
	case SORTID_PRIORITY:
	case SORTID_TIMELIMIT:
	case SORTID_MIN_NODES:
	case SORTID_MAX_NODES:
		break;
	case SORTID_ROOT:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, SORTID_ROOT,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, SORTID_ROOT,
				   -1);	
		break;
	case SORTID_SHARE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "force",
				   1, SORTID_SHARE,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, SORTID_SHARE,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, SORTID_SHARE,
				   -1);
		gtk_list_store_append(model, &iter);	
		gtk_list_store_set(model, &iter,
				   0, "exclusive",
				   1, SORTID_SHARE,
				   -1);
		break;
	case SORTID_GROUPS:
		break;
	case SORTID_NODELIST:
		break;
	case SORTID_AVAIL:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "up",
				   1, SORTID_AVAIL,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "down",
				   1, SORTID_AVAIL,
				   -1);	
		break;
	case SORTID_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "drain",
				   1, SORTID_STATE,
				   -1);	
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "resume",
				   1, SORTID_STATE,
				   -1);	
		for(i = 0; i < NODE_STATE_END; i++) {
			upper = node_state_string(i);
			if(!strcmp(upper, "UNKNOWN"))
				continue;
			
			gtk_list_store_append(model, &iter);
			lower = str_tolower(upper);
			gtk_list_store_set(model, &iter,
					   0, lower,
					   1, SORTID_STATE,
					   -1);
			xfree(lower);
		}
						
		break;

	}
	return model;
}

extern void admin_edit_part(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	update_part_msg_t *part_msg = xmalloc(sizeof(update_part_msg_t));
	
	char *temp = NULL;
	char *old_text = NULL;
	const char *type = NULL;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell), 
						       "column"));
	
	if(!new_text || !strcmp(new_text, ""))
		goto no_input;
	
	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);

	if(column != SORTID_STATE) {
		slurm_init_part_desc_msg(part_msg);
		gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter, 
				   SORTID_NAME, &temp, 
				   column, &old_text,
				   -1);
		part_msg->name = xstrdup(temp);
		g_free(temp);
	}
	
	type = _set_part_msg(part_msg, new_text, column);
	if(errno)
		goto print_error;
	if(got_edit_signal) {
		temp = got_edit_signal;
		got_edit_signal = NULL;
		admin_part(GTK_TREE_MODEL(treestore), &iter, temp);
		xfree(temp);
		goto no_input;
	}

	if(got_features_edit_signal) {
		admin_part(GTK_TREE_MODEL(treestore), &iter, (char *)type);
		goto no_input;
	}
	
	if(column != SORTID_STATE && column != SORTID_FEATURES ) {
		if(old_text && !strcmp(old_text, new_text)) {
			temp = g_strdup_printf("No change in value.");
		} else if(slurm_update_partition(part_msg) == SLURM_SUCCESS) {
			gtk_tree_store_set(treestore, &iter, column,
					   new_text, -1);
			temp = g_strdup_printf("Partition %s %s changed to %s",
					       part_msg->name,
					       type,
					       new_text);
		} else {
		print_error:
			temp = g_strdup_printf("Partition %s %s can't be "
					       "set to %s",
					       part_msg->name,
					       type,
					       new_text);
		}
		display_edit_note(temp);
		g_free(temp);	
	}
no_input:
	slurm_free_update_part_msg(part_msg);
	gtk_tree_path_free (path);
	g_free(old_text);
	g_static_mutex_unlock(&sview_mutex);
}

extern void get_info_part(GtkTable *table, display_data_t *display_data)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static int view = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	static node_select_info_msg_t *node_select_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List info_list = NULL;
	int changed = 1;
	int j=0, i=0;
	sview_part_info_t *sview_part_info = NULL;
	partition_info_t *part_ptr = NULL;
	ListIterator itr = NULL;
	
	if(display_data)
		local_display_data = display_data;
	if(!table) {
		display_data_part->set_menu = local_display_data->set_menu;
		return;
	}
	if(display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}
	
	if((part_error_code = get_new_info_part(&part_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		// just goto the new info node 
	} else 	if (part_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

	if((node_error_code = get_new_info_node(&node_info_ptr, force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		// just goto the new info node select
	} else if (node_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

	if((block_error_code = get_new_info_node_select(&node_select_ptr, 
							force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!display_widget || view == ERROR_VIEW) 
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA)
		   || (node_error_code != SLURM_NO_CHANGE_IN_DATA)) {
			goto display_it;
		}
		changed = 0;
	} else if (block_error_code != SLURM_SUCCESS) {
		if(view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if(display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_node_select: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(table, 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(label);
		goto end_it;
	}

display_it:

	info_list = _create_part_info_list(part_info_ptr,
					   node_info_ptr,
					   node_select_ptr,
					   changed);
	if(!info_list)
		return;
	/* set up the grid */
	itr = list_iterator_create(info_list);
	while ((sview_part_info = list_next(itr))) {
		part_ptr = sview_part_info->part_ptr;
		j=0;
		while(part_ptr->node_inx[j] >= 0) {
			sview_part_info->color = 
				change_grid_color(grid_button_list,
						  part_ptr->node_inx[j],
						  part_ptr->node_inx[j+1],
						  i);
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
		create_treestore(tree_view, display_data_part, SORTID_CNT);
	}
	view = INFO_VIEW;
	_update_info_part(info_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;
	
	return;
}

extern void specific_info_part(popup_info_t *popup_win)
{
	int part_error_code = SLURM_SUCCESS;
	int node_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static partition_info_msg_t *part_info_ptr = NULL;
	static node_info_msg_t *node_info_ptr = NULL;
	static node_select_info_msg_t *node_select_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List info_list = NULL;
	List send_info_list = NULL;
	int changed = 1;
	int j=0, i=-1;
	sview_part_info_t *sview_part_info_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	ListIterator itr = NULL;
	char *host = NULL, *host2 = NULL;
	hostlist_t hostlist = NULL;
	int found = 0;
	
	if(!spec_info->display_widget)
		setup_popup_info(popup_win, display_data_part, SORTID_CNT);
	
	if(spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if((part_error_code = get_new_info_part(&part_info_ptr, 
						popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA)  {
		
	} else if (part_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget) {
			gtk_widget_destroy(spec_info->display_widget);
			spec_info->display_widget = NULL;
		}
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_partitions: %s",
			 slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);			
		goto end_it;
	}

	if((node_error_code = get_new_info_node(&node_info_ptr, 
						popup_win->force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
					
	} else if (node_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		snprintf(error_char, 100, "slurm_load_node: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(GTK_WIDGET(label));
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);
		goto end_it;
	}

	if((block_error_code = get_new_info_node_select(&node_select_ptr, 
							force_refresh))
	   == SLURM_NO_CHANGE_IN_DATA) { 
		if((!spec_info->display_widget
		    || spec_info->view == ERROR_VIEW) 
		   || (part_error_code != SLURM_NO_CHANGE_IN_DATA)
		   || (node_error_code != SLURM_NO_CHANGE_IN_DATA))
			goto display_it;
		changed = 0;
	} else if (block_error_code != SLURM_SUCCESS) {
		if(spec_info->view == ERROR_VIEW)
			goto end_it;
		if(spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		spec_info->view = ERROR_VIEW;
		sprintf(error_char, "slurm_load_node_select: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		spec_info->display_widget = gtk_widget_ref(label);
		gtk_table_attach_defaults(popup_win->table, label, 0, 1, 0, 1);
		gtk_widget_show(label);	
		goto end_it;
	}

display_it:	
	
	info_list = _create_part_info_list(part_info_ptr,
					   node_info_ptr,
					   node_select_ptr,
					   changed);
	if(!info_list)
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
		create_treestore(tree_view, popup_win->display_data, 
				 SORTID_CNT);
	}
	
	if(popup_win->grid_button_list) {
		list_destroy(popup_win->grid_button_list);
	}	       
	
#ifdef HAVE_BG
	popup_win->grid_button_list = copy_main_button_list();
#else
	popup_win->grid_button_list = list_create(destroy_grid_button);
#endif	

	spec_info->view = INFO_VIEW;
	if(spec_info->type == INFO_PAGE) {
		_display_info_part(info_list, popup_win);
		goto end_it;
	} 
	
	/* just linking to another list, don't free the inside, just
	   the list */
	send_info_list = list_create(NULL);	
	
	itr = list_iterator_create(info_list);
	while ((sview_part_info_ptr = list_next(itr))) {
		i++;
		part_ptr = sview_part_info_ptr->part_ptr;	
		switch(spec_info->type) {
		case NODE_PAGE:
			if(!part_ptr->nodes)
				continue;

			hostlist = hostlist_create(
				spec_info->search_info->gchar_data);
			host = hostlist_shift(hostlist);
			hostlist_destroy(hostlist);
			if(!host) 
				continue;
			
			hostlist = hostlist_create(part_ptr->nodes);
			found = 0;
			while((host2 = hostlist_shift(hostlist))) { 
				if(!strcmp(host, host2)) {
					free(host2);
					found = 1;
					break; 
				}
				free(host2);
			}
			hostlist_destroy(hostlist);
			if(!found)
				continue;
			break;
		case PART_PAGE:
		case BLOCK_PAGE:
		case JOB_PAGE:
			if(strcmp(part_ptr->name, 
				  spec_info->search_info->gchar_data)) 
				continue;
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			list_iterator_destroy(itr);
			goto end_it;
		}
		list_push(send_info_list, sview_part_info_ptr);
		j=0;
		while(part_ptr->node_inx[j] >= 0) {
#ifdef HAVE_BG
			change_grid_color(
				popup_win->grid_button_list,
				part_ptr->node_inx[j],
				part_ptr->node_inx[j+1], i);
#else
			get_button_list_from_main(
				&popup_win->grid_button_list,
				part_ptr->node_inx[j],
				part_ptr->node_inx[j+1], i);
#endif
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	put_buttons_in_table(popup_win->grid_table,
			     popup_win->grid_button_list);
	 
	_update_info_part(send_info_list, 
			  GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_info_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;
		
	return;
}

extern void set_menus_part(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(menu, display_data_part, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_part);
		break;
	case POPUP_CLICKED:
		make_popup_fields_menu(popup_win, menu);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char *state = NULL;
	char title[100];
	int only_line = 0;
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
				
	gtk_tree_model_get(model, iter, SORTID_NAME, &name, -1);
	
	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Job(s) in partition %s", name);
		break;
	case NODE_PAGE:
		gtk_tree_model_get(model, iter, SORTID_ONLY_LINE,
				   &only_line, -1);
		if(!only_line)
			gtk_tree_model_get(model, iter,
					   SORTID_STATE, &state, -1);
#ifdef HAVE_BG
		if(!state || !strlen(state))
			snprintf(title, 100, 
				 "Base partition(s) in partition %s",
				 name);
		else
			snprintf(title, 100, 
				 "Base partition(s) in partition %s "
				 "that are in '%s' state",
				 name, state);
#else
		if(!state || !strlen(state))
			snprintf(title, 100, "Node(s) in partition %s ",
				 name);
		else
			snprintf(title, 100, 
				 "Node(s) in partition %s that are in "
				 "'%s' state",
				 name, state);
#endif
		break;
	case BLOCK_PAGE: 
		snprintf(title, 100, "Block(s) in partition %s", name);
		break;
	case SUBMIT_PAGE: 
		snprintf(title, 100, "Submit job in partition %s", name);
		break;
	case INFO_PAGE: 
		snprintf(title, 100, "Full info for partition %s", name);
		break;
	default:
		g_print("part got %d\n", id);
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
			popup_win = create_popup_info(id, PART_PAGE, title);
		else
			popup_win = create_popup_info(PART_PAGE, id, title);
	} else {
		g_free(name);
		g_free(state);
		gtk_window_present(GTK_WINDOW(popup_win->popup));
		return;
	}

	switch(id) {
	case JOB_PAGE:
	case BLOCK_PAGE: 
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		//specific_info_job(popup_win);
		break;
	case NODE_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		if(state && strlen(state)) {
			popup_win->spec_info->search_info->search_type =
				SEARCH_NODE_STATE;
			gtk_tree_model_get(
				model, iter, SORTID_STATE_NUM,
				&popup_win->spec_info->search_info->int_data,
				-1);
		} else {
			popup_win->spec_info->search_info->search_type = 
				SEARCH_NODE_NAME;
		}
		g_free(state);
		
		//specific_info_node(popup_win);
		break;
	case SUBMIT_PAGE: 
		break;
	default:
		g_print("part got unknown type %d\n", id);
	}
	if (!g_thread_create((gpointer)popup_thr, popup_win, FALSE, &error))
	{
		g_printerr ("Failed to create part popup thread: %s\n", 
			    error->message);
		return;
	}		
}

extern void admin_part(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	update_part_msg_t *part_msg = xmalloc(sizeof(update_part_msg_t));
	char *partid = NULL;
	char *nodelist = NULL;
	char *state = NULL;
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

	gtk_tree_model_get(model, iter, SORTID_NAME, &partid, -1);
	gtk_tree_model_get(model, iter, SORTID_NODELIST, &nodelist, -1);
	gtk_tree_model_get(model, iter, SORTID_AVAIL, &state, -1);
	slurm_init_part_desc_msg(part_msg);
	
	part_msg->name = xstrdup(partid);
		
	if(!strcasecmp("Change Availablity Up/Down", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_YES, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
		
		if(!strcasecmp("down", type)) 
			temp = "up";
		else
			temp = "down";
		snprintf(tmp_char, sizeof(tmp_char), 
			 "Are you sure you want to set partition %s %s?",
			 partid, temp);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_AVAIL;
	} else if(!strcasecmp("Edit Part", type)) {
		label = gtk_dialog_add_button(GTK_DIALOG(popup),
					      GTK_STOCK_OK, GTK_RESPONSE_OK);
		gtk_window_set_default(GTK_WINDOW(popup), label);
		gtk_dialog_add_button(GTK_DIALOG(popup),
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

		gtk_window_set_default_size(GTK_WINDOW(popup), 200, 400);
		snprintf(tmp_char, sizeof(tmp_char), 
			 "Editing partition %s think before you type",
			 partid);
		label = gtk_label_new(tmp_char);
		edit_type = EDIT_EDIT;
		entry = _admin_full_edit_part(part_msg, model, iter);
	} else if(!strncasecmp("Update", type, 6)) {
		char *old_features = NULL;
		if(got_features_edit_signal) 
			old_features = got_features_edit_signal;
		else 
			gtk_tree_model_get(model, iter, SORTID_FEATURES,
					   &old_features, -1);
		update_features_node(GTK_DIALOG(popup),
				     nodelist, old_features);
		if(got_features_edit_signal) {
			got_features_edit_signal = NULL;
			xfree(old_features);
		} else 
			g_free(old_features);
		goto end_it;
	} else {
		/* something that has to deal with a node state change */
		update_state_node(GTK_DIALOG(popup), nodelist, type);
		goto end_it;
	}

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, FALSE, FALSE, 0);
	if(entry)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
				   entry, TRUE, TRUE, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK) {
		if(slurm_update_partition(part_msg) == SLURM_SUCCESS) {
			temp = g_strdup_printf(
				"Partition %s updated successfully",
				partid);
		} else {
			temp = g_strdup_printf(
				"Problem updating partition %s.",
				partid);
		}
		display_edit_note(temp);
		g_free(temp);
	}
end_it:
		
	g_free(state);
	g_free(partid);
	g_free(nodelist);
	slurm_free_update_part_msg(part_msg);
	gtk_widget_destroy(popup);
	if(got_edit_signal) {
		type = got_edit_signal;
		got_edit_signal = NULL;
		admin_part(model, iter, type);
		xfree(type);
	}			
	if(got_features_edit_signal) {
		type = "Update Features";		
		admin_part(model, iter, type);		
	} 
	return;
}

