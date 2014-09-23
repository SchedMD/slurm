/*****************************************************************************\
 *  block_info.c - Functions related to Bluegene block display
 *  mode of sview.
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
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
#include "src/common/node_select.h"

#define _DEBUG 0

typedef struct {
	char *bg_block_name;
	char *slurm_part_name;
	char *mp_str;
	uint16_t bg_conn_type[HIGHEST_DIMENSIONS];
	uint16_t bg_node_use;
	uint16_t state;
	int size;
	int cnode_cnt;
	int cnode_err_cnt;
	GtkTreeIter iter_ptr;
	bool iter_set;
	int *mp_inx;            /* list index pairs into node_table for *mp_str:
				 * start_range_1, end_range_1,
				 * start_range_2, .., -1  */
	int color_inx;
	List job_list;
	int pos;
	bool printed;
	char *reason;
	bool small_block;
	char *imageblrts;       /* ImageBlrts for this block */
	char *imagelinux;       /* ImageLinux for this block */
	char *imagemloader;     /* imagemloader for this block */
	char *imageramdisk;     /* ImageRamDisk for this block */
} sview_block_info_t;

enum {
	SORTID_POS = POS_LOC,
	SORTID_BLOCK,
	SORTID_COLOR,
	SORTID_COLOR_INX,
	SORTID_CONN,
	SORTID_JOB,
	SORTID_IMAGEBLRTS,
#ifdef HAVE_BGL
	SORTID_IMAGELINUX,
	SORTID_IMAGEMLOADER,
	SORTID_IMAGERAMDISK,
#else
	SORTID_IMAGELINUX,
	SORTID_IMAGERAMDISK,
	SORTID_IMAGEMLOADER,
#endif
	SORTID_NODELIST,
	SORTID_NODE_CNT,
	SORTID_PARTITION,
	SORTID_REASON,
	SORTID_STATE,
	SORTID_UPDATED,
	SORTID_USE,
	SORTID_NODE_INX,
	SORTID_SMALL_BLOCK,
	SORTID_USER,
	SORTID_CNT
};

/*these are the settings to apply for the user
 * on the first startup after a fresh slurm install.*/
static char *_initial_page_opts = "Block_ID,State,JobID,User,Node_Count,"
	"Node_Use,MidplaneList,Partition";

static display_data_t display_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_BLOCK, "Block ID",
	 FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_COLOR, NULL, TRUE, EDIT_COLOR,
	 refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_STATE, "State", FALSE, EDIT_MODEL, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_JOB, "JobID", FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
#ifdef HAVE_BG_L_P
	{G_TYPE_STRING, SORTID_USER, "User", FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
#else
	{G_TYPE_STRING, SORTID_USER, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
#endif
	{G_TYPE_STRING, SORTID_NODE_CNT, "Node Count",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_CONN, "Connection Type",
	 FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_NODELIST, "MidplaneList", FALSE,
	 EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_PARTITION, "Partition",
	 FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
#ifdef HAVE_BGL
	{G_TYPE_STRING, SORTID_USE, "Node Use", FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGEBLRTS, "Image Blrts",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGELINUX, "Image Linux",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGERAMDISK, "Image Ramdisk",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
#elif defined HAVE_BGP
	{G_TYPE_STRING, SORTID_USE, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGEBLRTS, NULL,
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGELINUX, "Image Cnload",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGERAMDISK, "Image Ioload",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
#else
	{G_TYPE_STRING, SORTID_USE, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGEBLRTS, NULL,
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGELINUX, NULL,
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_IMAGERAMDISK, NULL,
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
#endif
	{G_TYPE_STRING, SORTID_IMAGEMLOADER, "Image Mloader",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_STRING, SORTID_REASON, "Reason",
	 FALSE, EDIT_NONE, refresh_block, create_model_block, admin_edit_block},
	{G_TYPE_POINTER, SORTID_NODE_INX, NULL, FALSE, EDIT_NONE,
	 refresh_block, create_model_resv, admin_edit_resv},
	{G_TYPE_INT, SORTID_COLOR_INX, NULL, FALSE, EDIT_NONE,
	 refresh_block, create_model_resv, admin_edit_resv},
	{G_TYPE_INT, SORTID_SMALL_BLOCK, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_INT, SORTID_UPDATED, NULL, FALSE, EDIT_NONE, refresh_block,
	 create_model_block, admin_edit_block},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t options_data_block[] = {
	{G_TYPE_INT, SORTID_POS, NULL, FALSE, EDIT_NONE},
	{G_TYPE_STRING, INFO_PAGE, "Full Info", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Put block in error state",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Put block in free state",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Recreate block",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Remove block",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, BLOCK_PAGE, "Resume block",
	 TRUE, ADMIN_PAGE},
	{G_TYPE_STRING, JOB_PAGE, "Jobs", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, PART_PAGE, "Partitions", TRUE, BLOCK_PAGE},
	{G_TYPE_STRING, NODE_PAGE, "Midplanes", TRUE, BLOCK_PAGE},
	//{G_TYPE_STRING, SUBMIT_PAGE, "Job Submit", FALSE, BLOCK_PAGE},
	{G_TYPE_STRING, RESV_PAGE, "Reservations", TRUE, BLOCK_PAGE},
	{G_TYPE_NONE, -1, NULL, FALSE, EDIT_NONE}
};

static display_data_t *local_display_data = NULL;
static GtkTreeModel *last_model = NULL;

static void _admin_block(GtkTreeModel *model, GtkTreeIter *iter, char *type);
static void _append_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore);
static int _in_slurm_partition(int *part_inx, int *block_inx);
static void _process_each_block(GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter*iter, gpointer userdata);

static char *_set_running_job_str(List job_list, bool compact)
{
	int cnt = list_count(job_list);
	block_job_info_t *block_job;

	if (!cnt) {
		return xstrdup("-");
	} else if (cnt == 1) {
		block_job = list_peek(job_list);
		return xstrdup_printf("%u", block_job->job_id);
	} else if (compact)
		return xstrdup("multiple");
	else {
		char *tmp_char = NULL;
		ListIterator itr = list_iterator_create(job_list);
		while ((block_job = list_next(itr))) {
			if (tmp_char)
				xstrcat(tmp_char, " ");
			xstrfmtcat(tmp_char, "%u", block_job->job_id);
		}
		return tmp_char;
	}

	return NULL;
}

static void _block_info_free(sview_block_info_t *block_ptr)
{
	if (block_ptr) {
		xfree(block_ptr->bg_block_name);
		xfree(block_ptr->slurm_part_name);
		xfree(block_ptr->mp_str);
		xfree(block_ptr->reason);
		xfree(block_ptr->imageblrts);
		xfree(block_ptr->imagelinux);
		xfree(block_ptr->imagemloader);
		xfree(block_ptr->imageramdisk);

		if (block_ptr->job_list) {
			list_destroy(block_ptr->job_list);
			block_ptr->job_list = NULL;
		}

		/* don't xfree(block_ptr->mp_inx);
		   it isn't copied like the chars and is freed in the api
		*/
	}
}

static void _block_list_del(void *object)
{
	sview_block_info_t *block_ptr = (sview_block_info_t *)object;

	if (block_ptr) {
		_block_info_free(block_ptr);
		xfree(block_ptr);
	}
}

static int _in_slurm_partition(int *part_inx, int *mp_inx)
{
	int found = 0;
	int i=0, j=0;

	while (mp_inx[i] >= 0) {
		j = 0;
		found = 0;
		while (part_inx[j] >= 0) {
			if ((mp_inx[i] >= part_inx[j])
			    && mp_inx[i+1] <= part_inx[j+1]) {
				found = 1;
				break;
			}
			j += 2;
		}
		if (!found)
			return 0;
		i += 2;
	}

	return 1;
}

static void _layout_block_record(GtkTreeView *treeview,
				 sview_block_info_t *block_ptr,
				 int update)
{
	char tmp_cnt[18], tmp_cnt2[18];
	char *tmp_char = NULL;
	GtkTreeIter iter;
	GtkTreeStore *treestore =
		GTK_TREE_STORE(gtk_tree_view_get_model(treeview));

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_NODELIST),
				   block_ptr->mp_str);
	tmp_char = conn_type_string_full(block_ptr->bg_conn_type);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_CONN),
				   tmp_char);
	xfree(tmp_char);

	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGEMLOADER),
					   block_ptr->imagemloader);
	} else if (cluster_flags & CLUSTER_FLAG_BGP) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGELINUX),
					   block_ptr->imagelinux);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGERAMDISK),
					   block_ptr->imageramdisk);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGEMLOADER),
					   block_ptr->imagemloader);
	} else if (cluster_flags & CLUSTER_FLAG_BGL) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGEBLRTS),
					   block_ptr->imageblrts);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGELINUX),
					   block_ptr->imagelinux);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGEMLOADER),
					   block_ptr->imagemloader);
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_IMAGERAMDISK),
					   block_ptr->imageramdisk);
	}

	tmp_char = _set_running_job_str(block_ptr->job_list, 0);

	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_JOB),
				   tmp_char);
	xfree(tmp_char);
	if (cluster_flags & CLUSTER_FLAG_BGL) {
		add_display_treestore_line(update, treestore, &iter,
					   find_col_name(display_data_block,
							 SORTID_USE),
					   node_use_string(
						   block_ptr->bg_node_use));
	}
	convert_num_unit((float)block_ptr->cnode_cnt, tmp_cnt, sizeof(tmp_cnt),
			 UNIT_NONE);
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		convert_num_unit((float)block_ptr->cnode_err_cnt, tmp_cnt2,
				 sizeof(tmp_cnt2), UNIT_NONE);
		tmp_char = xstrdup_printf("%s/%s", tmp_cnt, tmp_cnt2);
	} else
		tmp_char = tmp_cnt;
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_NODE_CNT),
				   tmp_char);
	if (cluster_flags & CLUSTER_FLAG_BGQ)
		xfree(tmp_char);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_PARTITION),
				   block_ptr->slurm_part_name);
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_STATE),
				   bg_block_state_string(block_ptr->state));
	add_display_treestore_line(update, treestore, &iter,
				   find_col_name(display_data_block,
						 SORTID_REASON),
				   block_ptr->reason);
}

static void _update_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore)
{
	char cnode_cnt[20], cnode_cnt2[20];
	char *tmp_char = NULL, *tmp_char2 = NULL, *tmp_char3 = NULL;

	convert_num_unit((float)block_ptr->cnode_cnt, cnode_cnt,
			 sizeof(cnode_cnt), UNIT_NONE);
	if (cluster_flags & CLUSTER_FLAG_BGQ) {
		convert_num_unit((float)block_ptr->cnode_err_cnt, cnode_cnt2,
				 sizeof(cnode_cnt), UNIT_NONE);
		tmp_char3 = xstrdup_printf("%s/%s", cnode_cnt, cnode_cnt2);
	} else
		tmp_char3 = cnode_cnt;

	tmp_char = conn_type_string_full(block_ptr->bg_conn_type);
	tmp_char2 = _set_running_job_str(block_ptr->job_list, 0);
	/* Combining these records provides a slight performance improvement */
	gtk_tree_store_set(treestore, &block_ptr->iter_ptr,
			   SORTID_BLOCK,        block_ptr->bg_block_name,
			   SORTID_COLOR,
				sview_colors[block_ptr->color_inx],
			   SORTID_COLOR_INX,    block_ptr->color_inx,
			   SORTID_CONN,		tmp_char,
			   SORTID_IMAGEMLOADER, block_ptr->imagemloader,
			   SORTID_JOB,          tmp_char2,
			   SORTID_NODE_INX,     block_ptr->mp_inx,
			   SORTID_NODE_CNT,     tmp_char3,
			   SORTID_NODELIST,     block_ptr->mp_str,
			   SORTID_PARTITION,    block_ptr->slurm_part_name,
			   SORTID_REASON,       block_ptr->reason,
			   SORTID_SMALL_BLOCK,  block_ptr->small_block,
			   SORTID_STATE,
				bg_block_state_string(block_ptr->state),
			   SORTID_UPDATED,      1,
			   -1);
	xfree(tmp_char);
	xfree(tmp_char2);
	if (cluster_flags & CLUSTER_FLAG_BGQ)
		xfree(tmp_char3);

	if (cluster_flags & CLUSTER_FLAG_BGP) {
		gtk_tree_store_set(treestore, &block_ptr->iter_ptr,
				   SORTID_IMAGERAMDISK, block_ptr->imageramdisk,
				   SORTID_IMAGELINUX,   block_ptr->imagelinux,
				   -1);
	} else if (cluster_flags & CLUSTER_FLAG_BGL) {
		gtk_tree_store_set(treestore, &block_ptr->iter_ptr,
				   SORTID_IMAGERAMDISK, block_ptr->imageramdisk,
				   SORTID_IMAGELINUX,   block_ptr->imagelinux,
				   SORTID_IMAGEBLRTS,   block_ptr->imageblrts,
				   SORTID_USE,
					node_use_string(block_ptr->bg_node_use),
				   -1);
	}

	return;
}

static void _append_block_record(sview_block_info_t *block_ptr,
				 GtkTreeStore *treestore)
{
	gtk_tree_store_append(treestore, &block_ptr->iter_ptr, NULL);
	gtk_tree_store_set(treestore, &block_ptr->iter_ptr, SORTID_POS,
			   block_ptr->pos, -1);
	_update_block_record(block_ptr, treestore);
}

static void _update_info_block(List block_list,
			       GtkTreeView *tree_view)
{
	ListIterator itr;
	sview_block_info_t *block_ptr = NULL;
	GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
	char *name = NULL;

	if (!block_list) {
		g_print("No block_list given");
		return;
	}

	set_for_update(model, SORTID_UPDATED);

 	/* Report the BG Blocks */

	itr = list_iterator_create(block_list);
	while ((block_ptr = (sview_block_info_t*) list_next(itr))) {
		if (block_ptr->cnode_cnt == 0)
			block_ptr->cnode_cnt = block_ptr->size;
		if (!block_ptr->slurm_part_name)
			block_ptr->slurm_part_name = xstrdup("no part");

		/* This means the tree_store changed (added new column
		   or something). */
		if (last_model != model)
			block_ptr->iter_set = false;

		if (block_ptr->iter_set) {
			gtk_tree_model_get(model, &block_ptr->iter_ptr,
					   SORTID_BLOCK, &name, -1);
			if (strcmp(name, block_ptr->bg_block_name)) {
				/* Bad pointer */
				block_ptr->iter_set = false;
			}
			g_free(name);
		}
		if (block_ptr->iter_set)
			_update_block_record(block_ptr,
					     GTK_TREE_STORE(model));
		else {
			_append_block_record(block_ptr,
					     GTK_TREE_STORE(model));
			block_ptr->iter_set = true;
		}
	}

	list_iterator_destroy(itr);

	/* remove all old blocks */
	remove_old(model, SORTID_UPDATED);
	last_model = model;
}

static int _sview_block_sort_aval_dec(void *s1, void *s2)
{
	sview_block_info_t *rec_a = *(sview_block_info_t **)s1;
	sview_block_info_t *rec_b = *(sview_block_info_t **)s2;
	int size_a;
	int size_b;

	size_a = rec_a->cnode_cnt;
	size_b = rec_b->cnode_cnt;

	if (list_count(rec_a->job_list) < list_count(rec_b->job_list))
		return 1;
	else if (list_count(rec_a->job_list) > list_count(rec_b->job_list))
		return -1;

	if ((rec_a->state == BG_BLOCK_FREE) && (rec_b->state != BG_BLOCK_FREE))
		return 1;
	else if ((rec_a->state != BG_BLOCK_FREE) &&
		 (rec_b->state == BG_BLOCK_FREE))
			return -1;

	if (size_a < size_b)
		return -1;
	else if (size_a > size_b)
		return 1;

	if (rec_a->mp_str && rec_b->mp_str) {
		size_a = strcmp(rec_a->mp_str, rec_b->mp_str);
		if (size_a < 0)
			return -1;
		else if (size_a > 0)
			return 1;
	}
	return 0;
}

static void _set_block_partition(partition_info_msg_t *part_info_ptr,
				 sview_block_info_t *block_ptr)
{
	int j;
	partition_info_t part;

	for (j = 0; j < part_info_ptr->record_count; j++) {
		part = part_info_ptr->partition_array[j];
		if (_in_slurm_partition(part.node_inx,
					block_ptr->mp_inx)) {
			xfree(block_ptr->slurm_part_name);
			block_ptr->slurm_part_name = xstrdup(part.name);
			return;
		}
	}
}

static List _create_block_list(partition_info_msg_t *part_info_ptr,
			       block_info_msg_t *block_info_ptr)
{
	int i;
	static List block_list = NULL;
	static partition_info_msg_t *last_part_info_ptr = NULL;
	static block_info_msg_t *last_block_info_ptr = NULL;
	List last_list = NULL;
	ListIterator last_list_itr = NULL;
	sview_block_info_t *block_ptr = NULL;
	char tmp_mp_str[50];

	if (block_list && (part_info_ptr == last_part_info_ptr)
	    && (block_info_ptr == last_block_info_ptr))
		return block_list;

	last_part_info_ptr = part_info_ptr;
	if (block_list) {
		/* Only the partition info changed so lets update just
		   that part.
		*/
		if (block_info_ptr == last_block_info_ptr) {
			ListIterator itr = list_iterator_create(block_list);

			while ((block_ptr = list_next(itr)))
				_set_block_partition(part_info_ptr, block_ptr);

			return block_list;
		}
		last_list = block_list;
	}

	block_list = list_create(_block_list_del);
	if (!block_list) {
		g_print("malloc error\n");
		return NULL;
	}

	last_block_info_ptr = block_info_ptr;

	if (last_list)
		last_list_itr = list_iterator_create(last_list);
	for (i=0; i<block_info_ptr->record_count; i++) {
		/* If we don't have a block name just continue since
		   ths block hasn't been made in the system yet. */
		if (!block_info_ptr->block_array[i].bg_block_id)
			continue;

		block_ptr = NULL;

		if (last_list_itr) {
			while ((block_ptr = list_next(last_list_itr))) {
				if (!strcmp(block_ptr->bg_block_name,
					    block_info_ptr->
					    block_array[i].bg_block_id)) {
					list_remove(last_list_itr);
					_block_info_free(block_ptr);
					break;
				}
			}
			list_iterator_reset(last_list_itr);
		}

		if (!block_ptr)
			block_ptr = xmalloc(sizeof(sview_block_info_t));
		block_ptr->pos = i;
		block_ptr->bg_block_name
			= xstrdup(block_info_ptr->
				  block_array[i].bg_block_id);


		block_ptr->color_inx =
			atoi(block_ptr->bg_block_name+7);

		/* on some systems they make there own blocks named
		   whatever they want, so doing this fixes what could
		   be a negative number.
		*/
		if (block_ptr->color_inx < 0)
			block_ptr->color_inx = i;

		block_ptr->color_inx %= sview_colors_cnt;

		block_ptr->mp_str
			= xstrdup(block_info_ptr->block_array[i].mp_str);
		if (block_info_ptr->block_array[i].ionode_str) {
			block_ptr->small_block = 1;
			snprintf(tmp_mp_str, sizeof(tmp_mp_str),
				 "%s[%s]",
				 block_ptr->mp_str,
				 block_info_ptr->block_array[i].ionode_str);
			xfree(block_ptr->mp_str);
			block_ptr->mp_str = xstrdup(tmp_mp_str);
		}
		block_ptr->reason
			= xstrdup(block_info_ptr->block_array[i].reason);

		if (cluster_flags & CLUSTER_FLAG_BGP) {
			block_ptr->imagelinux = xstrdup(
				block_info_ptr->block_array[i].linuximage);
			block_ptr->imageramdisk = xstrdup(
				block_info_ptr->block_array[i].ramdiskimage);
		} else if (cluster_flags & CLUSTER_FLAG_BGL) {
			block_ptr->imageblrts = xstrdup(
				block_info_ptr->block_array[i].blrtsimage);
			block_ptr->imagelinux = xstrdup(
				block_info_ptr->block_array[i].linuximage);
			block_ptr->imageramdisk = xstrdup(
				block_info_ptr->block_array[i].ramdiskimage);
		}

		block_ptr->imagemloader = xstrdup(
			block_info_ptr->block_array[i].mloaderimage);

		block_ptr->state
			= block_info_ptr->block_array[i].state;
		memcpy(block_ptr->bg_conn_type,
		       block_info_ptr->block_array[i].conn_type,
		       sizeof(block_ptr->bg_conn_type));

		if (cluster_flags & CLUSTER_FLAG_BGL)
			block_ptr->bg_node_use
				= block_info_ptr->block_array[i].node_use;

		block_ptr->cnode_cnt
			= block_info_ptr->block_array[i].cnode_cnt;
		block_ptr->cnode_err_cnt
			= block_info_ptr->block_array[i].cnode_err_cnt;
		block_ptr->mp_inx
			= block_info_ptr->block_array[i].mp_inx;
		_set_block_partition(part_info_ptr, block_ptr);

		block_ptr->job_list = list_create(slurm_free_block_job_info);
		if (block_info_ptr->block_array[i].job_list)
			list_transfer(block_ptr->job_list,
				      block_info_ptr->block_array[i].job_list);

		if (block_ptr->bg_conn_type[0] >= SELECT_SMALL)
			block_ptr->size = 0;

		list_append(block_list, block_ptr);
	}

	list_sort(block_list,
		  (ListCmpF)_sview_block_sort_aval_dec);

	if (last_list) {
		list_iterator_destroy(last_list_itr);
		list_destroy(last_list);
	}

	return block_list;
}

void _display_info_block(List block_list,
			 popup_info_t *popup_win)
{
	specific_info_t *spec_info = popup_win->spec_info;
	char *name = (char *)spec_info->search_info->gchar_data;
	int j = 0, found = 0;
	sview_block_info_t *block_ptr = NULL;
	int update = 0;
	GtkTreeView *treeview = NULL;
	ListIterator itr = NULL;

	if (!spec_info->search_info->gchar_data)
		goto finished;

need_refresh:
	if (!spec_info->display_widget) {
		treeview = create_treeview_2cols_attach_to_table(
			popup_win->table);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(treeview));
	} else {
		treeview = GTK_TREE_VIEW(spec_info->display_widget);
		update = 1;
	}

	itr = list_iterator_create(block_list);
	while ((block_ptr = (sview_block_info_t*) list_next(itr))) {
		if (!strcmp(block_ptr->bg_block_name, name)
		    || !strcmp(block_ptr->mp_str, name)) {
			/* we want to over ride any subgrp in error
			   state */
			enum node_states state = NODE_STATE_UNKNOWN;

			if (block_ptr->state & BG_BLOCK_ERROR_FLAG)
				state = NODE_STATE_ERROR;
			else if (list_count(block_ptr->job_list))
				state = NODE_STATE_ALLOCATED;
			else
				state = NODE_STATE_IDLE;

			j = 0;
			while (block_ptr->mp_inx[j] >= 0) {
				change_grid_color(
					popup_win->grid_button_list,
					block_ptr->mp_inx[j],
					block_ptr->mp_inx[j+1],
					block_ptr->color_inx, true,
					state);
				j += 2;
			}
			_layout_block_record(treeview, block_ptr, update);
			found = 1;
			break;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	if (!found) {
		if (!popup_win->not_found) {
			char *temp = "BLOCK DOESN'T EXSIST\n";
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
		if (popup_win->not_found) {
			popup_win->not_found = false;
			gtk_widget_destroy(spec_info->display_widget);

			goto need_refresh;
		}
	}
	gtk_widget_show(spec_info->display_widget);

finished:

	return;
}

extern void refresh_block(GtkAction *action, gpointer user_data)
{
	popup_info_t *popup_win = (popup_info_t *)user_data;
	xassert(popup_win);
	xassert(popup_win->spec_info);
	xassert(popup_win->spec_info->title);
	popup_win->force_refresh = 1;
	specific_info_block(popup_win);
}

extern int get_new_info_block(block_info_msg_t **block_ptr, int force)
{
	int error_code = SLURM_NO_CHANGE_IN_DATA;
	block_info_msg_t *new_bg_ptr = NULL;
	time_t now = time(NULL);
	static time_t last;
	static bool changed = 0;
	uint16_t show_flags = 0;

	if (!(cluster_flags & CLUSTER_FLAG_BG))
		return error_code;

	if (g_block_info_ptr && !force
	    && ((now - last) < working_sview_config.refresh_delay)) {
		if (*block_ptr != g_block_info_ptr)
			error_code = SLURM_SUCCESS;
		*block_ptr = g_block_info_ptr;
		if (changed)
			error_code = SLURM_SUCCESS;
		goto end_it;
	}
	last = now;
	if (working_sview_config.show_hidden)
		show_flags |= SHOW_ALL;
	if (g_block_info_ptr) {
		error_code = slurm_load_block_info(
			g_block_info_ptr->last_update, &new_bg_ptr, show_flags);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_block_info_msg(g_block_info_ptr);
			changed = 1;
		} else if (slurm_get_errno() == SLURM_NO_CHANGE_IN_DATA) {
			error_code = SLURM_NO_CHANGE_IN_DATA;
			new_bg_ptr = g_block_info_ptr;
			changed = 0;
		}
	} else {
		new_bg_ptr = NULL;
		error_code = slurm_load_block_info(
			(time_t) NULL, &new_bg_ptr, show_flags);
		changed = 1;
	}

	g_block_info_ptr = new_bg_ptr;
	if (block_ptr) {
		if (g_block_info_ptr && (*block_ptr != g_block_info_ptr))
			error_code = SLURM_SUCCESS;

		*block_ptr = g_block_info_ptr;
	}
end_it:
	return error_code;
}

extern int update_state_block(GtkDialog *dialog,
			      const char *blockid, const char *type)
{
	int i = 0;
	int rc = SLURM_SUCCESS;
	char tmp_char[100];
	update_block_msg_t block_msg;
	GtkWidget *label = NULL;
	int no_dialog = 0;

	if (!dialog) {
		dialog = GTK_DIALOG(
			gtk_dialog_new_with_buttons(
				type,
				GTK_WINDOW(main_window),
				GTK_DIALOG_MODAL
				| GTK_DIALOG_DESTROY_WITH_PARENT,
				NULL));
		no_dialog = 1;
	}
	slurm_init_update_block_msg(&block_msg);
	block_msg.bg_block_id = (char *)blockid;

	label = gtk_dialog_add_button(dialog,
				      GTK_STOCK_YES, GTK_RESPONSE_OK);
	gtk_window_set_default(GTK_WINDOW(dialog), label);
	gtk_dialog_add_button(dialog,
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	if (!strcasecmp("Error", type) ||
	    !strcasecmp("Put block in error state", type)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to put block %s "
			 "in an error state?",
			 blockid);
		block_msg.state = BG_BLOCK_ERROR_FLAG;
	} else if (!strcasecmp("Recreate block", type)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to recreate block %s?",
			 blockid);
		block_msg.state = BG_BLOCK_BOOTING;
	} else if (!strcasecmp("Remove block", type)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to remove block %s?",
			 blockid);
		block_msg.state = BG_BLOCK_NAV;
	} else if (!strcasecmp("Resume block", type)) {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to resume block %s?",
			 blockid);
		block_msg.state = BG_BLOCK_TERM;
	} else {
		snprintf(tmp_char, sizeof(tmp_char),
			 "Are you sure you want to put block %s "
			 "in a free state?",
			 blockid);
		block_msg.state = BG_BLOCK_FREE;
	}

	label = gtk_label_new(tmp_char);

	gtk_box_pack_start(GTK_BOX(dialog->vbox), label, FALSE, FALSE, 0);

	gtk_widget_show_all(GTK_WIDGET(dialog));
	i = gtk_dialog_run(dialog);
	if (i == GTK_RESPONSE_OK) {
		if (slurm_update_block(&block_msg)
		    == SLURM_SUCCESS) {
			snprintf(tmp_char, sizeof(tmp_char),
				 "Block %s updated successfully",
				 blockid);
		} else {
			snprintf(tmp_char, sizeof(tmp_char),
				 "Problem updating block %s.",
				 blockid);
		}
		display_edit_note(tmp_char);
	}

	if (no_dialog)
		gtk_widget_destroy(GTK_WIDGET(dialog));
	return rc;
}

extern GtkListStore *create_model_block(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	last_model = NULL;	/* Reformat display */
	switch(type) {
	case SORTID_STATE:
		model = gtk_list_store_new(2, G_TYPE_STRING,
					   G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Error",
				   1, SORTID_STATE,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "Free",
				   1, SORTID_STATE,
				   -1);
		break;
	default:
		break;
	}

	return model;
}

extern void admin_edit_block(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data)
{
	GtkTreeStore *treestore = GTK_TREE_STORE(data);
	GtkTreePath *path = gtk_tree_path_new_from_string(path_string);
	GtkTreeIter iter;
	int column = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(cell),
						       "column"));

	char *blockid = NULL;
	char *old_text = NULL;
	if (!new_text || !strcmp(new_text, ""))
		goto no_input;

	gtk_tree_model_get_iter(GTK_TREE_MODEL(treestore), &iter, path);
	gtk_tree_model_get(GTK_TREE_MODEL(treestore), &iter,
			   SORTID_BLOCK, &blockid,
			   column, &old_text,
			   -1);
	switch(column) {
	case SORTID_STATE:
		update_state_block(NULL, blockid, new_text);
		break;
	default:
		break;
	}

	g_free(blockid);
	g_free(old_text);
no_input:
	gtk_tree_path_free(path);
	g_mutex_unlock(sview_mutex);
}

extern void get_info_block(GtkTable *table, display_data_t *display_data)
{
	int part_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static int view = -1;
	static partition_info_msg_t *part_info_ptr = NULL;
	static block_info_msg_t *block_ptr = NULL;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	static GtkWidget *display_widget = NULL;
	List block_list = NULL;
	int j=0;
	ListIterator itr = NULL;
	sview_block_info_t *sview_block_info_ptr = NULL;
	GtkTreePath *path = NULL;
	static bool set_opts = FALSE;

	if (!set_opts)
		set_page_opts(BLOCK_PAGE, display_data_block,
			      SORTID_CNT, _initial_page_opts);
	set_opts = TRUE;

	/* reset */
	if (!table && !display_data) {
		if (display_widget)
			gtk_widget_destroy(display_widget);
		display_widget = NULL;
		part_info_ptr = NULL;
		block_ptr = NULL;
		goto reset_curs;
	}

	if (display_data)
		local_display_data = display_data;
	if (!table) {
		display_data_block->set_menu = local_display_data->set_menu;
		goto reset_curs;
	}

	if (display_widget && toggled) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
		goto display_it;
	}

	if ((part_error_code = get_new_info_part(&part_info_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {

	} else if (part_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(GTK_TABLE(table),
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(label);
		goto end_it;
	}

	if ((block_error_code = get_new_info_block(&block_ptr, force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if ((!display_widget || view == ERROR_VIEW)
		    || (part_error_code != SLURM_NO_CHANGE_IN_DATA)) {
			goto display_it;
		}
	} else if (block_error_code != SLURM_SUCCESS) {
		if (view == ERROR_VIEW)
			goto end_it;
		view = ERROR_VIEW;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		sprintf(error_char, "slurm_load_block: %s",
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
	if (!block_ptr) {
		view = ERROR_VIEW;
		if (display_widget)
			gtk_widget_destroy(display_widget);
		label = gtk_label_new("No blocks on non-Bluegene systems");
		gtk_table_attach_defaults(GTK_TABLE(table),
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		display_widget = gtk_widget_ref(label);
		goto end_it;
	}
	if (!part_info_ptr)
		goto reset_curs;

	block_list = _create_block_list(part_info_ptr, block_ptr);
	if (!block_list)
		goto reset_curs;

	/* set up the grid */
	if (display_widget && GTK_IS_TREE_VIEW(display_widget)
	    && gtk_tree_selection_count_selected_rows(
		    gtk_tree_view_get_selection(
			    GTK_TREE_VIEW(display_widget)))) {
		GtkTreeViewColumn *focus_column = NULL;
		/* highlight the correct mp_str from the last selection */
		gtk_tree_view_get_cursor(GTK_TREE_VIEW(display_widget),
					 &path, &focus_column);
	}
	if (!path) {
		itr = list_iterator_create(block_list);
		while ((sview_block_info_ptr = list_next(itr))) {
			j=0;
			while (sview_block_info_ptr->mp_inx[j] >= 0) {
				change_grid_color(
					grid_button_list,
					sview_block_info_ptr->mp_inx[j],
					sview_block_info_ptr->mp_inx[j+1],
					sview_block_info_ptr->color_inx,
					true, 0);
				j += 2;
			}
		}
		list_iterator_destroy(itr);
		change_grid_color(grid_button_list, -1, -1,
				  MAKE_WHITE, true, 0);
	} else {
		highlight_grid(GTK_TREE_VIEW(display_widget),
			       SORTID_NODE_INX, SORTID_COLOR_INX,
			       grid_button_list);
		gtk_tree_path_free(path);
	}

	if (view == ERROR_VIEW && display_widget) {
		gtk_widget_destroy(display_widget);
		display_widget = NULL;
	}
	if (!display_widget) {
		tree_view = create_treeview(local_display_data,
					    &grid_button_list);
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		display_widget = gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, display_data_block,
				 SORTID_CNT, SORTID_NODELIST, SORTID_COLOR);
	}

	view = INFO_VIEW;
	_update_info_block(block_list, GTK_TREE_VIEW(display_widget));
end_it:
	toggled = FALSE;
	force_refresh = FALSE;

reset_curs:
	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);
	return;
}

extern void specific_info_block(popup_info_t *popup_win)
{
	int part_error_code = SLURM_SUCCESS;
	int block_error_code = SLURM_SUCCESS;
	static partition_info_msg_t *part_info_ptr = NULL;
	static block_info_msg_t *block_info_ptr = NULL;
	specific_info_t *spec_info = popup_win->spec_info;
	sview_search_info_t *search_info = spec_info->search_info;
	char error_char[100];
	GtkWidget *label = NULL;
	GtkTreeView *tree_view = NULL;
	List block_list = NULL;
	List send_block_list = NULL;
	sview_block_info_t *block_ptr = NULL;
	int j=0, i=-1;
	hostset_t hostset = NULL;
	ListIterator itr = NULL;

	if (!spec_info->display_widget) {
		setup_popup_info(popup_win, display_data_block, SORTID_CNT);
	}

	if (spec_info->display_widget && popup_win->toggled) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
		goto display_it;
	}

	if ((part_error_code = get_new_info_part(&part_info_ptr,
						 popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {

	} else if (part_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_partitions: %s",
			slurm_strerror(slurm_get_errno()));
		label = gtk_label_new(error_char);
		gtk_table_attach_defaults(popup_win->table,
					  label,
					  0, 1, 0, 1);
		gtk_widget_show(label);
		spec_info->display_widget = gtk_widget_ref(label);
		goto end_it;
	}

	if ((block_error_code =
	     get_new_info_block(&block_info_ptr, popup_win->force_refresh))
	    == SLURM_NO_CHANGE_IN_DATA) {
		if ((!spec_info->display_widget
		     || spec_info->view == ERROR_VIEW)
		    || (part_error_code != SLURM_NO_CHANGE_IN_DATA)) {
			goto display_it;
		}
	} else if (block_error_code != SLURM_SUCCESS) {
		if (spec_info->view == ERROR_VIEW)
			goto end_it;
		spec_info->view = ERROR_VIEW;
		if (spec_info->display_widget)
			gtk_widget_destroy(spec_info->display_widget);
		sprintf(error_char, "slurm_load_block: %s",
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
	block_list = _create_block_list(part_info_ptr, block_info_ptr);

	if (!block_list)
		return;

	if (spec_info->view == ERROR_VIEW && spec_info->display_widget) {
		gtk_widget_destroy(spec_info->display_widget);
		spec_info->display_widget = NULL;
	}
	if (spec_info->type != INFO_PAGE && !spec_info->display_widget) {
		tree_view = create_treeview(local_display_data,
					    &popup_win->grid_button_list);
		gtk_tree_selection_set_mode(
			gtk_tree_view_get_selection(tree_view),
			GTK_SELECTION_MULTIPLE);
		spec_info->display_widget =
			gtk_widget_ref(GTK_WIDGET(tree_view));
		gtk_table_attach_defaults(popup_win->table,
					  GTK_WIDGET(tree_view),
					  0, 1, 0, 1);
		/* since this function sets the model of the tree_view
		   to the treestore we don't really care about
		   the return value */
		create_treestore(tree_view, popup_win->display_data,
				 SORTID_CNT, SORTID_BLOCK, SORTID_COLOR);
	}

	setup_popup_grid_list(popup_win);
	spec_info->view = INFO_VIEW;
	if (spec_info->type == INFO_PAGE) {
		_display_info_block(block_list, popup_win);
		goto end_it;
	}

	/* just linking to another list, don't free the inside, just
	   the list */
	send_block_list = list_create(NULL);
	itr = list_iterator_create(block_list);
	i = -1;
	while ((block_ptr = list_next(itr))) {
		/* we want to over ride any subgrp in error
		   state */
		enum node_states state = NODE_STATE_UNKNOWN;
		char *name = NULL;

		i++;
		switch(spec_info->type) {
		case PART_PAGE:
			if (strcmp(block_ptr->slurm_part_name,
				   search_info->gchar_data))
				continue;
			break;
		case RESV_PAGE:
		case NODE_PAGE:
			if (!block_ptr->mp_str)
				continue;
			if (!(hostset = hostset_create(
				      search_info->gchar_data)))
				continue;
			name = block_ptr->mp_str;
			if (block_ptr->small_block) {
				int j=0;
				/* strip off the ionodes part */
				while (name[j]) {
					if (name[j] == '[') {
						name[j] = '\0';
						break;
					}
					j++;
				}
			}

			if (!hostset_intersects(hostset, name)) {
				hostset_destroy(hostset);
				continue;
			}
			hostset_destroy(hostset);
			break;
		case BLOCK_PAGE:
			switch(search_info->search_type) {
			case SEARCH_BLOCK_NAME:
				if (!search_info->gchar_data)
					continue;

				if (strcmp(block_ptr->bg_block_name,
					   search_info->gchar_data))
					continue;
				break;
			case SEARCH_BLOCK_SIZE:
				if (search_info->int_data == NO_VAL)
					continue;
				if (block_ptr->cnode_cnt
				    != search_info->int_data)
					continue;
				break;
			case SEARCH_BLOCK_STATE:
				if (search_info->int_data == NO_VAL)
					continue;
				if (block_ptr->state != search_info->int_data)
					continue;

				break;
			default:
				continue;
				break;
			}
			break;
		case JOB_PAGE:
			if (strcmp(block_ptr->bg_block_name,
				   search_info->gchar_data))
				continue;
			break;
		default:
			g_print("Unknown type %d\n", spec_info->type);
			continue;
		}
		list_push(send_block_list, block_ptr);

		if (block_ptr->state & BG_BLOCK_ERROR_FLAG)
			state = NODE_STATE_ERROR;
		else if (list_count(block_ptr->job_list))
			state = NODE_STATE_ALLOCATED;
		else
			state = NODE_STATE_IDLE;

		j=0;
		while (block_ptr->mp_inx[j] >= 0) {
			change_grid_color(
				popup_win->grid_button_list,
				block_ptr->mp_inx[j],
				block_ptr->mp_inx[j+1], block_ptr->color_inx,
				true, state);
			j += 2;
		}
	}
	list_iterator_destroy(itr);
	post_setup_popup_grid_list(popup_win);

	_update_info_block(send_block_list,
			   GTK_TREE_VIEW(spec_info->display_widget));
	list_destroy(send_block_list);
end_it:
	popup_win->toggled = 0;
	popup_win->force_refresh = 0;

	return;
}

extern void set_menus_block(void *arg, void *arg2, GtkTreePath *path, int type)
{
	GtkTreeView *tree_view = (GtkTreeView *)arg;
	popup_info_t *popup_win = (popup_info_t *)arg;
	GtkMenu *menu = (GtkMenu *)arg2;
	List button_list = (List)arg2;

	switch(type) {
	case TAB_CLICKED:
		make_fields_menu(NULL, menu, display_data_block, SORTID_CNT);
		break;
	case ROW_CLICKED:
		make_options_menu(tree_view, path, menu, options_data_block);
		break;
	case ROW_LEFT_CLICKED:
		highlight_grid(tree_view, SORTID_NODE_INX,
			       SORTID_COLOR_INX, button_list);
		break;
	case FULL_CLICKED:
	{
		GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter(model, &iter, path)) {
			g_error("error getting iter from model\n");
			break;
		}

		popup_all_block(model, &iter, INFO_PAGE);

		break;
	}
	case POPUP_CLICKED:
		make_fields_menu(popup_win, menu,
				 popup_win->display_data, SORTID_CNT);
		break;
	default:
		g_error("UNKNOWN type %d given to set_fields\n", type);
	}
}

extern void popup_all_block(GtkTreeModel *model, GtkTreeIter *iter, int id)
{
	char *name = NULL;
	char title[100];
	ListIterator itr = NULL;
	popup_info_t *popup_win = NULL;
	GError *error = NULL;
	int i=0;

	gtk_tree_model_get(model, iter, SORTID_BLOCK, &name, -1);

	switch(id) {
	case JOB_PAGE:
		snprintf(title, 100, "Jobs(s) in block %s", name);
		break;
	case PART_PAGE:
		snprintf(title, 100, "Partition(s) containing block %s", name);
		break;
	case RESV_PAGE:
		snprintf(title, 100, "Reservations(s) containing block %s",
			 name);
		break;
	case NODE_PAGE:
		snprintf(title, 100, "Midplane(s) in block %s", name);
		break;
	case SUBMIT_PAGE:
		snprintf(title, 100, "Submit job on %s", name);
		break;
	case INFO_PAGE:
		snprintf(title, 100, "Full info for block %s", name);
		break;
	default:
		g_print("Block got %d\n", id);
	}

	itr = list_iterator_create(popup_list);
	while ((popup_win = list_next(itr))) {
		if (popup_win->spec_info)
			if (!strcmp(popup_win->spec_info->title, title)) {
				break;
			}
	}
	list_iterator_destroy(itr);

	if (!popup_win) {
		if (id == INFO_PAGE)
			popup_win = create_popup_info(id, BLOCK_PAGE, title);
		else
			popup_win = create_popup_info(BLOCK_PAGE, id, title);
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
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case PART_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_PARTITION, &name, -1);
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case RESV_PAGE:
	case NODE_PAGE:
		g_free(name);
		gtk_tree_model_get(model, iter, SORTID_NODELIST, &name, -1);
		gtk_tree_model_get(model, iter, SORTID_SMALL_BLOCK, &i, -1);
		if (i) {
			i=0;
			/* strip off the ionodes part */
			while (name[i]) {
				if (name[i] == '[') {
					name[i] = '\0';
					break;
				}
				i++;
			}
		}
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	case INFO_PAGE:
		popup_win->spec_info->search_info->gchar_data = name;
		break;
	default:
		g_print("block got %d\n", id);
	}


	if (!sview_thread_new((gpointer)popup_thr, popup_win, FALSE, &error)) {
		g_printerr ("Failed to create part popup thread: %s\n",
			    error->message);
		return;
	}
}

static void _process_each_block(GtkTreeModel *model, GtkTreePath *path,
				GtkTreeIter*iter, gpointer userdata)
{
	char *type = userdata;
	if (_DEBUG)
		g_print("process_each_block: global_multi_error = %d\n",
			global_multi_error);

	if (!global_multi_error) {
		_admin_block(model, iter, type);
	}
}

extern void select_admin_block(GtkTreeModel *model, GtkTreeIter *iter,
			       display_data_t *display_data,
			       GtkTreeView *treeview)
{
	if (treeview) {
		if (display_data->extra & EXTRA_NODES) {
			select_admin_nodes(model, iter, display_data,
					   SORTID_NODELIST, treeview);
			return;
		}
		global_multi_error = FALSE;
		gtk_tree_selection_selected_foreach(
			gtk_tree_view_get_selection(treeview),
			_process_each_block, display_data->name);
	}
}

static void _admin_block(GtkTreeModel *model, GtkTreeIter *iter, char *type)
{
	char *blockid = NULL;
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		type,
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	gtk_window_set_transient_for(GTK_WINDOW(popup), NULL);

	gtk_tree_model_get(model, iter, SORTID_BLOCK, &blockid, -1);

	update_state_block(GTK_DIALOG(popup), blockid, type);

	g_free(blockid);
	gtk_widget_destroy(popup);

	return;
}

extern void cluster_change_block(void)
{
	display_data_t *display_data = display_data_block;
	while (display_data++) {
		if (display_data->id == -1)
			break;
		if (cluster_flags & CLUSTER_FLAG_BGQ) {
			switch(display_data->id) {
			case SORTID_USE:
			case SORTID_USER:
			case SORTID_IMAGEBLRTS:
			case SORTID_IMAGELINUX:
			case SORTID_IMAGERAMDISK:
				display_data->name = NULL;
				break;
			default:
				break;
			}
		} else if (cluster_flags & CLUSTER_FLAG_BGP) {
			switch(display_data->id) {
			case SORTID_USE:
			case SORTID_IMAGEBLRTS:
				display_data->name = NULL;
				break;
			case SORTID_IMAGELINUX:
				display_data->name = "Image Cnload";
				break;
			case SORTID_IMAGERAMDISK:
				display_data->name = "Image Ioload";
				break;
			case SORTID_USER:
				display_data->name = "User";
				break;
			default:
				break;
			}
		} else if (cluster_flags & CLUSTER_FLAG_BGL) {
			switch(display_data->id) {
			case SORTID_USE:
				display_data->name = "Node Use";
				break;
			case SORTID_IMAGEBLRTS:
				display_data->name = "Image Blrt";
				break;
			case SORTID_IMAGELINUX:
				display_data->name = "Image Linux";
				break;
			case SORTID_IMAGERAMDISK:
				display_data->name = "Image Ramdisk";
				break;
			case SORTID_USER:
				display_data->name = "User";
				break;
			default:
				break;
			}
		}
	}
	get_info_block(NULL, NULL);
}
