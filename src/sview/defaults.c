/****************************************************************************\
 *  defaults.c - put default configuration information here
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <fcntl.h>

#include "sview.h"
#include "src/common/parse_config.h"
#include "src/common/xstring.h"

/* These need to be in alpha order (except POS and CNT) */
enum {
	SORTID_POS = POS_LOC,
	SORTID_ADMIN,
	SORTID_BUTTON_SIZE,
	SORTID_DEFAULT_PAGE,
	SORTID_GRID_HORI,
	SORTID_GRID_VERT,
	SORTID_GRID_X_WIDTH,
	SORTID_GRID_TOPO_ORDER,
	SORTID_PAGE_VISIBLE,
	SORTID_REFRESH_DELAY,
	SORTID_RULED_TV,
	SORTID_SHOW_GRID,
	SORTID_SHOW_HIDDEN,
	SORTID_SAVE_PAGE_OPTS,
	SORTID_TAB_POS,
	SORTID_CNT
};

/* extra field here is for choosing the type of edit you that will
 * take place.  If you choose EDIT_MODEL (means only display a set of
 * known options) create it in function create_model_*.
 */

static display_data_t display_data_defaults[] = {
	{G_TYPE_INT, SORTID_POS, NULL, false, EDIT_NONE, NULL},
	{G_TYPE_STRING, SORTID_ADMIN, "Start in Admin Mode",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_BUTTON_SIZE, "Node Button Size in Pixels",
	 true, EDIT_TEXTBOX, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_DEFAULT_PAGE, "Default Page",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_GRID_HORI, "Grid: Nodes before Horizontal break",
	 true, EDIT_TEXTBOX, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_GRID_VERT, "Grid: Nodes before Vertical break",
	 true, EDIT_TEXTBOX, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_GRID_X_WIDTH, "Grid: Nodes in Row",
	 true, EDIT_TEXTBOX, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_GRID_TOPO_ORDER, "Grid: Topology Order",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_PAGE_VISIBLE, "Visible Pages",
	 true, EDIT_ARRAY, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_REFRESH_DELAY, "Refresh Delay in Secs",
	 true, EDIT_TEXTBOX, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_RULED_TV, "Ruled Tables",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_SHOW_GRID, "Show Grid",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_SHOW_HIDDEN, "Show Hidden",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_SAVE_PAGE_OPTS, "Save Page Settings",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_STRING, SORTID_TAB_POS, "Tab Position",
	 true, EDIT_MODEL, NULL, create_model_defaults, NULL},
	{G_TYPE_NONE, -1, NULL, false, EDIT_NONE}
};


extern display_data_t main_display_data[];

static int _trans_tab_pos(int tab_pos)
{
	/* default to TOP if unknown is given */
	switch (tab_pos) {
	case GTK_POS_TOP:
		return 0;
	case GTK_POS_BOTTOM:
		return 1;
	case GTK_POS_LEFT:
		return 2;
	case GTK_POS_RIGHT:
		return 3;
	default:
		return 0;
	}
	return 0;
}

static void _set_active_combo_defaults(GtkComboBox *combo,
				       sview_config_t *sview_config,
				       int type)
{
	int action = 0;

	switch(type) {
	case SORTID_ADMIN:
		action = sview_config->admin_mode;
		break;
	case SORTID_GRID_TOPO_ORDER:
		action = sview_config->grid_topological;
		break;
	case SORTID_RULED_TV:
		action = sview_config->ruled_treeview;
		break;
	case SORTID_SHOW_GRID:
		action = sview_config->show_grid;
		break;
	case SORTID_SHOW_HIDDEN:
		action = sview_config->show_hidden;
		break;
	case SORTID_SAVE_PAGE_OPTS:
		action = sview_config->save_page_opts;
		break;
	case SORTID_DEFAULT_PAGE:
		action = sview_config->default_page;
		break;
	case SORTID_TAB_POS:
		action = _trans_tab_pos(sview_config->tab_pos);
		break;
	default:
		break;
	}

	gtk_combo_box_set_active(combo, action);
}

static const char *_set_sview_config(sview_config_t *sview_config,
				     const char *new_text, int column)
{
	char *type = "";
	int temp_int = 0;

	/* need to clear global_edit_error here (just in case) */
	global_edit_error = 0;
	if (!sview_config)
		return NULL;

	switch(column) {
	case SORTID_ADMIN:
		type = "Admin Mode";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->admin_mode = 1;
		else
			sview_config->admin_mode = 0;
		break;
	case SORTID_BUTTON_SIZE:
		type = "Button Size";
		temp_int = strtol(new_text, (char **)NULL, 10);
		if ((temp_int <= 0) && (temp_int != INFINITE))
			goto return_error;
		sview_config->button_size = temp_int;
		sview_config->gap_size = MIN(temp_int/2, 2);
		break;
	case SORTID_DEFAULT_PAGE:
		if (!xstrcasecmp(new_text, "job"))
			sview_config->default_page = JOB_PAGE;
		else if (!xstrcasecmp(new_text, "part"))
			sview_config->default_page = PART_PAGE;
		else if (!xstrcasecmp(new_text, "res"))
			sview_config->default_page = RESV_PAGE;
		else if (!xstrcasecmp(new_text, "node"))
			sview_config->default_page = NODE_PAGE;
		else if (!xstrcasecmp(new_text, "frontend"))
			sview_config->default_page = FRONT_END_PAGE;
		else if (!xstrcasecmp(new_text, "burstbuffer"))
			sview_config->default_page = BB_PAGE;
		else
			sview_config->default_page = JOB_PAGE;
		break;
	case SORTID_GRID_HORI:
		temp_int = strtol(new_text, (char **)NULL, 10);
		if (temp_int <= 0)
			goto return_error;
		sview_config->grid_hori = temp_int;
		break;
	case SORTID_GRID_VERT:
		temp_int = strtol(new_text, (char **)NULL, 10);
		if (temp_int <= 0)
			goto return_error;
		sview_config->grid_vert = temp_int;
		break;
	case SORTID_GRID_X_WIDTH:
		temp_int = strtol(new_text, (char **)NULL, 10);
		if (temp_int <= 0)
			goto return_error;
		sview_config->grid_x_width = temp_int;
		break;
	case SORTID_PAGE_VISIBLE:
		break;
	case SORTID_REFRESH_DELAY:
		type = "Refresh Delay";
		temp_int = strtol(new_text, (char **)NULL, 10);
		if ((temp_int <= 0) && (temp_int != INFINITE))
			goto return_error;
		sview_config->refresh_delay = temp_int;
		break;
	case SORTID_RULED_TV:
		type = "Ruled Tables";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->ruled_treeview = 1;
		else
			sview_config->ruled_treeview = 0;
		break;
	case SORTID_SHOW_GRID:
		type = "Show Grid";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->show_grid = 1;
		else
			sview_config->show_grid = 0;
		break;
	case SORTID_GRID_TOPO_ORDER:
		type = "Topology order";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->grid_topological = 1;
		else
			sview_config->grid_topological =  0;
		break;
	case SORTID_SHOW_HIDDEN:
		type = "Show Hidden";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->show_hidden = 1;
		else
			sview_config->show_hidden = 0;
		break;
	case SORTID_SAVE_PAGE_OPTS:
		type = "Save Page Settings";
		if (!xstrcasecmp(new_text, "yes"))
			sview_config->save_page_opts = 1;
		else
			sview_config->save_page_opts = 0;
		break;
	case SORTID_TAB_POS:
		type = "Tab Position";
		if (!xstrcasecmp(new_text, "top")) {
			sview_config->tab_pos = GTK_POS_TOP;
		} else if (!xstrcasecmp(new_text, "bottom")) {
			sview_config->tab_pos = GTK_POS_BOTTOM;
		} else if (!xstrcasecmp(new_text, "left")) {
			sview_config->tab_pos = GTK_POS_LEFT;
		} else if (!xstrcasecmp(new_text, "right"))
			sview_config->tab_pos = GTK_POS_RIGHT;
		else
			goto return_error;
		break;
	default:
		type = "unknown";
		break;
	}
	if (xstrcmp(type, "unknown")) {
		global_send_update_msg = 1;

	}
	return type;

return_error:
	global_edit_error = 1;
	return type;
}

static void _admin_focus_toggle(GtkToggleButton *toggle_button,
				bool *visible)
{
	if (visible) {
		(*visible) = gtk_toggle_button_get_active(toggle_button);
		global_send_update_msg = 1;
	}
}

static void _admin_edit_combo_box_defaults(GtkComboBox *combo,
					   sview_config_t *sview_config)
{
	GtkTreeModel *model = NULL;
	GtkTreeIter iter;
	int column = 0;
	char *name = NULL;

	if (!sview_config)
		return;
	if (!gtk_combo_box_get_active_iter(combo, &iter)) {
		g_print("nothing selected\n");
		return;
	}
	model = gtk_combo_box_get_model(combo);
	if (!model) {
		g_print("nothing selected\n");
		return;
	}
	gtk_tree_model_get(model, &iter, 0, &name, -1);
	gtk_tree_model_get(model, &iter, 1, &column, -1);

	_set_sview_config(sview_config, name, column);
	g_free(name);
}

static gboolean _admin_focus_out_defaults(GtkEntry *entry,
					  GdkEventFocus *event,
					  sview_config_t *sview_config)
{
	if (global_entry_changed) {
		const char *col_name = NULL;
		int type = gtk_entry_get_max_length(entry);
		const char *name = gtk_entry_get_text(entry);
		type -= DEFAULT_ENTRY_LENGTH;
		col_name = _set_sview_config(sview_config, name, type);
		if (global_edit_error) {
			if (global_edit_error_msg)
				g_free(global_edit_error_msg);
			global_edit_error_msg = g_strdup_printf(
				"Default for %s can't be set to %s",
				col_name,
				name);
		}

		global_entry_changed = 0;
	}
	return false;
}

static void _local_display_admin_edit(GtkTable *table,
				      sview_config_t *sview_config, int *row,
				      display_data_t *display_data)
{
	GtkWidget *label = NULL;
	GtkWidget *entry = NULL;

	if (display_data->extra == EDIT_MODEL) {
		/* edittable items that can only be known
		   values */
		GtkCellRenderer *renderer = NULL;
		GtkTreeModel *model2 = GTK_TREE_MODEL(
			create_model_defaults(display_data->id));
		if (!model2) {
			g_print("no model set up for %d(%s)\n",
				display_data->id,
				display_data->name);
			return;
		}
		entry = gtk_combo_box_new_with_model(model2);
		g_object_unref(model2);

		_set_active_combo_defaults(GTK_COMBO_BOX(entry), sview_config,
					   display_data->id);

		g_signal_connect(entry, "changed",
				 G_CALLBACK(_admin_edit_combo_box_defaults),
				 sview_config);

		renderer = gtk_cell_renderer_text_new();
		gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(entry),
					   renderer, true);
		gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(entry),
					      renderer, "text", 0);
	} else if (display_data->extra == EDIT_TEXTBOX) {
		char *temp_char = NULL;
		/* other edittable items that are unknown */
		entry = create_entry();
		switch(display_data->id) {
		case SORTID_GRID_HORI:
			temp_char = xstrdup_printf("%u",
						   sview_config->grid_hori);
			break;
		case SORTID_GRID_VERT:
			temp_char = xstrdup_printf("%u",
						   sview_config->grid_vert);
			break;
		case SORTID_GRID_X_WIDTH:
			temp_char = xstrdup_printf("%u",
						   sview_config->grid_x_width);
			break;
		case SORTID_BUTTON_SIZE:
			temp_char = xstrdup_printf("%u",
						   sview_config->button_size);
			break;
		case SORTID_REFRESH_DELAY:
			temp_char = xstrdup_printf("%u",
						   sview_config->refresh_delay);
			break;
		default:
			break;
		}

		gtk_entry_set_max_length(GTK_ENTRY(entry),
					 (DEFAULT_ENTRY_LENGTH +
					  display_data->id));

		if (temp_char) {
			gtk_entry_set_text(GTK_ENTRY(entry),
					   temp_char);
			xfree(temp_char);
		}
		g_signal_connect(entry, "focus-out-event",
				 G_CALLBACK(_admin_focus_out_defaults),
				 sview_config);

		/* set global variable so we know something changed */
		g_signal_connect(entry, "changed",
				 G_CALLBACK(entry_changed),
				 NULL);
	} else if (display_data->extra == EDIT_ARRAY) {
		int i;
		switch(display_data->id) {
		case SORTID_PAGE_VISIBLE:
			label = gtk_label_new(display_data->name);
			/* left justify */
			gtk_misc_set_alignment(GTK_MISC(label),0.0,0.5);

			gtk_table_attach(table, label, 0, 1,
					 *row, (*row)+1,
					 GTK_FILL | GTK_EXPAND,
					 GTK_SHRINK, 0, 0);
			for(i=0; i<PAGE_CNT; i++) {
				if (main_display_data[i].id == -1)
					break;

				if (!main_display_data[i].name
				    || (i == TAB_PAGE))
					continue;
				entry = gtk_check_button_new_with_label(
					main_display_data[i].name);
				gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(entry),
					sview_config->page_visible[i]);
				g_signal_connect(
					G_OBJECT(entry),
					"toggled",
					G_CALLBACK(_admin_focus_toggle),
					&sview_config->page_visible[i]);

				gtk_table_attach(table, entry, 1, 2,
						 *row, (*row)+1,
						 GTK_FILL, GTK_SHRINK,
						 0, 0);
				(*row)++;
			}
			break;
		default:
			break;
		}
		return;
	} else /* others can't be altered by the user */
		return;
	label = gtk_label_new(display_data->name);
	/* left justify */
	gtk_misc_set_alignment(GTK_MISC(label),0.0,0.5);

	gtk_table_attach(table, label, 0, 1, *row, (*row)+1,
			 GTK_FILL | GTK_EXPAND, GTK_SHRINK,
			 0, 0);
	gtk_table_attach(table, entry, 1, 2, *row, (*row)+1,
			 GTK_FILL, GTK_SHRINK,
			 0, 0);
	(*row)++;
}


static int _write_to_file(int fd, char *data)
{
	int pos = 0, nwrite = strlen(data), amount;
	int rc = SLURM_SUCCESS;

	while (nwrite > 0) {
		amount = write(fd, &data[pos], nwrite);
		if ((amount < 0) && (errno != EINTR)) {
			error("Error writing file: %m");
			rc = errno;
			break;
		}
		nwrite -= amount;
		pos    += amount;
	}
	return rc;
}

static void _init_sview_conf(void)
{
	int i;

	default_sview_config.main_width = 1000;
	default_sview_config.main_height = 500;
	default_sview_config.fi_popup_width = 800;
	default_sview_config.fi_popup_height = 500;
	default_sview_config.button_size = 10;
	default_sview_config.gap_size = 5;
	default_sview_config.refresh_delay = 5;
	default_sview_config.grid_x_width = 0;
	default_sview_config.grid_hori = 10;
	default_sview_config.grid_vert = 10;
	default_sview_config.show_hidden = 0;
	default_sview_config.admin_mode = false;
	default_sview_config.grid_topological = false;
	default_sview_config.ruled_treeview = false;
	default_sview_config.show_grid = true;
	default_sview_config.default_page = JOB_PAGE;
	default_sview_config.tab_pos = GTK_POS_TOP;
	default_sview_config.convert_flags = CONVERT_NUM_UNIT_EXACT;

	for(i=0; i<PAGE_CNT; i++) {
		memset(&default_sview_config.page_opts[i],
		       0, sizeof(page_opts_t));

		if (!main_display_data[i].show)
			default_sview_config.page_visible[i] = false;
		else
			default_sview_config.page_visible[i] = true;
	}
}

extern int load_defaults(void)
{
	s_p_hashtbl_t *hashtbl = NULL;
	s_p_options_t sview_conf_options[] = {
		{"AdminMode", S_P_BOOLEAN},
		{"ButtonSize", S_P_UINT16},
		{"DefaultPage", S_P_STRING},
		{"ExcludedPartitions", S_P_STRING},	/* Vestigial */
		{"FullInfoPopupWidth", S_P_UINT32},
		{"FullInfoPopupHeight", S_P_UINT32},
		{"GridHorizontal", S_P_UINT32},
		{"GridSpeedUp", S_P_BOOLEAN},
		{"GridTopo", S_P_BOOLEAN},
		{"GridVertical", S_P_UINT32},
		{"GridXWidth", S_P_UINT32},
		{"MainHeight", S_P_UINT32},
		{"MainWidth", S_P_UINT32},
		{"PageOptsBlock", S_P_STRING},
		{"PageOptsJob", S_P_STRING},
		{"PageOptsNode", S_P_STRING},
		{"PageOptsPartition", S_P_STRING},
		{"PageOptsReservation", S_P_STRING},
		{"PageOptsFrontend", S_P_STRING},
		{"PageOptsBurstBuffer", S_P_STRING},
		{"RefreshDelay", S_P_UINT16},
		{"RuledTables", S_P_BOOLEAN},
		{"SavePageSettings", S_P_BOOLEAN},
		{"ShowGrid", S_P_BOOLEAN},
		{"ShowHidden", S_P_BOOLEAN},
		{"TabPosition", S_P_STRING},
		{"VisiblePages", S_P_STRING},
		{NULL}
	};
	char *pathname = NULL;
	char *home = getenv("HOME");
	uint32_t hash_val = NO_VAL;
	int i, rc = SLURM_SUCCESS;
	char *tmp_str;

	_init_sview_conf();

	if (!home)
		goto end_it;

	pathname = xstrdup_printf("%s/.slurm", home);
	if ((mkdir(pathname, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", pathname);
		rc = SLURM_ERROR;
		goto end_it;
	}
	xstrcat(pathname, "/sviewrc");

	if (access(pathname, R_OK) != 0) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	hashtbl = s_p_hashtbl_create(sview_conf_options);

	if (s_p_parse_file(hashtbl, &hash_val, pathname, true) == SLURM_ERROR)
		error("something wrong with opening/reading conf file");

	s_p_get_boolean(&default_sview_config.admin_mode, "AdminMode", hashtbl);
	if (s_p_get_uint16(&default_sview_config.button_size, "ButtonSize",
			   hashtbl)) {
		default_sview_config.gap_size =
			MAX(default_sview_config.button_size/2, 2);
	}
	if (s_p_get_string(&tmp_str, "DefaultPage", hashtbl)) {
		if (xstrcasestr(tmp_str, "job"))
			default_sview_config.default_page = JOB_PAGE;
		else if (xstrcasestr(tmp_str, "part"))
			default_sview_config.default_page = PART_PAGE;
		else if (xstrcasestr(tmp_str, "res"))
			default_sview_config.default_page = RESV_PAGE;
		else if (xstrcasestr(tmp_str, "node"))
			default_sview_config.default_page = NODE_PAGE;
		else if (xstrcasestr(tmp_str, "frontend"))
			default_sview_config.default_page = FRONT_END_PAGE;
		else if (xstrcasestr(tmp_str, "burstbuffer"))
			default_sview_config.default_page = BB_PAGE;

		xfree(tmp_str);
	}
	s_p_get_uint32(&default_sview_config.grid_hori,
		       "GridHorizontal", hashtbl);
	s_p_get_boolean(&default_sview_config.grid_topological,
			"GridTopo", hashtbl);
	if (default_sview_config.grid_topological == 0)
		default_sview_config.grid_topological = false;
	s_p_get_uint32(&default_sview_config.grid_vert,
		       "GridVertical", hashtbl);
	s_p_get_uint32(&default_sview_config.grid_x_width,
		       "GridXWidth", hashtbl);
	s_p_get_uint16(&default_sview_config.refresh_delay,
		       "RefreshDelay", hashtbl);
	s_p_get_boolean(&default_sview_config.ruled_treeview,
			"RuledTables", hashtbl);
	s_p_get_boolean(&default_sview_config.show_grid,
			"ShowGrid", hashtbl);
	s_p_get_boolean(&default_sview_config.show_hidden,
			"ShowHidden", hashtbl);
	s_p_get_boolean(&default_sview_config.save_page_opts,
			"SavePageSettings", hashtbl);
	s_p_get_uint32(&default_sview_config.main_width,
		       "MainWidth", hashtbl);
	s_p_get_uint32(&default_sview_config.main_height,
		       "MainHeight", hashtbl);
	s_p_get_uint32(&default_sview_config.fi_popup_width,
		       "FullInfoPopupWidth", hashtbl);
	s_p_get_uint32(&default_sview_config.fi_popup_height,
		       "FullInfoPopupHeight", hashtbl);
	if (s_p_get_string(&tmp_str, "TabPosition", hashtbl)) {
		if (xstrcasestr(tmp_str, "top"))
			default_sview_config.tab_pos = GTK_POS_TOP;
		else if (xstrcasestr(tmp_str, "bottom"))
			default_sview_config.tab_pos = GTK_POS_BOTTOM;
		else if (xstrcasestr(tmp_str, "left"))
			default_sview_config.tab_pos = GTK_POS_LEFT;
		else if (xstrcasestr(tmp_str, "right"))
			default_sview_config.tab_pos = GTK_POS_RIGHT;
		xfree(tmp_str);
	}
	if (s_p_get_string(&tmp_str, "VisiblePages", hashtbl)) {
		int i = 0;
		for (i=0; i<PAGE_CNT; i++)
			default_sview_config.page_visible[i] = false;

		if (xstrcasestr(tmp_str, "job"))
			default_sview_config.page_visible[JOB_PAGE] = 1;
		if (xstrcasestr(tmp_str, "part"))
			default_sview_config.page_visible[PART_PAGE] = 1;
		if (xstrcasestr(tmp_str, "res"))
			default_sview_config.page_visible[RESV_PAGE] = 1;
		if (xstrcasestr(tmp_str, "node"))
			default_sview_config.page_visible[NODE_PAGE] = 1;
		if (xstrcasestr(tmp_str, "frontend"))
			default_sview_config.page_visible[FRONT_END_PAGE] = 1;
		if (xstrcasestr(tmp_str, "burstbuffer"))
			default_sview_config.page_visible[BB_PAGE] = 1;
		xfree(tmp_str);
	}

	/*pull in page options*/
	for (i=0; i<PAGE_CNT; i++) {
		char *col_list = NULL;
		char *page_name = page_to_str(i);
		page_opts_t *page_opts = &default_sview_config.page_opts[i];
		if (!page_name)
			continue;
		memset(page_opts, 0, sizeof(page_opts_t));
		page_opts->page_name = page_name;
		tmp_str = xstrdup_printf("PageOpts%s", page_name);
		s_p_get_string(&col_list, tmp_str, hashtbl);
		xfree(tmp_str);
		if (col_list) {
			page_opts->col_list = list_create(slurm_destroy_char);
			slurm_addto_char_list(page_opts->col_list, col_list);
			xfree(col_list);
		}
	}

	xfree(tmp_str);
	s_p_hashtbl_destroy(hashtbl);

end_it:
	/* copy it all into the working struct (memory will work out
	 * in the end the col_list doesn't change) */
	memcpy(&working_sview_config, &default_sview_config,
	       sizeof(sview_config_t));

	xfree(pathname);
	return rc;
}

extern int save_defaults(bool final_save)
{
	char *reg_file = NULL, *old_file = NULL, *new_file = NULL;
	char *home = getenv("HOME");
	int rc = SLURM_SUCCESS;
	char *tmp_str = NULL, *tmp_str2 = NULL;
	int fd = 0;
	int i = 0;
	display_data_t *display_data;

	if (!home)
		return SLURM_ERROR;

	reg_file = xstrdup_printf("%s/.slurm", home);
	if ((mkdir(reg_file, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", reg_file);
		rc = SLURM_ERROR;
		goto end_it;
	}
	xstrcat(reg_file, "/sviewrc");
	old_file = xstrdup_printf("%s.old", reg_file);
	new_file = xstrdup_printf("%s.new", reg_file);

	fd = creat(new_file, 0600);
	if (fd < 0) {
		error("Can't save config file %s error %m", reg_file);
		rc = errno;
		goto end_it;
	}

	tmp_str = xstrdup_printf("AdminMode=%s\n",
				 default_sview_config.admin_mode ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("DefaultPage=%s\n",
				 page_to_str(default_sview_config.
					     default_page));
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridHorizontal=%u\n",
				 default_sview_config.grid_hori);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridTopo=%s\n",
				 default_sview_config.grid_topological ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridVertical=%u\n",
				 default_sview_config.grid_vert);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridXWidth=%u\n",
				 default_sview_config.grid_x_width);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("ButtonSize=%u\n",
				 default_sview_config.button_size);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("RefreshDelay=%u\n",
				 default_sview_config.refresh_delay);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("MainWidth=%u\n",
				 default_sview_config.main_width);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("MainHeight=%u\n",
				 default_sview_config.main_height);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("FullInfoPopupWidth=%u\n",
				 default_sview_config.fi_popup_width);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("FullInfoPopupHeight=%u\n",
				 default_sview_config.fi_popup_height);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("RuledTables=%s\n",
				 default_sview_config.ruled_treeview ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("ShowGrid=%s\n",
				 default_sview_config.show_grid ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("ShowHidden=%s\n",
				 default_sview_config.show_hidden ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("SavePageSettings=%s\n",
				 default_sview_config.save_page_opts ?
				 "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("TabPosition=%s\n",
				 tab_pos_to_str(default_sview_config.tab_pos));
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str2 = visible_to_str(&default_sview_config);
	tmp_str = xstrdup_printf("VisiblePages=%s\n", tmp_str2);
	xfree(tmp_str2);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if (rc != SLURM_SUCCESS)
		goto end_it;

	/* save all current page options */
	for (i=0; i<PAGE_CNT; i++) {
		page_opts_t *page_opts =
			&working_sview_config.page_opts[i];

		if (!page_opts->page_name)
			continue;

		if (working_sview_config.save_page_opts) {
			display_data = page_opts->display_data;
			while (display_data++) {
				if (display_data->id == -1)
					break;
				if (!display_data->name || !display_data->show)
					continue;

				if (tmp_str2)
					xstrcat(tmp_str2, ",");
				xstrfmtcat(tmp_str2, "%s", display_data->name);
			} //spin the menu options ^^
		} else if (!page_opts->def_col_list
			   && page_opts->col_list
			   && list_count(page_opts->col_list)) {
			ListIterator itr =
				list_iterator_create(page_opts->col_list);
			char *col_name = NULL;
			//user requested no save of page options
			while ((col_name = list_next(itr))) {
				if (tmp_str2)
					xstrcat(tmp_str2, ",");
				xstrfmtcat(tmp_str2, "%s", col_name);
			}
			list_iterator_destroy(itr);
		}

		if (tmp_str2) {
			replspace(tmp_str2);
			tmp_str = xstrdup_printf("PageOpts%s=%s\n",
						 page_opts->page_name,
						 tmp_str2);
			xfree(tmp_str2);
			rc = _write_to_file(fd, tmp_str);
			xfree(tmp_str);
			if (rc != SLURM_SUCCESS)
				goto end_it;
		}
	}//spin the pages

end_it:
	fsync(fd);
	close(fd);

	if (rc)
		(void) unlink(new_file);
	else {			/* file shuffle */
		(void) unlink(old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink(reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink(new_file);
	}

	xfree(old_file);
	xfree(new_file);
	xfree(reg_file);
	return rc;
}

extern GtkListStore *create_model_defaults(int type)
{
	GtkListStore *model = NULL;
	GtkTreeIter iter;

	switch(type) {
	case SORTID_ADMIN:
	case SORTID_GRID_TOPO_ORDER:
	case SORTID_RULED_TV:
	case SORTID_SHOW_GRID:
	case SORTID_SHOW_HIDDEN:
	case SORTID_SAVE_PAGE_OPTS:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "no",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "yes",
				   1, type,
				   -1);
		break;
	case SORTID_DEFAULT_PAGE:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "job",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "part",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "res",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "node",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "frontend",
				   1, type,
				   -1);
		break;
	case SORTID_TAB_POS:
		model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "top",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "bottom",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "left",
				   1, type,
				   -1);
		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
				   0, "right",
				   1, type,
				   -1);
		break;
	default:
		break;
	}
	return model;
}

extern int configure_defaults(void)
{
	GtkScrolledWindow *window = create_scrolled_window();
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Sview Defaults",
		GTK_WINDOW(main_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		NULL);
	GtkWidget *label = gtk_dialog_add_button(GTK_DIALOG(popup),
						 GTK_STOCK_OK, GTK_RESPONSE_OK);
	GtkBin *bin = NULL;
	GtkViewport *view = NULL;
	GtkTable *table = NULL;
	int i = 0, row = 0;
	char tmp_char[100];
	char *tmp_char_ptr;
	display_data_t *display_data = display_data_defaults;
	sview_config_t tmp_config;
	int response = 0;
	int rc = SLURM_SUCCESS;
	uint32_t width = 150;
	uint32_t height = 700;

	apply_hidden_change = true;

	memcpy(&tmp_config, &default_sview_config, sizeof(sview_config_t));
	gtk_window_set_type_hint(GTK_WINDOW(popup),
				 GDK_WINDOW_TYPE_HINT_NORMAL);
	gtk_window_set_default(GTK_WINDOW(popup), label);
	gtk_dialog_add_button(GTK_DIALOG(popup),
			      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

	/*
	  for(i=0;; i++) {
	  if (main_popup_positioner[i].width == -1)
	  break;
	  if (strstr("Sview Defaults", main_popup_positioner[i].name)) {
	  pos_x = main_popup_positioner[i].width;
	  pos_y = main_popup_positioner[i].height;
	  break;
	  }
	  }
	*/

	gtk_window_set_default_size(GTK_WINDOW(popup), width, height);
	snprintf(tmp_char, sizeof(tmp_char),
		 "Default Settings for Sview");
	label = gtk_label_new(tmp_char);

	gtk_scrolled_window_set_policy(window,
				       GTK_POLICY_NEVER,
				       GTK_POLICY_AUTOMATIC);
	bin = GTK_BIN(&window->container);
	view = GTK_VIEWPORT(bin->child);
	bin = GTK_BIN(&view->bin);
	table = GTK_TABLE(bin->child);
	gtk_table_resize(table, SORTID_CNT, 2);

	gtk_table_set_homogeneous(table, false);

	for (i = 0; i < SORTID_CNT; i++) {
		while (display_data++) {
			if (display_data->id == -1)
				break;
			if (!display_data->name)
				continue;
			if (display_data->id != i)
				continue;

			_local_display_admin_edit(
				table, &tmp_config, &row,
				display_data);
			break;
		}
		display_data = display_data_defaults;
	}
	gtk_table_resize(table, row, 2);

	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
			   label, false, false, 0);
	if (window)
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox),
				   GTK_WIDGET(window), true, true, 0);
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));
	if (response == GTK_RESPONSE_OK) {
		tmp_char_ptr = g_strdup_printf(
			"Defaults updated successfully");
		if (global_edit_error)
			tmp_char_ptr = global_edit_error_msg;
		else if (!global_send_update_msg)
			tmp_char_ptr = g_strdup_printf(
				"No change detected.");
		else {
			int action = 0;
			gdk_window_set_cursor(main_window->window,
					      in_process_cursor);
			if (tmp_config.ruled_treeview
			    != working_sview_config.ruled_treeview) {
				/* get rid of each existing table */
				cluster_change_resv();
				cluster_change_part();
				cluster_change_job();
				cluster_change_node();
				cluster_change_front_end();
			} else if (tmp_config.grid_topological !=
				   working_sview_config.grid_topological) {
				apply_hidden_change = false;
				if (tmp_config.grid_topological) {
					default_sview_config.grid_topological =
						tmp_config.grid_topological;
					if (!g_switch_nodes_maps)
						rc = get_topo_conf();
					if (rc != SLURM_SUCCESS) {
						/*denied*/
						tmp_char_ptr = g_strdup_printf(
							"Valid topology not "
							"detected");
						tmp_config.grid_topological =
							false;
						goto denied_change;
					}
				}
				/*force fresh grid and
				 * node state check
				 * */
				FREE_NULL_LIST(grid_button_list);
				slurm_free_node_info_msg(g_node_info_ptr);
				g_node_info_ptr = NULL;
			}


			/*apply change*/

			memcpy(&default_sview_config, &tmp_config,
			       sizeof(sview_config_t));
			memcpy(&working_sview_config, &tmp_config,
			       sizeof(sview_config_t));

			/* set the current display to the default */
			gtk_toggle_action_set_active(
				default_sview_config.action_admin,
				working_sview_config.admin_mode);
			gtk_toggle_action_set_active(
				default_sview_config.action_ruled,
				working_sview_config.ruled_treeview);
			gtk_toggle_action_set_active(
				default_sview_config.action_grid,
				working_sview_config.show_grid);
			gtk_toggle_action_set_active(
				default_sview_config.action_hidden,
				working_sview_config.show_hidden);
			apply_hidden_change = true;
			gtk_toggle_action_set_active(
				default_sview_config.action_page_opts,
				working_sview_config.save_page_opts);
#ifdef GTK2_USE_RADIO_SET
			/* Since this value isn't a simple 0->n we
			   need to translate if we don't have the
			   newer gtk.
			*/
			action = working_sview_config.tab_pos;
#else
			/* If using older gtk we need to translate the
			   tab position to that of the position in the
			   radio list.
			*/
			action = _trans_tab_pos(working_sview_config.tab_pos);
#endif

			sview_radio_action_set_current_value(
				default_sview_config.action_tab, action);


			for (i=0; i<PAGE_CNT; i++) {
				if (main_display_data[i].id == -1)
					break;

				if (!main_display_data[i].name
				    || (i == TAB_PAGE))
					continue;

				toggle_tab_visiblity(NULL, main_display_data+i);
			}
			get_system_stats(main_grid_table);
			/******************************************/
			save_defaults(false);
		}
	denied_change:
		display_edit_note(tmp_char_ptr);
		g_free(tmp_char_ptr);
	}

	global_entry_changed = 0;

	gtk_widget_destroy(popup);

	if (main_window && main_window->window)
		gdk_window_set_cursor(main_window->window, NULL);

	return rc;
}
