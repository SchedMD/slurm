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

enum { JOBS, SLURMPART, BGPART, COMMANDS };
enum { PARTITION_PAGE, 
       JOB_PAGE, 
       NODE_PAGE, 
       BLOCK_PAGE, 
       JOB_SUBMIT_PAGE,
       ADMIN_PAGE,
       PAGE_CNT};

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

typedef struct {
	int id;
	char *name;
	bool show;
	int extra;
	void (*get_info)    (GtkTable *table, void *display_data);
	void (*set_fields)  (GtkMenu *menu);
	void (*row_clicked) (GtkTreeView *tree_view,
			     GtkTreePath *path,
			     GtkTreeViewColumn *column,
			     gpointer user_data);
	gpointer user_data;
} display_data_t;

extern sview_parameters_t params;
extern int text_line_cnt;

extern void parse_command_line(int argc, char *argv[]);

extern ba_system_t *ba_system_ptr;
extern int quiet_flag;
extern bool toggled;


extern void init_grid(node_info_msg_t *node_info_ptr);
extern int set_grid(int start, int end, int count);
extern int set_grid_bg(int *start, int *end, int count, int set);
extern void print_grid(int dir);

extern void parse_command_line(int argc, char *argv[]);
extern void print_date();
extern void clear_window(WINDOW *win);

//sview.c
extern void refresh_page(GtkAction *action,
			 gpointer user_data);
extern void tab_pressed(GtkWidget *widget, GdkEventButton *event, 
			const display_data_t *display_data);

// part_info.c
extern void get_info_part(GtkTable *table, display_data_t *display_data);
extern void set_fields_part(GtkMenu *menu);
extern void row_clicked_part(GtkTreeView *tree_view,
			     GtkTreePath *path,
			     GtkTreeViewColumn *column,
			     gpointer user_data);

// block_info.c
extern void get_info_block(GtkTable *table, display_data_t *display_data);
extern void set_fields_block(GtkMenu *menu);
extern void row_clicked_block(GtkTreeView *tree_view,
			      GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      gpointer user_data);

// job_info.c
extern void get_info_job(GtkTable *table, display_data_t *display_data);
extern void set_fields_job(GtkMenu *menu);
extern void row_clicked_job(GtkTreeView *tree_view,
			    GtkTreePath *path,
			    GtkTreeViewColumn *column,
			    gpointer user_data);

// admin_info.c
extern void get_info_admin(GtkTable *table, display_data_t *display_data);
extern void set_fields_admin(GtkMenu *menu);
extern void row_clicked_admin(GtkTreeView *tree_view,
			      GtkTreePath *path,
			      GtkTreeViewColumn *column,
			      gpointer user_data);

// node_info.c
extern void get_info_node(GtkTable *table, display_data_t *display_data);
extern void set_fields_node(GtkMenu *menu);
extern void row_clicked_node(GtkTreeView *tree_view,
			     GtkTreePath *path,
			     GtkTreeViewColumn *column,
			     gpointer user_data);

// submit_info.c
extern void get_info_submit(GtkTable *table, display_data_t *display_data);
extern void set_fields_submit(GtkMenu *menu);
extern void row_clicked_submit(GtkTreeView *tree_view,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       gpointer user_data);

// common.c
extern void snprint_time(char *buf, size_t buf_size, time_t time);
extern int get_row_number(GtkTreeView *tree_view, GtkTreePath *path);
extern GtkListStore *create_liststore(display_data_t *display_data, int count);
extern void load_header(GtkTreeView *tree_view, display_data_t *display_data);
extern void make_fields_menu(GtkMenu *menu, display_data_t *display_data);
extern void create_page(GtkNotebook *notebook, display_data_t *display_data);
extern void right_button_pressed(GtkWidget *widget, GdkEventButton *event, 
				 const display_data_t *display_data);
extern void button_pressed(GtkTreeView *tree_view, GdkEventButton *event, 
			   const display_data_t *display_data);
#endif
