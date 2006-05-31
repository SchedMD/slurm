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
	SORTID_BLOCK,
	SORTID_STATE,
	SORTID_USER,
	SORTID_CONN,
	SORTID_USE,
	SORTID_NODES, 
	SORTID_NODELIST, 
	SORTID_PARTITION_CNT
};

static display_data_t display_data_block[] = {
	{SORTID_POS, NULL, FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_PARTITION, "PARTITION", TRUE, -1, NULL, NULL, NULL, NULL},
	{SORTID_BLOCK, "BG_BLOCK", FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_STATE, "STATE", FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_USER, "USER", FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_CONN, "CONN TYPE", FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_USE, "NODE USE", FALSE, -1, NULL, NULL, NULL, NULL},
	{SORTID_NODES, "NODES", TRUE, -1, NULL, NULL, NULL, NULL},
#ifdef HAVE_BG
	{SORTID_NODELIST, "BP_LIST", TRUE, -1, NULL, NULL, NULL, NULL},
#else
	{SORTID_NODELIST, "NODELIST", TRUE, -1, NULL, NULL, NULL, NULL},
#endif
	{-1, NULL, FALSE, -1, NULL, NULL, NULL, NULL}};
static display_data_t *local_display_data = NULL;

static List block_list = NULL;

static char* _convert_conn_type(enum connection_type conn_type);
static char* _convert_node_use(enum node_use_type node_use);
static int _marknodes(db2_block_info_t *block_ptr, int count);
static char *_part_state_str(rm_partition_state_t state);
static int _append_block_record(db2_block_info_t *block_ptr,
				GtkListStore *liststore, GtkTreeIter *iter, 
				int line);
static void _block_list_del(void *object);
static void _nodelist_del(void *object);
static int _list_match_all(void *object, void *key);
static int _in_slurm_partition(List slurm_nodes, List bg_nodes);
static int _make_nodelist(char *nodes, List nodelist);

static void _set_up_button(GtkTreeView *tree_view, GdkEventButton *event, 
			    gpointer user_data)
{
	local_display_data->user_data = user_data;
	button_pressed(tree_view, event, local_display_data);
}

extern void get_info_block(GtkTable *table, display_data_t *display_data)
{
	int error_code = SLURM_SUCCESS;
	int i, j, recs=0, count = 0, last_count = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static partition_info_msg_t *new_part_ptr = NULL;
	static node_select_info_msg_t *bg_info_ptr = NULL;
	static node_select_info_msg_t *new_bg_ptr = NULL;

	partition_info_t part;
	db2_block_info_t *block_ptr = NULL;
	db2_block_info_t *found_block = NULL;
	char error_char[50];
	ListIterator itr;
	List nodelist = NULL;
	GtkTreeIter iter;
	GtkListStore *liststore = NULL;
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	
	local_display_data = display_data;
	if(new_part_ptr && new_bg_ptr && toggled)
		goto got_toggled;
	if (part_info_ptr) {
		error_code = slurm_load_partitions(part_info_ptr->last_update, 
						   &new_part_ptr, SHOW_ALL);
		if (error_code == SLURM_SUCCESS)
			slurm_free_partition_info_msg(part_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_part_ptr = part_info_ptr;
		}
	} else {
		error_code = slurm_load_partitions((time_t) NULL, 
						   &new_part_ptr, SHOW_ALL);
	}
	
	if (error_code != SLURM_SUCCESS) {
		if(display_widget)
			gtk_widget_destroy(display_widget);
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
	if (bg_info_ptr) {
		error_code = slurm_load_node_select(bg_info_ptr->last_update, 
						   &new_bg_ptr);
		if (error_code == SLURM_SUCCESS)
			select_g_free_node_info(&bg_info_ptr);
		else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_SUCCESS;
			new_bg_ptr = bg_info_ptr;
			return;
		}
	} else {
		error_code = slurm_load_node_select((time_t) NULL, 
						    &new_bg_ptr);
	}

	if (error_code != SLURM_SUCCESS) {
		if(display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_node_select: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table), 
					  label,
					  0, 1, 0, 1); 
		gtk_widget_show(label);	
		display_widget = gtk_widget_ref(GTK_WIDGET(label));
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

got_toggled:
	if(display_widget)
		gtk_widget_destroy(display_widget);

	if (new_part_ptr)
		recs = new_part_ptr->record_count;
	else
		recs = 0;

	tree_view = GTK_TREE_VIEW(gtk_tree_view_new());
	display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));

	g_signal_connect(G_OBJECT(tree_view), "row-activated",
			 G_CALLBACK(row_clicked_block),
			 new_part_ptr);
	g_signal_connect(G_OBJECT(tree_view), "button-press-event",
			 G_CALLBACK(_set_up_button),
			 new_part_ptr);
	
	gtk_table_attach_defaults(GTK_TABLE(table), 
				  GTK_WIDGET(tree_view),
				  0, 1, 0, 1); 
	gtk_widget_show(GTK_WIDGET(tree_view));
	
	liststore = create_liststore(display_data, SORTID_PARTITION_CNT);
	
	load_header(tree_view, display_data);

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
			
			if(block_ptr->node_cnt == 0)
				block_ptr->node_cnt = block_ptr->size;
			part.total_nodes = block_ptr->node_cnt;
			if(!block_ptr->slurm_part_name)
				block_ptr->slurm_part_name = "no part";
			/* this is the letter for later 
			   part.root_only = 
			   (int) letters[block_ptr->letter_num%62];
			*/
			_append_block_record(block_ptr, liststore, 
					     &iter, count);
			count++;			
		}
		
		list_iterator_destroy(itr);
	}

	part_info_ptr = new_part_ptr;
	bg_info_ptr = new_bg_ptr;
	return;
}

extern void set_fields_block(GtkMenu *menu)
{
	make_fields_menu(menu, display_data_block);
}

extern void row_clicked_block(GtkTreeView *tree_view,
			      GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      gpointer user_data)
{
	partition_info_msg_t *new_part_ptr = (partition_info_msg_t *)user_data;
	partition_info_t *part_ptr = NULL;
	int line = get_row_number(tree_view, path);
	GtkWidget *popup = NULL;
	GtkWidget *label = NULL;
	char *info = NULL;
	if(line == -1) {
		g_error("problem getting line number");
		return;
	}
	
	part_ptr = &new_part_ptr->partition_array[line];
	if(!(info = slurm_sprint_partition_info(part_ptr, 0))) {
		info = xmalloc(100);
		sprintf(info, "Problem getting partition info for %s", 
			part_ptr->name);
	} 

	popup = gtk_dialog_new();

	label = gtk_label_new(info);
	gtk_box_pack_end(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   label, TRUE, TRUE, 0);
	xfree(info);
	gtk_widget_show(label);
	
	gtk_widget_show(popup);
	
}

static int _marknodes(db2_block_info_t *block_ptr, int count)
{
#ifdef HAVE_BG
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
#endif
	return SLURM_SUCCESS;
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

static int _append_block_record(db2_block_info_t *block_ptr,
				GtkListStore *liststore, GtkTreeIter *iter,
				int line)
{
	int printed = 0;
	int tempxcord;
	char *nodes = NULL;
	char tmp_cnt[7];
	char tmp_nodes[30];
	char *temp[SORTID_PARTITION_CNT];
	convert_to_kilo(block_ptr->node_cnt, tmp_cnt);
	gtk_list_store_append(liststore, iter);
		
	temp[SORTID_PARTITION] = block_ptr->slurm_part_name;
	
	if (block_ptr) {
		temp[SORTID_BLOCK] = block_ptr->bg_block_name;
		temp[SORTID_STATE] = _part_state_str(
			block_ptr->state);
		
		temp[SORTID_USER] = block_ptr->bg_user_name;
		temp[SORTID_CONN] = _convert_conn_type(
			block_ptr->bg_conn_type);
		temp[SORTID_USE] = _convert_node_use(
			block_ptr->bg_node_use);
	}
	
		
       	temp[SORTID_NODES] = tmp_cnt;
		
	tempxcord = ba_system_ptr->xcord;
		
	nodes = block_ptr->nodes;
			
	if(block_ptr && (block_ptr->quarter != (uint16_t) NO_VAL)) {
		if(block_ptr->nodecard != (uint16_t) NO_VAL)
			sprintf(tmp_nodes, "%s.%d.%d", nodes,
				block_ptr->quarter,
				block_ptr->nodecard);
		else
			sprintf(tmp_nodes, "%s.%d", nodes,
				block_ptr->quarter);
		temp[SORTID_NODELIST] = tmp_nodes;
	} else {
		temp[SORTID_NODELIST] = nodes;
	}
	
	gtk_list_store_set(liststore, iter,
			   SORTID_POS, line,
			   SORTID_PARTITION, temp[SORTID_PARTITION],
			   SORTID_BLOCK, temp[SORTID_BLOCK],
			   SORTID_STATE, temp[SORTID_STATE],
			   SORTID_USER, temp[SORTID_USER],
			   SORTID_CONN, temp[SORTID_CONN],
			   SORTID_USE, temp[SORTID_USE],
			   SORTID_NODES, temp[SORTID_NODES],
			   SORTID_NODELIST, temp[SORTID_NODELIST],
			   -1);		
	
	return printed;
}

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

