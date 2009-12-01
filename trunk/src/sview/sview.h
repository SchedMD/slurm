/****************************************************************************\
 *  sview.h - definitions used for sview data functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
\****************************************************************************/

#ifndef _SVIEW_H
#define _SVIEW_H

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_INTTYPES_H
#  include <inttypes.h>
#else				/* !HAVE_INTTYPES_H */
#  if HAVE_STDINT_H
#    include <stdint.h>
#  endif
#endif				/* HAVE_INTTYPES_H */

#if HAVE_GETOPT_H
#  include <getopt.h>
#else
#  include "src/common/getopt.h"
#endif

#include <stdlib.h>
#include <pwd.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#if defined(HAVE_AIX)
/* AIX defines a func_data macro which conflicts with func_data
 * variable names in the gtk.h headers */
#  undef func_data
#  include <gtk/gtk.h>
#else
#  include <gtk/gtk.h>
#endif

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/plugins/select/bluegene/block_allocator/block_allocator.h"
#include "src/plugins/select/bluegene/plugin/bluegene.h"
//#include "src/plugins/select/bluegene/wrap_rm_api.h"

#include "src/common/slurm_protocol_api.h"
#include "src/common/slurm_protocol_defs.h"

#include "src/plugins/select/bluegene/wrap_rm_api.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP	0x100
#define OPT_LONG_USAGE	0x101
#define OPT_LONG_HIDE	0x102

#define POS_LOC 0
#define DEFAULT_ENTRY_LENGTH 500

#define MAKE_INIT -4
#define MAKE_DOWN -3
#define MAKE_BLACK -2
#define MAKE_WHITE -1

enum { JOB_PAGE,
       STEP_PAGE,
       PART_PAGE,
       NODE_PAGE,
       BLOCK_PAGE,
       RESV_PAGE,
       SUBMIT_PAGE,
       ADMIN_PAGE,
       INFO_PAGE,
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
       EDIT_MODEL,
       EDIT_TEXTBOX
};

typedef enum { SEARCH_JOB_ID = 1,
	       SEARCH_JOB_USER,
	       SEARCH_JOB_STATE,
	       SEARCH_BLOCK_NAME,
	       SEARCH_BLOCK_NODENAME,
	       SEARCH_BLOCK_SIZE,
	       SEARCH_BLOCK_STATE,
	       SEARCH_PARTITION_NAME,
	       SEARCH_PARTITION_STATE,
	       SEARCH_NODE_NAME,
	       SEARCH_NODE_STATE,
	       SEARCH_RESERVATION_NAME,
} sview_search_type_t;


/* Input parameters */
typedef struct {
	bool all_flag;
	bool no_header;

	char *format;
	char *sort;
	char *states;

	int iterate;
	int verbose;
	int display;

	bool long_output;
	bool commandline;
	bool parse;

	char *nodes;
	char *partition;

	int node_field_size;

} sview_parameters_t;

typedef struct display_data display_data_t;
typedef struct specific_info specific_info_t;
typedef struct popup_info popup_info_t;

struct display_data {
	GType type;
	int id;
	char *name;
	bool show;
	int extra;
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

typedef struct {
	sview_search_type_t search_type;
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
	int state;
	GtkTable *table;
	int table_x;
	int table_y;
#ifndef GTK2_USE_TOOLTIP
	GtkTooltips *tip;
#endif
	bool used;
} grid_button_t;

typedef struct {
	node_info_t *node_ptr;
	char *color;
	int pos;
} sview_node_info_t;

typedef struct {
	display_data_t *display_data;
	List *button_list;
} signal_params_t;


extern sview_parameters_t params;
extern int text_line_cnt;

extern void parse_command_line(int argc, char *argv[]);

extern int fini;
extern ba_system_t *ba_system_ptr;
extern int quiet_flag;
extern bool toggled;
extern bool force_refresh;
extern List popup_list;
extern List grid_button_list;
extern List signal_params_list;
extern int global_sleep_time;
extern bool admin_mode;
extern GtkWidget *main_statusbar;
extern GtkWidget *main_window;
extern GtkTable *main_grid_table;
extern GStaticMutex sview_mutex;
extern int cpus_per_node;
extern int g_node_scaling;
extern int grid_speedup;
extern char *sview_colors[];
extern int sview_colors_cnt;

extern void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bg(int *start, int *end, int count, int set);
extern void print_grid(int dir);

extern void parse_command_line(int argc, char *argv[]);
extern void print_date();

//sview.c
extern void refresh_main(GtkAction *action, gpointer user_data);
extern void tab_pressed(GtkWidget *widget, GdkEventButton *event,
			display_data_t *display_data);

//popups.c
extern void create_config_popup(GtkAction *action, gpointer user_data);
extern void create_daemon_popup(GtkAction *action, gpointer user_data);
extern void create_search_popup(GtkAction *action, gpointer user_data);
extern void change_refresh_popup(GtkAction *action, gpointer user_data);

//grid.c
extern void destroy_grid_button(void *arg);
extern grid_button_t *create_grid_button_from_another(
	grid_button_t *grid_button, char *name, int color_inx);
/* do not free the char * from this function it is static */
extern char *change_grid_color(List button_list, int start, int end,
			       int color_inx, bool change_unused,
			       enum node_states state_override);
extern void highlight_grid(GtkTreeView *tree_view, GtkTreePath *path,
			   int node_inx_id, List button_list);
extern void highlight_grid_range(int start, int end, List button_list);
extern void set_grid_used(List button_list, int start, int end,
			  bool used, bool reset_highlight);
extern void get_button_list_from_main(List *button_list, int start, int end,
				      int color_inx);
extern List copy_main_button_list(int initial_color);
#ifdef HAVE_BG
extern void add_extra_bluegene_buttons(List *button_list, int inx,
				       int *color_inx);
#endif
extern void add_extra_cr_buttons(List *button_list, node_info_t *node_ptr);
extern void put_buttons_in_table(GtkTable *table, List button_list);
extern int get_system_stats(GtkTable *table);
extern int setup_grid_table(GtkTable *table, List button_list, List node_list);
extern void sview_init_grid(bool reset_highlight);
extern void sview_clear_unused_grid(List button_list, int color_inx);
extern void setup_popup_grid_list(popup_info_t *popup_win);
extern void post_setup_popup_grid_list(popup_info_t *popup_win);

// part_info.c
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
extern void admin_part(GtkTreeModel *model, GtkTreeIter *iter, char *type);

// block_info.c
extern void refresh_block(GtkAction *action, gpointer user_data);
extern int update_state_block(GtkDialog *dialog,
			      const char *blockid, const char *type);
extern GtkListStore *create_model_block(int type);
extern void admin_edit_block(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data);
extern int get_new_info_block(block_info_msg_t **block_ptr, int force);
extern void get_info_block(GtkTable *table, display_data_t *display_data);
extern void specific_info_block(popup_info_t *popup_win);
extern void set_menus_block(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_block(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void admin_block(GtkTreeModel *model, GtkTreeIter *iter, char *type);

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
extern void admin_job(GtkTreeModel *model, GtkTreeIter *iter, char *type);

// node_info.c
extern void refresh_node(GtkAction *action, gpointer user_data);
/* don't destroy the list from this function */
extern List create_node_info_list(node_info_msg_t *node_info_ptr, int changed);
extern int update_features_node(GtkDialog *dialog, const char *nodelist,
				const char *old_features);
extern int update_state_node(GtkDialog *dialog,
			     const char *nodelist, const char *type);
extern GtkListStore *create_model_node(int type);
extern void admin_edit_node(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data);
extern int get_new_info_node(node_info_msg_t **info_ptr, int force);
extern void get_info_node(GtkTable *table, display_data_t *display_data);
extern void specific_info_node(popup_info_t *popup_win);
extern void set_menus_node(void *arg, void *arg2, GtkTreePath *path, int type);
extern void popup_all_node(GtkTreeModel *model, GtkTreeIter *iter, int id);
extern void popup_all_node_name(char *name, int id);
extern void admin_menu_node_name(char *name, GdkEventButton *event);
extern void admin_node(GtkTreeModel *model, GtkTreeIter *iter, char *type);
extern void admin_node_name(char *name, char *old_features, char *type);

// resv_info.c
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
extern void admin_resv(GtkTreeModel *model, GtkTreeIter *iter, char *type);


// submit_info.c
extern void get_info_submit(GtkTable *table, display_data_t *display_data);
extern void set_menus_submit(void *arg, void *arg2,
			     GtkTreePath *path, int type);
// common.c
extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path);
extern int find_col(display_data_t *display_data, int type);
extern const char *find_col_name(display_data_t *display_data, int type);
extern void load_header(GtkTreeView *tree_view, display_data_t *display_data);
extern void make_fields_menu(popup_info_t *popup_win, GtkMenu *menu,
			     display_data_t *display_data, int count);
extern void make_options_menu(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkMenu *menu, display_data_t *display_data);
extern GtkScrolledWindow *create_scrolled_window();
extern GtkWidget *create_entry();
extern void create_page(GtkNotebook *notebook, display_data_t *display_data);
extern GtkTreeView *create_treeview(display_data_t *local, List *button_list);
extern GtkTreeView *create_treeview_2cols_attach_to_table(GtkTable *table);
extern GtkTreeStore *create_treestore(GtkTreeView *tree_view,
				      display_data_t *display_data,
				      int count, int sort_column,
				      int color_column);

extern gboolean right_button_pressed(GtkTreeView *tree_view, GtkTreePath *path,
				     GdkEventButton *event,
				     const signal_params_t *signal_params,
				     int type);
extern gboolean left_button_pressed(GtkTreeView *tree_view,
				    GtkTreePath *path,
				    const signal_params_t *signal_params);
extern gboolean row_activated(GtkTreeView *tree_view, GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      const signal_params_t *signal_params);
extern gboolean row_clicked(GtkTreeView *tree_view, GdkEventButton *event,
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
extern void *popup_thr(popup_info_t *popup_win);
extern void remove_old(GtkTreeModel *model, int updated);
extern GtkWidget *create_pulldown_combo(display_data_t *display_data,
					int count);
extern char *str_tolower(char *upper_str);
extern char *get_reason();
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
				       const char *name, char *value);
extern void add_display_treestore_line_with_font(
	int update,
	GtkTreeStore *treestore,
	GtkTreeIter *iter,
	const char *name, char *value,
	char *font);
extern void sview_widget_modify_bg(GtkWidget *widget, GtkStateType state,
				   const GdkColor color);
#endif
