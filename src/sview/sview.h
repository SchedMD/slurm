/****************************************************************************\
 *  sview.h - definitions used for sview data functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\****************************************************************************/

#ifndef _SVIEW_H
#define _SVIEW_H

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>
#include <inttypes.h>
#include <getopt.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "slurm/slurmdb.h"

#include "src/common/assoc_mgr.h"
#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/parse_time.h"
#include "src/common/select.h"
#include "src/common/slurmdb_defs.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/strlcpy.h"
#include "src/common/uid.h"
#include "src/common/xstring.h"

#include "gthread_helper.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP	0x100
#define OPT_LONG_USAGE	0x101
#define OPT_LONG_HIDE	0x102

#define POS_LOC 0
#define DEFAULT_ENTRY_LENGTH 500

#define MAKE_TOPO_1 -6
#define MAKE_TOPO_2 -5
#define MAKE_INIT -4
#define MAKE_DOWN -3
#define MAKE_BLACK -2
#define MAKE_WHITE -1

#define EXTRA_BASE  0x0000ffff
#define EXTRA_FLAGS 0xffff0000
#define EXTRA_NODES 0x00010000

enum { JOB_PAGE,
       PART_PAGE,
       RESV_PAGE,
       BB_PAGE,
       NODE_PAGE,
       FRONT_END_PAGE,
       SUBMIT_PAGE,
       ADMIN_PAGE,
       INFO_PAGE,
       TAB_PAGE,
       PAGE_CNT
};
enum { TAB_CLICKED,
       ROW_LEFT_CLICKED,
       ROW_CLICKED,
       FULL_CLICKED,
       POPUP_CLICKED
};

enum { ERROR_VIEW,
       INFO_VIEW
};

enum { STATUS_ADMIN_MODE,
       STATUS_REFRESH,
       STATUS_ADMIN_EDIT
};

enum { DISPLAY_NAME,
       DISPLAY_VALUE,
       DISPLAY_FONT
};

enum { EDIT_NONE,
       EDIT_ARRAY,
       EDIT_MODEL,
       EDIT_TEXTBOX,
       EDIT_COLOR
};

//typedef struct pages_options page_opts_t;
typedef struct display_data display_data_t;
typedef struct specific_info specific_info_t;
typedef struct popup_info popup_info_t;
typedef struct popup_positioner popup_positioner_t;

typedef enum {
	       CREATE_PARTITION = 1,
	       CREATE_RESERVATION,
	       SEARCH_JOB_ID = 10,
	       SEARCH_JOB_USER,
	       SEARCH_JOB_STATE,
	       SEARCH_PARTITION_NAME,
	       SEARCH_PARTITION_STATE,
	       SEARCH_NODE_NAME,
	       SEARCH_NODE_STATE,
	       SEARCH_RESERVATION_NAME,
} sview_search_type_t;

typedef struct {
	uint32_t x;
	uint32_t y;
	int cntr;
	int slider;
} popup_pos_t;

typedef struct  {
	char *nodes;
	bitstr_t *node_bitmap;
} switch_record_bitmaps_t;

typedef struct {
	List col_list;
	bool def_col_list;
	display_data_t *display_data;
	char *page_name;
} page_opts_t;

/* Input parameters */
typedef struct {
	GtkToggleAction *action_admin;
	GtkToggleAction *action_grid;
	GtkToggleAction *action_hidden;
	GtkToggleAction *action_page_opts;
	GtkToggleAction *action_gridtopo;
	GtkToggleAction *action_ruled;
	GtkRadioAction *action_tab;
	uint16_t button_size;
	uint16_t gap_size;
	bool admin_mode;
	uint16_t default_page;
	uint32_t fi_popup_width;
	uint32_t fi_popup_height;
	uint32_t grid_hori;
	bool grid_topological;
	uint32_t grid_vert;
	uint32_t grid_x_width;
	uint32_t main_width;
	uint32_t main_height;
	GtkWidget *page_check_widget[PAGE_CNT];
	page_opts_t page_opts[PAGE_CNT];
	bool page_visible[PAGE_CNT];
	uint16_t refresh_delay;
	bool ruled_treeview;
	bool show_grid;
	bool show_hidden;
	bool save_page_opts;
	uint16_t tab_pos;
	uint32_t convert_flags;
} sview_config_t;

struct display_data {
	GType type;
	int id;
	char *name;
	bool show;
	uint32_t extra;
	void (*refresh)     (GtkAction *action, gpointer user_data);
	GtkListStore *(*create_model)(int type);
	void (*admin_edit)  (GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data);
	void (*get_info)    (GtkTable *table, display_data_t *display_data);
	void (*specific)    (popup_info_t *popup_win);
	void (*set_menu)    (void *arg, void *arg2,
			     GtkTreePath *path, int type);
	gpointer user_data;
	gpointer button_list;
};

struct popup_positioner {
	int id;
	char *name;
	uint32_t width;
	uint32_t height;
};

typedef struct {
	sview_search_type_t search_type;
	gchar *cluster_name;
	gchar *gchar_data;
	int  int_data;
	int  int_data2;
} sview_search_info_t;

struct specific_info {
	int type; /* calling window type */
	int view;
	sview_search_info_t *search_info;
	char *title;
	GtkWidget *display_widget;
};

struct popup_info {
	display_data_t *display_data;
	GtkWidget *event_box;
	int force_refresh;
	int full_grid;
	List grid_button_list;
	List multi_button_list;
	GtkTable *grid_table;
	GtkTreeIter iter;
	GtkTreeModel *model;
	int *node_inx;
	int node_inx_id;
	bool not_found;
	GtkWidget *popup;
	int *running;
	int show_grid;
	specific_info_t *spec_info;
	GtkTable *table;
	int toggled;
	int type; /* window type */
};

typedef struct {
	GtkWidget *button;
	List button_list; /*list this grid_button exists in does not
			   * need to be freed, should only be a
			   * pointer to an existing list */
	char *color;
	int color_inx;
	int inx;
	GtkStateType last_state;
	char *node_name;
	uint32_t state;
	GtkTable *table;
	int table_x;
	int table_y;
#ifndef GTK2_USE_TOOLTIP
	GtkTooltips *tip;
#endif
	bool used;
} grid_button_t;

typedef struct {
	char *boot_time;
	char *color;
	GtkTreeIter iter_ptr;
	bool iter_set;
	char *node_name;
	node_info_t *node_ptr;
	int pos;
	char *reason;
	char *rack_mp;
	char *slurmd_start_time;
} sview_node_info_t;

typedef struct {
	display_data_t *display_data;
	List *button_list;
} signal_params_t;

extern sview_config_t default_sview_config;
extern sview_config_t working_sview_config;

extern int fini;
extern bool toggled;
extern bool force_refresh;
extern bool apply_hidden_change;
extern bool apply_partition_check;
extern List popup_list;
extern List grid_button_list;
extern List multi_button_list;
extern List signal_params_list;
extern bool global_entry_changed;
extern bool global_send_update_msg;
extern bool global_edit_error;
extern bool global_multi_error;
extern int global_error_code;
extern gchar *global_edit_error_msg;
extern GtkWidget *main_notebook;
extern GtkWidget *main_statusbar;
extern GtkWidget *main_window;
extern GtkTable *main_grid_table;
extern GMutex *sview_mutex;
extern int global_row_count;
extern gint last_event_x;
extern gint last_event_y;
extern GdkCursor* in_process_cursor;
extern char *sview_colors[];
extern int sview_colors_cnt;
extern int cluster_dims;
extern uint32_t cluster_flags;
extern List cluster_list;
extern front_end_info_msg_t *g_front_end_info_ptr;
extern job_info_msg_t *g_job_info_ptr;
extern node_info_msg_t *g_node_info_ptr;
extern partition_info_msg_t *g_part_info_ptr;
extern reserve_info_msg_t *g_resv_info_ptr;
extern burst_buffer_info_msg_t *g_bb_info_ptr;
extern slurm_ctl_conf_info_msg_t *g_ctl_info_ptr;
extern job_step_info_response_msg_t *g_step_info_ptr;
extern topo_info_response_msg_t *g_topo_info_msg_ptr;
extern switch_record_bitmaps_t *g_switch_nodes_maps;
extern popup_positioner_t main_popup_positioner[];
extern popup_pos_t popup_pos;
extern char *federation_name;

extern void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bg(int *start, int *end, int count, int set);
extern void print_grid(int dir);

//sview.c
extern void refresh_main(GtkAction *action, gpointer user_data);
extern void toggle_tab_visiblity(GtkToggleButton *toggle_button,
				 display_data_t *display_data);
extern gboolean tab_pressed(GtkWidget *widget, GdkEventButton *event,
			    display_data_t *display_data);
extern void close_tab(GtkWidget *widget, GdkEventButton *event,
		      display_data_t *display_data);

//popups.c
extern void create_config_popup(GtkAction *action, gpointer user_data);
extern void create_create_popup(GtkAction *action, gpointer user_data);
extern void create_dbconfig_popup(GtkAction *action, gpointer user_data);
extern void create_daemon_popup(GtkAction *action, gpointer user_data);
extern void create_search_popup(GtkAction *action, gpointer user_data);
extern void change_refresh_popup(GtkAction *action, gpointer user_data);
extern void change_grid_popup(GtkAction *action, gpointer user_data);
extern void about_popup(GtkAction *action, gpointer user_data);
extern void usage_popup(GtkAction *action, gpointer user_data);
extern void display_fed_disabled_popup(const char *title);

//grid.c
extern void destroy_grid_button(void *arg);
extern grid_button_t *create_grid_button_from_another(
	grid_button_t *grid_button, char *name, int color_inx);
/* do not free the char * from this function it is static */
extern void change_grid_color(List button_list, int start, int end,
			      int color_inx, bool change_unused,
			      enum node_states state_override);
extern void change_grid_color_array(List button_list, int array_len,
				    int *color_inx, bool *color_set_flag,
				    bool only_change_unused,
				    enum node_states state_override);
extern void highlight_grid(GtkTreeView *tree_view,
			   int node_inx_id, int color_inx_id, List button_list);
extern void highlight_grid_range(int start, int end, List button_list);
extern void set_grid_used(List button_list, int start, int end,
			  bool used, bool reset_highlight);
extern void get_button_list_from_main(List *button_list, int start, int end,
				      int color_inx);
extern List copy_main_button_list(int initial_color);
extern void put_buttons_in_table(GtkTable *table, List button_list);
extern int get_system_stats(GtkTable *table);
extern int setup_grid_table(GtkTable *table, List button_list, List node_list);
extern void sview_init_grid(bool reset_highlight);
extern void sview_clear_unused_grid(List button_list, int color_inx);
extern void setup_popup_grid_list(popup_info_t *popup_win);
extern void post_setup_popup_grid_list(popup_info_t *popup_win);

// part_info.c
extern GtkWidget *create_part_entry(update_part_msg_t *part_msg,
				    GtkTreeModel *model, GtkTreeIter *iter);
extern bool check_part_includes_node(int node_dx);
extern void refresh_part(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_part(int type);
extern void admin_edit_part(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data);
extern int get_new_info_part(partition_info_msg_t **part_ptr, int force);
extern void get_info_part(GtkTable *table, display_data_t *display_data);
extern void specific_info_part(popup_info_t *popup_win);
extern void set_menus_part(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void select_admin_partitions(GtkTreeModel *model, GtkTreeIter *iter,
				    display_data_t *display_data,
				    GtkTreeView *treeview);
extern void admin_part(GtkTreeModel *model, GtkTreeIter *iter, char *type);
extern void cluster_change_part(void);

// accnt_info.c
extern void refresh_accnt(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_accnt(int type);
extern void admin_edit_accnt(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data);
extern void specific_info_accnt(popup_info_t *popup_win);

// front_end_info.c
extern void admin_edit_front_end(GtkCellRendererText *cell,
				 const char *path_string,
				 const char *new_text, gpointer data);
extern void cluster_change_front_end(void);
extern GtkListStore *create_model_front_end(int type);
extern void get_info_front_end(GtkTable *table, display_data_t *display_data);
extern int  get_new_info_front_end(front_end_info_msg_t **info_ptr, int force);
extern void popup_all_front_end(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void refresh_front_end(GtkAction *action, gpointer user_data);
extern void select_admin_front_end(GtkTreeModel *model, GtkTreeIter *iter,
				  display_data_t *display_data,
				  GtkTreeView *treeview);
extern void set_menus_front_end(void *arg, void *arg2, GtkTreePath *path,
				int type);
extern void specific_info_front_end(popup_info_t *popup_win);

// job_info.c
extern void refresh_job(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_job(int type);
extern void admin_edit_job(GtkCellRendererText *cell,
			   const char *path_string,
			   const char *new_text,
			   gpointer data);
extern int get_new_info_job(job_info_msg_t **info_ptr, int force);
extern int get_new_info_job_step(job_step_info_response_msg_t **info_ptr,
				 int force);
extern void get_info_job(GtkTable *table, display_data_t *display_data);
extern void specific_info_job(popup_info_t *popup_win);
extern void set_menus_job(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_job(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void admin_job(GtkTreeModel *model, GtkTreeIter *iter, char *type,
		      GtkTreeView *treeview);
extern void cluster_change_job(void);

// node_info.c
extern void refresh_node(GtkAction *action, gpointer user_data);
/* don't destroy the list from this function */
extern List create_node_info_list(node_info_msg_t *node_info_ptr,
				  bool by_partition);
extern int update_active_features_node(GtkDialog *dialog, const char *nodelist,
				      const char *old_features);
extern int update_avail_features_node(GtkDialog *dialog, const char *nodelist,
				      const char *old_features);
extern int update_state_node(GtkDialog *dialog,
			     const char *nodelist, const char *type);
extern GtkListStore *create_model_node(int type);
extern void select_admin_nodes(GtkTreeModel *model, GtkTreeIter *iter,
			       display_data_t *display_data, uint32_t node_col,
			       GtkTreeView *treeview);
extern void admin_edit_node(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data);
extern int get_new_info_node(node_info_msg_t **info_ptr, int force);
extern void get_info_node(GtkTable *table, display_data_t *display_data);
extern void specific_info_node(popup_info_t *popup_win);
extern void set_menus_node(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_node(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void popup_all_node_name(char *name, int id, char *cluster_name);
extern void admin_menu_node_name(char *name, GdkEventButton *event);
extern void admin_node(GtkTreeModel *model, GtkTreeIter *iter, char *type);
extern void admin_node_name(char *name, char *old_value, char *type);
extern void cluster_change_node(void);

// resv_info.c
extern GtkWidget *create_resv_entry(resv_desc_msg_t *resv_msg,
				    GtkTreeModel *model, GtkTreeIter *iter);
extern void refresh_resv(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_resv(int type);
extern void admin_edit_resv(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data);
extern int get_new_info_resv(reserve_info_msg_t **info_ptr, int force);
extern void get_info_resv(GtkTable *table, display_data_t *display_data);
extern void specific_info_resv(popup_info_t *popup_win);
extern void set_menus_resv(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_resv(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void select_admin_resv(GtkTreeModel *model, GtkTreeIter *iter,
			      display_data_t *display_data,
			      GtkTreeView *treeview);
extern void cluster_change_resv(void);


// submit_info.c
extern void get_info_submit(GtkTable *table, display_data_t *display_data);
extern void set_menus_submit(void *arg, void *arg2,
			     GtkTreePath *path, int type);

// config_info.c
extern int get_new_info_config(slurm_ctl_conf_info_msg_t **info_ptr);

// common.c
extern char * replspace (char *str);
extern char * replus (char *str);
extern void set_page_opts(int tab, display_data_t *display_data,
			  int count, char* initial_opts);
extern void free_switch_nodes_maps(switch_record_bitmaps_t
				   *g_switch_nodes_maps);
extern int get_topo_conf(void);
extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path);
extern int find_col(display_data_t *display_data, int type);
extern const char *find_col_name(display_data_t *display_data, int type);
extern void load_header(GtkTreeView *tree_view, display_data_t *display_data);
extern void make_fields_menu(popup_info_t *popup_win, GtkMenu *menu,
			     display_data_t *display_data, int count);
extern void make_options_menu(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkMenu *menu, display_data_t *display_data);
extern GtkScrolledWindow *create_scrolled_window(void);
extern GtkWidget *create_entry(void);
extern void create_page(GtkNotebook *notebook, display_data_t *display_data);
extern GtkTreeView *create_treeview(display_data_t *local, List *button_list);
extern GtkTreeView *create_treeview_2cols_attach_to_table(GtkTable *table);
extern void create_treestore(GtkTreeView *tree_view,
			     display_data_t *display_data, int count,
			     int sort_column, int color_column);

extern gboolean right_button_pressed(GtkTreeView *tree_view, GtkTreePath *path,
				     GdkEventButton *event,
				     const signal_params_t *signal_params,
				     int type);
extern gboolean left_button_pressed(GtkTreeView *tree_view,
				    GtkTreePath *path,
				    const signal_params_t *signal_params,
				    GdkEventButton *event);
extern gboolean row_activated(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      const signal_params_t *signal_params);
extern gboolean row_expander(GtkTreeView *tree_view,
			     gboolean arg1, gboolean arg2,
			     const signal_params_t *signal_params);
extern gboolean row_expand(GtkTreeView *tree_view,  GtkTreeIter *iter,
			   GtkTreePath *path,
			   const signal_params_t *signal_params);
extern gboolean row_clicked(GtkTreeView *tree_view, GdkEventButton *event,
			    const signal_params_t *signal_params);
extern gboolean key_pressed(GtkTreeView *tree_view, GdkEventKey *event,
			    const signal_params_t *signal_params);
extern gboolean focus_in(GtkTreeView *tree_view, GdkEventButton *event,
			 const signal_params_t *signal_params);
extern gboolean key_released(GtkTreeView *tree_view, GdkEventKey *event,
			     const signal_params_t *signal_params);
extern popup_info_t *create_popup_info(int type, int dest_type, char *title);
extern void setup_popup_info(popup_info_t *popup_win,
			     display_data_t *display_data,
			     int cnt);
extern void redo_popup(GtkWidget *widget, GdkEventButton *event,
		       popup_info_t *popup_win);

extern void destroy_search_info(void *arg);
extern void destroy_specific_info(void *arg);
extern void destroy_popup_info(void *arg);
extern void destroy_signal_params(void *arg);

extern gboolean delete_popup(GtkWidget *widget, GtkWidget *event, char *title);
extern gboolean delete_popups(void);
extern void *popup_thr(popup_info_t *popup_win);
extern void set_for_update(GtkTreeModel *model, int updated);
extern void remove_old(GtkTreeModel *model, int updated);
extern GtkWidget *create_pulldown_combo(display_data_t *display_data);
extern char *str_tolower(char *upper_str);
extern char *get_reason(void);
extern void display_admin_edit(GtkTable *table, void *type_msg, int *row,
			       GtkTreeModel *model, GtkTreeIter *iter,
			       display_data_t *display_data,
			       GCallback changed_callback,
			       GCallback focus_callback,
			       void (*set_active)(
				       GtkComboBox *combo,
				       GtkTreeModel *model, GtkTreeIter *iter,
				       int type));
extern void display_edit_note(char *edit_note);
extern void add_display_treestore_line(int update,
				       GtkTreeStore *treestore,
				       GtkTreeIter *iter,
				       const char *name,
				       const char *value);
extern void add_display_treestore_line_with_font(
	int update,
	GtkTreeStore *treestore,
	GtkTreeIter *iter,
	const char *name, char *value,
	char *font);
extern void sview_widget_modify_bg(GtkWidget *widget, GtkStateType state,
				   const GdkColor color);
extern void sview_radio_action_set_current_value(GtkRadioAction *action,
						 gint current_value);
extern char *page_to_str(int page);
extern char *tab_pos_to_str(int tab_pos);
extern char *visible_to_str(sview_config_t *sview_config);
extern gboolean entry_changed(GtkWidget *widget, void *msg);
extern void select_admin_common(GtkTreeModel *model, GtkTreeIter *iter,
				display_data_t *display_data,
				GtkTreeView *treeview,
				uint32_t node_col,
				void (*process_each)(GtkTreeModel *model,
						     GtkTreePath *path,
						     GtkTreeIter *iter,
						     gpointer userdata));

// defaults.c
extern int load_defaults(void);
extern int save_defaults(bool final_save);
extern GtkListStore *create_model_defaults(int type);
extern int configure_defaults(void);

//bb_info.c
extern void refresh_bb(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_bb(int type);
extern void admin_edit_bb(GtkCellRendererText *cell,
			  const char *path_string,
			  const char *new_text,
			  gpointer data);
extern void get_info_bb(GtkTable *table, display_data_t *display_data);
extern void specific_info_bb(popup_info_t *popup_win);
extern void set_menus_bb(void *arg, void *arg2, GtkTreePath *path, int type);
extern void cluster_change_bb(void);
extern void popup_all_bb(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void select_admin_bb(GtkTreeModel *model, GtkTreeIter *iter,
			    display_data_t *display_data,
			    GtkTreeView *treeview);

#endif
