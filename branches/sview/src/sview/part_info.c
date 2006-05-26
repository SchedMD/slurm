/*****************************************************************************\
 *  part_info.c - Functions related to partition display 
 *  mode of sview.
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
#include "src/common/node_select.h"
#include "src/api/node_select_info.h"

#define _DEBUG 0
DEF_TIMERS;

typedef struct {
	char *bg_user_name;
	char *bg_block_name;
	char *slurm_part_name;
	char *nodes;
	enum connection_type bg_conn_type;
	enum node_use_type bg_node_use;
	rm_partition_state_t state;
	int letter_num;
	List nodelist;
	int size;
	uint16_t quarter;	
	uint16_t nodecard;	
	int node_cnt;	
	bool printed;

} db2_block_info_t;

enum { 
	SORTID_POS = POS_LOC,
	SORTID_PARTITION, 
	SORTID_AVAIL, 
	SORTID_TIMELIMIT, 
	SORTID_BLOCK,
	SORTID_STATE,
	SORTID_USER,
	SORTID_CONN,
	SORTID_USE,
	SORTID_NODES, 
	SORTID_NODELIST, 
	SORTID_PARTITION_CNT
};


#ifdef HAVE_BG
static List block_list = NULL;
#endif

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
#ifdef HAVE_BG
static int _marknodes(db2_block_info_t *block_ptr, int count);
#endif
static void _print_header_part(GtkTreeView *tree_view);
static char *_part_state_str(rm_partition_state_t state);
static int _append_part_record(partition_info_t *part_ptr, 
			       db2_block_info_t *db2_info_ptr,
			       GtkListStore *liststore, GtkTreeIter *iter, 
			       int line);
#ifdef HAVE_BG
static void _block_list_del(void *object);
static void _nodelist_del(void *object);
static int _list_match_all(void *object, void *key);
static int _in_slurm_partition(List slurm_nodes, List bg_nodes);
static int _print_rest(db2_block_info_t *block_ptr);
static int _make_nodelist(char *nodes, List nodelist);
#endif

static void _row_clicked(GtkTreeView *tree_view,
			 GtkTreePath *path,
			 GtkTreeViewColumn *column,
			 gpointer user_data)
{
	partition_info_msg_t *new_part_ptr = (partition_info_msg_t *)user_data;
	partition_info_t *part_ptr = NULL;
	int line = get_row_number(tree_view, path);
	char temp_char[50];
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;

	if(line == -1) {
		g_error("problem getting line number");
		return;
	}
	
	part_ptr = &new_part_ptr->partition_array[line];
	sprintf(temp_char, "the name is %s", part_ptr->name);
	
	popup = gtk_dialog_new();

	label = gtk_label_new(temp_char);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, TRUE, TRUE, 0);	
	gtk_widget_show(label);
	
	gtk_widget_show(popup);
	
}

/* static void _button_pressed(GtkTreeView *tree_view, */
/* 			    GtkTreePath *path, */
/* 			    GtkTreeViewColumn *column, */
/* 			    gpointer user_data) */
/* { */
/* 	g_print("hey a button was clicked\n"); */
/* } */

void _button_pressed(GtkTreeView *tree_view, GdkEventButton *event, 
		     gpointer user_data)
{
	GtkTreePath *path = NULL;
	GtkTreeSelection *selection = NULL;

        if(!gtk_tree_view_get_path_at_pos(tree_view,
					  (gint) event->x, 
					  (gint) event->y,
					  &path, NULL, NULL, NULL)) {
		g_error("problems getting path from treeview\n");
		return;
	}
	selection = gtk_tree_view_get_selection(tree_view);
	gtk_tree_selection_unselect_all(selection);
	gtk_tree_selection_select_path(selection, path);
             	
	/* single click with the right mouse button? */
	if(event->button == 3) {
		g_print ("Single right click on the tree view.\n");
		//view_popup_menu(treeview, event, userdata);
	} else if(event->type==GDK_2BUTTON_PRESS ||
		  event->type==GDK_3BUTTON_PRESS) {
		_row_clicked(tree_view, path, NULL, user_data);
	}
	gtk_tree_path_free(path);
}

static GtkListStore *_create_part_liststore()
{
	GtkListStore *liststore = NULL;
	GType types[SORTID_PARTITION_CNT];
	int i=0;
	/* for the position 'unseen' var */
	types[i] = G_TYPE_INT;
	for(i=1; i<SORTID_PARTITION_CNT; i++)
		types[i] = G_TYPE_STRING;
	liststore = gtk_list_store_newv(SORTID_PARTITION_CNT, 
					types);
	if(!liststore)
		return NULL;

	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore), 
					SORTID_PARTITION, 
					sort_iter_compare_func,
					GINT_TO_POINTER(SORTID_PARTITION), 
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore), 
					SORTID_AVAIL, 
					sort_iter_compare_func,
					GINT_TO_POINTER(SORTID_AVAIL), 
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore), 
					SORTID_TIMELIMIT, 
					sort_iter_compare_func,
					GINT_TO_POINTER(SORTID_TIMELIMIT), 
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore), 
					SORTID_NODES, 
					sort_iter_compare_func,
					GINT_TO_POINTER(SORTID_NODES), 
					NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(liststore), 
					SORTID_NODELIST, 
					sort_iter_compare_func,
					GINT_TO_POINTER(SORTID_NODELIST), 
					NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(liststore), 
					     SORTID_PARTITION, 
					     GTK_SORT_ASCENDING);
	return liststore;
}

extern void get_slurm_part(GtkTable *table)
{
	int error_code, i, j, recs, count = 0;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	partition_info_t part;
	char error_char[50];
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
			return;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
	}
	
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (error_code != SLURM_SUCCESS) {
		sprintf(error_char, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
		return;
	}
	
	
	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;
	
	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(_row_clicked),
			 new_part_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_button_pressed),
			 new_part_ptr);

	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = _create_part_liststore();

	_print_header_part(tree_view);
	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		
				
		while (part.node_inx[j] >= 0) {
			/* set_grid(part.node_inx[j], */
/* 				 part.node_inx[j + 1], count); */
			j += 2;
		}
		_append_part_record(&part, NULL, liststore, &iter, i);
		count++;
			
	}
	if(count==128)
		count=0;
	if (params.commandline && params.iterate)
		printf("\n");
	gtk_tree_view_set_model(tree_view, GTK_TREE_MODEL(liststore));
	g_object_unref(GTK_TREE_MODEL(liststore));
	part_info_ptr = new_part_ptr;
	return;
}

extern void get_bg_part(GtkTable *table)
{
#ifdef HAVE_BG
	int error_code, i, j, recs=0, count = 0, last_count = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	static node_select_info_msg_t *bg_info_ptr = NULL;
	static node_select_info_msg_t *new_bg_ptr = NULL;

	partition_info_t part;
	db2_block_info_t *block_ptr = NULL;
	db2_block_info_t *found_block = NULL;
	ListIterator itr;
	List nodelist = NULL;

	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
			return;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
	}

	if (error_code) {
		printf("slurm_load_partitions: %s",
		       slurm_strerror(slurm_get_errno()));
		return;
	}
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						   &new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
						    &new_bg_ptr);
	}
	if (error_code) {
		printf("slurm_load_partitions: %s",
		       slurm_strerror(slurm_get_errno()));
		return;
	}
	if (block_list) {
		/* clear the old list */
		list_delete_all(block_list, _list_match_all, NULL);
	} else {
		block_list = list_create(_block_list_del);
		if (!block_list) {
			fprintf(stderr, "malloc error\n");
			return;
		}
	}
	if (!params.commandline)
		if((new_bg_ptr->record_count - text_line_cnt) 
		   < (ba_system_ptr->text_win->_maxy-3))
			text_line_cnt--;
	
	for (i=0; i<new_bg_ptr->record_count; i++) {
		block_ptr = xmalloc(sizeof(db2_block_info_t));
			
		block_ptr->bg_block_name 
			= xstrdup(new_bg_ptr->bg_info_array[i].bg_block_id);
		block_ptr->nodes 
			= xstrdup(new_bg_ptr->bg_info_array[i].nodes);
		block_ptr->nodelist = list_create(_nodelist_del);
		_make_nodelist(block_ptr->nodes,block_ptr->nodelist);
		
		block_ptr->bg_user_name 
			= xstrdup(new_bg_ptr->bg_info_array[i].owner_name);
		block_ptr->state 
			= new_bg_ptr->bg_info_array[i].state;
		block_ptr->bg_conn_type 
			= new_bg_ptr->bg_info_array[i].conn_type;
		block_ptr->bg_node_use 
			= new_bg_ptr->bg_info_array[i].node_use;
		block_ptr->quarter 
			= new_bg_ptr->bg_info_array[i].quarter;
		block_ptr->nodecard 
			= new_bg_ptr->bg_info_array[i].nodecard;
		block_ptr->node_cnt 
			= new_bg_ptr->bg_info_array[i].node_cnt;
	       
		itr = list_iterator_create(block_list);
		while ((found_block = (db2_block_info_t*)list_next(itr)) 
		       != NULL) {
			if(!strcmp(block_ptr->nodes, found_block->nodes)) {
				block_ptr->letter_num = 
					found_block->letter_num;
				break;
			}
		}
		list_iterator_destroy(itr);

		if(!found_block) {
			last_count++;
			_marknodes(block_ptr, last_count);
		}
		
		if(block_ptr->bg_conn_type == SELECT_SMALL)
			block_ptr->size = 0;

		list_append(block_list, block_ptr);
	}
	
	if (!params.no_header)
		_print_header_part();

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;

	for (i = 0; i < recs; i++) {
		j = 0;
		part = new_part_ptr->partition_array[i];
		
		if (!part.nodes || (part.nodes[0] == '\0'))
			continue;	/* empty partition */
		nodelist = list_create(_nodelist_del);
		_make_nodelist(part.nodes,nodelist);	
		
		if (block_list) {
			itr = list_iterator_create(block_list);
			while ((block_ptr = (db2_block_info_t*) 
				list_next(itr)) != NULL) {
				if(_in_slurm_partition(nodelist,
						       block_ptr->nodelist)) {
					block_ptr->slurm_part_name 
						= xstrdup(part.name);
				}
			}
			list_iterator_destroy(itr);
		}
		list_destroy(nodelist);
	}

	/* Report the BG Blocks */
	if (block_list) {
		itr = list_iterator_create(block_list);
		while ((block_ptr = (db2_block_info_t*) 
			list_next(itr)) != NULL) {
			if (params.commandline)
				block_ptr->printed = 1;
			else {
				if(count>=text_line_cnt)
					block_ptr->printed = 1;
			}
			_print_rest(block_ptr);
			count++;			
		}
		
		list_iterator_destroy(itr);
	}

	part_info_ptr = new_part_ptr;
	bg_info_ptr = new_bg_ptr;
#endif /* HAVE_BG */
	return;
}

#ifdef HAVE_BG
static int _marknodes(db2_block_info_t *block_ptr, int count)
{
	int j=0;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];
	int number = 0;
	
	block_ptr->letter_num = count;
	while (block_ptr->nodes[j] != '\0') {
		if ((block_ptr->nodes[j] == '['
		     || block_ptr->nodes[j] == ',')
		    && (block_ptr->nodes[j+8] == ']' 
			|| block_ptr->nodes[j+8] == ',')
		    && (block_ptr->nodes[j+4] == 'x'
			|| block_ptr->nodes[j+4] == '-')) {
			j++;
			number = atoi(block_ptr->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(block_ptr->nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 3;
			
			if(block_ptr->state != RM_PARTITION_FREE) 
				block_ptr->size += set_grid_bg(start,
							       end,
							       count,
							       1);
			else
				block_ptr->size += set_grid_bg(start, 
							       end, 
							       count, 
							       0);
			if(block_ptr->nodes[j] != ',')
				break;
			j--;
		} else if((block_ptr->nodes[j] < 58 
			   && block_ptr->nodes[j] > 47)) {
					
			number = atoi(block_ptr->nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			block_ptr->size += set_grid_bg(start, 
							start, 
							count, 
							0);
			if(block_ptr->nodes[j] != ',')
				break;
		}
		j++;
	}
	return SLURM_SUCCESS;
}
#endif

static void _print_header_part(GtkTreeView *tree_view)
{
	
	add_col_to_treeview(tree_view, SORTID_PARTITION, "PARTITION");

	if (params.display != BGPART) {
		add_col_to_treeview(tree_view, SORTID_AVAIL, "AVAIL");
		add_col_to_treeview(tree_view, SORTID_TIMELIMIT, "TIMELIMIT");
	} else {
		add_col_to_treeview(tree_view, SORTID_BLOCK, "BG_BLOCK");
		add_col_to_treeview(tree_view, SORTID_STATE, "STATE");
		add_col_to_treeview(tree_view, SORTID_USER, "USER");
		add_col_to_treeview(tree_view, SORTID_CONN, "CONN TYPE");
		add_col_to_treeview(tree_view, SORTID_USE, "NODE USE");
	}

	add_col_to_treeview(tree_view, SORTID_NODES, "NODES");
	
#ifdef HAVE_BG
	add_col_to_treeview(tree_view, SORTID_NODELIST, "BP_LIST");
#else
	add_col_to_treeview(tree_view, SORTID_NODELIST, "NODELIST");	
#endif
		
}

static char *_part_state_str(rm_partition_state_t state)
{
	static char tmp[16];

#ifdef HAVE_BG
	switch (state) {
		case RM_PARTITION_BUSY: 
			return "BUSY";
		case RM_PARTITION_CONFIGURING:
			return "CONFIG";
		case RM_PARTITION_DEALLOCATING:
			return "DEALLOC";
		case RM_PARTITION_ERROR:
			return "ERROR";
		case RM_PARTITION_FREE:
			return "FREE";
		case RM_PARTITION_NAV:
			return "NAV";
		case RM_PARTITION_READY:
			return "READY";
	}
#endif

	snprintf(tmp, sizeof(tmp), "%d", state);
	return tmp;
}

static int _append_part_record(partition_info_t *part_ptr, 
			       db2_block_info_t *db2_info_ptr,
			       GtkListStore *liststore, GtkTreeIter *iter,
			       int line)
{
	int printed = 0;
	int tempxcord;
	int width = 0;
	char *nodes = NULL, time_buf[20];
	char tmp_cnt[7];
	char tmp_nodes[30];
	char *temp[SORTID_PARTITION_CNT];
	convert_to_kilo(part_ptr->total_nodes, tmp_cnt);
	gtk_list_store_append(liststore, iter);
		
	if (part_ptr->name) {
		temp[SORTID_PARTITION] = part_ptr->name;
		if (params.display != BGPART) {
			if (part_ptr->state_up)
				temp[SORTID_AVAIL] = "up";
			else
				temp[SORTID_AVAIL] = "down";
								
			if (part_ptr->max_time == INFINITE)
				snprintf(time_buf, sizeof(time_buf),
					 "infinite");
			else {
				snprint_time(time_buf,
					     sizeof(time_buf),
					     (part_ptr->max_time
					      * 60));
			}
			
			width = strlen(time_buf);
			temp[SORTID_TIMELIMIT] = time_buf;		
		}
	}

	if (params.display == BGPART) {
		if (db2_info_ptr) {
			temp[SORTID_BLOCK] = db2_info_ptr->bg_block_name;
			temp[SORTID_STATE] = _part_state_str(
				db2_info_ptr->state);
				
			temp[SORTID_USER] = db2_info_ptr->bg_user_name;
			temp[SORTID_CONN] = _convert_conn_type(
				db2_info_ptr->bg_conn_type);
			temp[SORTID_USE] = _convert_node_use(
				db2_info_ptr->bg_node_use);
		}
	}
		
       	temp[SORTID_NODES] = tmp_cnt;
		
	tempxcord = ba_system_ptr->xcord;
		
	if (params.display == BGPART)
		nodes = part_ptr->allow_groups;
	else
		nodes = part_ptr->nodes;
		
	if((params.display == BGPART) && db2_info_ptr
	   && (db2_info_ptr->quarter != (uint16_t) NO_VAL)) {
		if(db2_info_ptr->nodecard != (uint16_t) NO_VAL)
			sprintf(tmp_nodes, "%s.%d.%d", nodes,
				db2_info_ptr->quarter,
				db2_info_ptr->nodecard);
		else
			sprintf(tmp_nodes, "%s.%d", nodes,
				db2_info_ptr->quarter);
		temp[SORTID_NODELIST] = tmp_nodes;
	} else {
		temp[SORTID_NODELIST] = nodes;
	}
	
	if(params.display == BGPART)
		gtk_list_store_set(liststore, iter,
				   SORTID_POS, line,
				   SORTID_PARTITION, temp[SORTID_PARTITION],
				   SORTID_AVAIL, temp[SORTID_AVAIL],
				   SORTID_TIMELIMIT, temp[SORTID_TIMELIMIT],
				   SORTID_BLOCK, temp[SORTID_BLOCK],
				   SORTID_STATE, temp[SORTID_STATE],
				   SORTID_USER, temp[SORTID_USER],
				   SORTID_CONN, temp[SORTID_CONN],
				   SORTID_USE, temp[SORTID_USE],
				   SORTID_NODES, temp[SORTID_NODES],
				   SORTID_NODELIST, temp[SORTID_NODELIST],
				   -1);		
	else
		gtk_list_store_set(liststore, iter,
				   SORTID_POS, line,
				   SORTID_PARTITION, temp[SORTID_PARTITION],
				   SORTID_AVAIL, temp[SORTID_AVAIL],
				   SORTID_TIMELIMIT, temp[SORTID_TIMELIMIT],
				   SORTID_NODES, temp[SORTID_NODES],
				   SORTID_NODELIST, temp[SORTID_NODELIST],
				   -1);		
	

	return printed;
}

#ifdef HAVE_BG
static void _block_list_del(void *object)
{
	db2_block_info_t *block_ptr = (db2_block_info_t *)object;

	if (block_ptr) {
		xfree(block_ptr->bg_user_name);
		xfree(block_ptr->bg_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->nodes);
		if(block_ptr->nodelist)
			list_destroy(block_ptr->nodelist);
		
		xfree(block_ptr);
		
	}
}

static void _nodelist_del(void *object)
{
	int *coord = (int *)object;
	xfree(coord);
	return;
}

static int _list_match_all(void *object, void *key)
{
	return 1;
}

static int _in_slurm_partition(List slurm_nodes, List bg_nodes)
{
	ListIterator slurm_itr;
	ListIterator bg_itr;
	int *coord = NULL;
	int *slurm_coord = NULL;
	int found = 0;
	
	bg_itr = list_iterator_create(bg_nodes);
	slurm_itr = list_iterator_create(slurm_nodes);
	while ((coord = list_next(bg_itr)) != NULL) {
		list_iterator_reset(slurm_itr);
		found = 0;
		while ((slurm_coord = list_next(slurm_itr)) != NULL) {
			if((coord[X] == slurm_coord[X])
			   && (coord[Y] == slurm_coord[Y])
			   && (coord[Z] == slurm_coord[Z])) {
				found=1;
				break;
			}			
		}
		if(!found) {
			break;
		}
	}
	list_iterator_destroy(slurm_itr);
	list_iterator_destroy(bg_itr);
			
	if(found)
		return 1;
	else
		return 0;
	
}

static int _print_rest(db2_block_info_t *block_ptr)
{
	partition_info_t part;
			
	if(block_ptr->node_cnt == 0)
		block_ptr->node_cnt = block_ptr->size;
	part.total_nodes = block_ptr->node_cnt;
	if(block_ptr->slurm_part_name)
		part.name = block_ptr->slurm_part_name;
	else
		part.name = "no part";

	if (!block_ptr->printed)
		return SLURM_SUCCESS;
	part.allow_groups = block_ptr->nodes;
	part.root_only = (int) letters[block_ptr->letter_num%62];
	_print_text_part(&part, block_ptr);
	
	return SLURM_SUCCESS;
}

static int _addto_nodelist(List nodelist, int *start, int *end)
{
	int *coord = NULL;
	int x,y,z;
	
	assert(end[X] < DIM_SIZE[X]);
	assert(start[X] >= 0);
	assert(end[Y] < DIM_SIZE[Y]);
	assert(start[Y] >= 0);
	assert(end[Z] < DIM_SIZE[Z]);
	assert(start[Z] >= 0);
	
	for (x = start[X]; x <= end[X]; x++) {
		for (y = start[Y]; y <= end[Y]; y++) {
			for (z = start[Z]; z <= end[Z]; z++) {
				coord = xmalloc(sizeof(int)*3);
				coord[X] = x;
				coord[Y] = y;
				coord[Z] = z;
				list_append(nodelist, coord);
			}
		}
	}
	return 1;
}

static int _make_nodelist(char *nodes, List nodelist)
{
	int j = 0;
	int number;
	int start[BA_SYSTEM_DIMENSIONS];
	int end[BA_SYSTEM_DIMENSIONS];
	
	if(!nodelist)
		nodelist = list_create(_nodelist_del);
	while (nodes[j] != '\0') {
		if ((nodes[j] == '['
		     || nodes[j] == ',')
		    && (nodes[j+8] == ']' 
			|| nodes[j+8] == ',')
		    && (nodes[j+4] == 'x'
			|| nodes[j+4] == '-')) {
			j++;
			number = atoi(nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j += 4;
			number = atoi(nodes + j);
			end[X] = number / 100;
			end[Y] = (number % 100) / 10;
			end[Z] = (number % 10);
			j += 3;
			_addto_nodelist(nodelist, start, end);
			if(nodes[j] != ',')
				break;
			j--;
		} else if((nodes[j] < 58 
			   && nodes[j] > 47)) {
					
			number = atoi(nodes + j);
			start[X] = number / 100;
			start[Y] = (number % 100) / 10;
			start[Z] = (number % 10);
			j+=3;
			_addto_nodelist(nodelist, start, start);
			if(nodes[j] != ',')
				break;
		}
		j++;
	}
	return 1;
}

#endif

static char* _convert_conn_type(enum connection_type conn_type)
{
	switch (conn_type) {
	case (SELECT_MESH):
		return "MESH";
	case (SELECT_TORUS):
		return "TORUS";
	case (SELECT_SMALL):
		return "SMALL";
	case (SELECT_NAV):
		return "NAV";
	}
	return "?";
}

static char* _convert_node_use(enum node_use_type node_use)
{
	switch (node_use) {
	case (SELECT_COPROCESSOR_MODE):
		return "COPROCESSOR";
	case (SELECT_VIRTUAL_NODE_MODE):
		return "VIRTUAL";
	case (SELECT_NAV_MODE):
		return "NAV";
	}
	return "?";
}

