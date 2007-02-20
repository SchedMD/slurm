/****************************************************************************\
 *  sview.h - definitions used for sview data functions
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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
#include <gtk/gtk.h>

#include "src/common/bitstring.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/macros.h"
#include "src/plugins/select/bluegene/block_allocator/block_allocator.h"
#include "src/common/slurm_protocol_api.h"

#include "src/plugins/select/bluegene/wrap_rm_api.h"

/* getopt_long options, integers but not characters */
#define OPT_LONG_HELP	0x100
#define OPT_LONG_USAGE	0x101
#define OPT_LONG_HIDE	0x102

#define POS_LOC 0

enum { JOB_PAGE, 
       STEP_PAGE, 
       PART_PAGE, 
       NODE_PAGE, 
       BLOCK_PAGE, 
       SUBMIT_PAGE,
       INFO_PAGE,
       PAGE_CNT 
};
enum { TAB_CLICKED,
       ROW_CLICKED,
       POPUP_CLICKED
};

enum { ERROR_VIEW,
       INFO_VIEW
};

enum { STATUS_ADMIN_MODE,
       STATUS_REFRESH,
       STATUS_ADMIN_EDIT
};

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
	void (*set_menu)    (void *arg, GtkTreePath *path,
			     GtkMenu *menu, int type);
	gpointer user_data;
};

struct specific_info {
	int type; /* calling window type */
	int view;
	void *data;
	char *title;
	GtkWidget *display_widget;	
};

struct popup_info {
	int type; /* window type */
	int toggled;
	int force_refresh;
	int *running;
	GtkWidget *popup;
	GtkWidget *event_box;
	GtkWidget *button;
	GtkTable *table;
	specific_info_t *spec_info;
	display_data_t *display_data;
};

typedef struct {
	int jobid;
	int stepid;
} job_step_num_t;

extern sview_parameters_t params;
extern int text_line_cnt;

extern void parse_command_line(int argc, char *argv[]);

extern ba_system_t *ba_system_ptr;
extern int quiet_flag;
extern bool toggled;
extern bool force_refresh;
extern List popup_list;
extern int global_sleep_time;
extern bool admin_mode;
extern GtkWidget *main_statusbar;
extern GtkWidget *main_window;
extern GStaticMutex sview_mutex;	

extern void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bg(int *start, int *end, int count, int set);
extern void print_grid(int dir);

extern void parse_command_line(int argc, char *argv[]);
extern void print_date();
extern void clear_window(WINDOW *win);

//sview.c
extern void refresh_main(GtkAction *action, gpointer user_data);
extern void tab_pressed(GtkWidget *widget, GdkEventButton *event, 
			const display_data_t *display_data);

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
extern void set_menus_part(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type);
extern void popup_all_part(GtkTreeModel *model, GtkTreeIter *iter, int id);

// block_info.c
extern void refresh_block(GtkAction *action, gpointer user_data);
extern GtkListStore *create_model_block(int type);
extern void admin_edit_block(GtkCellRendererText *cell,
			     const char *path_string,
			     const char *new_text,
			     gpointer data);
extern int get_new_info_node_select(node_select_info_msg_t **node_select_ptr,
				    int force);
extern void get_info_block(GtkTable *table, display_data_t *display_data);
extern void specific_info_block(popup_info_t *popup_win);
extern void set_menus_block(void *arg, GtkTreePath *path, 
			    GtkMenu *menu, int type);
extern void popup_all_block(GtkTreeModel *model, GtkTreeIter *iter, int id);

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
extern void set_menus_job(void *arg, GtkTreePath *path, 
			  GtkMenu *menu, int type);
extern void popup_all_job(GtkTreeModel *model, GtkTreeIter *iter, int id);

// node_info.c
extern void refresh_node(GtkAction *action, gpointer user_data);
extern int update_state_node(GtkTreeStore *treestore, GtkTreeIter *iter, 
			     int text_column, int num_column,
			     const char *new_text,
			     update_node_msg_t *node_msg);
extern GtkListStore *create_model_node(int type);
extern void admin_edit_node(GtkCellRendererText *cell,
			    const char *path_string,
			    const char *new_text,
			    gpointer data);
extern int get_new_info_node(node_info_msg_t **info_ptr, int force);
extern void get_info_node(GtkTable *table, display_data_t *display_data);
extern void specific_info_node(popup_info_t *popup_win);
extern void set_menus_node(void *arg, GtkTreePath *path, 
			   GtkMenu *menu, int type);
extern void popup_all_node(GtkTreeModel *model, GtkTreeIter *iter, int id);

// submit_info.c
extern void get_info_submit(GtkTable *table, display_data_t *display_data);
extern void set_menus_submit(void *arg, GtkTreePath *path, 
			     GtkMenu *menu, int type);
// common.c
extern void snprint_time(char *buf, size_t buf_size, time_t time);
extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path);
extern void load_header(GtkTreeView *tree_view, display_data_t *display_data);
extern void make_fields_menu(GtkMenu *menu, display_data_t *display_data);
extern void make_popup_fields_menu(popup_info_t *popup_win, GtkMenu *men);
extern void make_options_menu(GtkTreeView *tree_view, GtkTreePath *path, 
			      GtkMenu *menu, display_data_t *display_data);
extern GtkScrolledWindow *create_scrolled_window();
extern void create_page(GtkNotebook *notebook, display_data_t *display_data);
extern GtkTreeView *create_treeview(display_data_t *local);
extern GtkTreeStore *create_treestore(GtkTreeView *tree_view, 
				      display_data_t *display_data, int count);
extern void right_button_pressed(GtkTreeView *tree_view, GtkTreePath *path, 
				 GdkEventButton *event, 
				 const display_data_t *display_data,
				 int type);
extern gboolean row_clicked(GtkTreeView *tree_view, GdkEventButton *event, 
			    const display_data_t *display_data);
extern popup_info_t *create_popup_info(int type, int dest_type, char *title);
extern void setup_popup_info(popup_info_t *popup_win, 
			     display_data_t *display_data, 
			     int cnt);
extern void redo_popup(GtkWidget *widget, GdkEventButton *event, 
		       popup_info_t *popup_win);
extern void destroy_specific_info(void *arg);
extern void destroy_popup_info(void *arg);
extern gboolean delete_popup(GtkWidget *widget, GtkWidget *event, char *title);
extern void *popup_thr(popup_info_t *popup_win);
extern void remove_old(GtkTreeModel *model, int updated);
extern GtkWidget *create_pulldown_combo(display_data_t *display_data,
					int count);
extern char *str_tolower(char *upper_str);
extern char *get_reason();
extern void display_edit_note(char *edit_note);

#endif
