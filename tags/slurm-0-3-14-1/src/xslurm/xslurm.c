/*****************************************************************************\
 *  xslurm - user tool to view SLURM state and manage their SLURM jobs.
 *****************************************************************************
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Moe Jette <jette1@llnl.gov>
 *  UCRL-CODE-2002-040.
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

/*
Build and test procedure:

gcc -I /usr/include/gtk-1.2 -I /usr/include/glib-1.2 -I /usr/lib/glib/include -c xslurm.c
gcc -lgtk -lgdk -lglib -lgdk_pixbuf -lgobject-2.0 -lgmodule-1.2 -lm -lslurm -L/usr/X11R6/lib -lX11 -lXext -o xslurm xslurm.o
./xslurm
*/

#include <pwd.h>
#include <sys/types.h>

#include <slurm/slurm.h>
#include <gtk/gtk.h>

static inline void 	_cat_if_room(char *str1, char *str2, int size1);
static void		_complete(GtkWidget *widget, gpointer data);
static void		_help(GtkWidget *widget, gpointer data);
static void 		_help_complete(GtkWidget *widget, gpointer data);
static GtkWidget *	_make_button_widget(void);
static GtkWidget *	_make_job_table(void);
static GtkWidget *	_make_job_widget(void);
static GtkWidget *	_make_part_table(void);
static GtkWidget *	_make_part_widget(void);
static void 		_part_details(GtkWidget *widget, gpointer data);
static inline char *	_root_str(uint16_t root);
static inline char *	_shared_str(uint16_t shared);
static void		_sprint_part_details(char *string, int size, 
					     partition_info_t *part_ptr);
static void		_submit(GtkWidget *widget, gpointer data);
static void		_refresh(GtkWidget *widget, gpointer data);

GtkWidget *help_widget = NULL;	/* help window widget */

partition_info_msg_t *old_part_info_ptr = NULL;

/*
 * Main window set-up and support
 */

int main (int argc, char **argv)
{
	GtkWidget *window;
        GtkWidget *box_vert, *box_parts, *box_jobs, *box_bottom;

	/* main window set-up */
	gtk_init(&argc, &argv);
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "xslurm");
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			 GTK_SIGNAL_FUNC(_complete), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			 GTK_SIGNAL_FUNC(_complete), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);
	gtk_widget_set_usize(window, 500, 300);

	/* Vertical box container */
	box_vert = gtk_vbox_new(FALSE, 0);

	/* Partition state info */
	box_parts = _make_part_widget();
	gtk_box_pack_start(GTK_BOX(box_vert), box_parts, TRUE, TRUE, 0);
	gtk_widget_show(box_parts);

	/* Job state info */
	box_jobs = _make_job_widget();
	gtk_box_pack_start(GTK_BOX(box_vert), box_jobs, TRUE, TRUE, 0);
	gtk_widget_show(box_jobs);

	/* bottom of window button set-up */
	box_bottom = _make_button_widget();
	gtk_box_pack_start(GTK_BOX(box_vert), box_bottom, FALSE, FALSE, 0);
	gtk_widget_show(box_bottom);

	gtk_container_add(GTK_CONTAINER(window), box_vert);
	gtk_widget_show(box_vert);
	gtk_widget_show(window);

	gtk_main();

	return 0;
}


/*
 * Partition info set-up and support
 */

/* _make_part_widget - build scrollable list of partitions */
static GtkWidget *_make_part_widget(void)
{
        GtkWidget *box_parts, *part_table;

	box_parts = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(box_parts), 10);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(box_parts),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	part_table = _make_part_table();
	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(box_parts), part_table);
	gtk_widget_show(part_table);

	return box_parts;
}

static GtkWidget *_make_part_table(void)
{
	partition_info_msg_t *part_info_ptr = NULL;
	partition_info_t *part_ptr = NULL;
	GtkWidget *part_button, *part_label, *part_table;
	int error_code, i;
	char part_desc[128];

	/* Read partition info */
	if (old_part_info_ptr) {
		error_code = slurm_load_partitions (
				old_part_info_ptr->last_update,
				&part_info_ptr);
		if (error_code == SLURM_SUCCESS) {
			slurm_free_partition_info_msg (old_part_info_ptr);
		}
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			part_info_ptr = old_part_info_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_partitions ((time_t) NULL, 
						    &part_info_ptr);
	old_part_info_ptr = part_info_ptr;

	part_table = gtk_vbox_new(FALSE, 0);
	part_label = gtk_label_new(
	  "Partition State #Nodes etc. (make into sort buttons)");
	/* 123456789 12345 123456 */
	gtk_label_set_justify(GTK_LABEL(part_label), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start(GTK_BOX(part_table), part_label, 
			FALSE, FALSE, 0);
	gtk_widget_show(part_label);
	if (error_code) {
		slurm_perror ("slurm_load_partitions error");
		return part_table;
	}

	part_ptr = part_info_ptr->partition_array;
	for (i=0; i<part_info_ptr->record_count; i++) {
		sprintf(part_desc, "%8.8s", 
			part_ptr->name);
		part_ptr++;
		part_button = gtk_button_new_with_label(part_desc);
		gtk_signal_connect(GTK_OBJECT(part_button), "clicked",
			GTK_SIGNAL_FUNC(_part_details), (gpointer) i);
		gtk_box_pack_start(GTK_BOX(part_table), part_button, 
			FALSE, FALSE, 0);
		gtk_widget_show(part_button);
	}
	return part_table;
}

/* _part_details - Report details of a particular partition in a dialog box */
static void _part_details(GtkWidget *widget, gpointer data)
{
	GtkWidget *window, *box_vert, *button, *part_label;
	char part_details[1024];
	partition_info_t *part_ptr = NULL;

//	if (help_widget) /* Destroy old help window if one exists */
//		gtk_widget_destroy(help_widget);
	window = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			 GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			 GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 10);

	/* Vertical box container */
	box_vert = gtk_vbox_new(FALSE, 0);

	/* build partition details */
	part_ptr = old_part_info_ptr->partition_array;
	part_ptr += (int)data;
	sprintf(part_details, "xslurm part %s", part_ptr->name);
	gtk_window_set_title(GTK_WINDOW(window), part_details);
	_sprint_part_details(part_details, sizeof(part_details), part_ptr);
	part_label = gtk_label_new(part_details);

	gtk_label_set_line_wrap(GTK_LABEL(part_label), TRUE);
	gtk_box_pack_start(GTK_BOX(box_vert), part_label, 
			TRUE, TRUE, 0);
	gtk_widget_show(part_label);

	/* put quit button at bottom of window */
	button = gtk_button_new_with_label("Quit");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_box_pack_start(GTK_BOX(box_vert), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	gtk_container_add(GTK_CONTAINER(window), box_vert);
	gtk_widget_show(box_vert);
	gtk_widget_show(window);
}

static void _sprint_part_details(char *string, int size, 
				 partition_info_t *part_ptr)
{
	char next_line[128];

	strcpy(string, "");

	sprintf(next_line, "PartitionName=%s\n", part_ptr->name);
	_cat_if_room(string, next_line, size);

	sprintf(next_line, "TotalNodes=%d\n", part_ptr->total_nodes);
	_cat_if_room(string, next_line, size);

	sprintf(next_line, "TotalCPUs=%d\n", part_ptr->total_cpus);
	_cat_if_room(string, next_line, size);

	sprintf(next_line, "RootOnly=%s\n", _root_str(part_ptr->root_only));
	_cat_if_room(string, next_line, size);

	sprintf(next_line, "Default=%s\n", _root_str(part_ptr->default_part));
	_cat_if_room(string, next_line, size);

	sprintf(next_line, "Shared=%s\n", _shared_str(part_ptr->shared));
	_cat_if_room(string, next_line, size);

	_cat_if_room(string, "\n", size);
}

static inline void _cat_if_room(char *str1, char *str2, int size1) 
{
	if ((strlen(str1) + strlen(str2) + 1) < size1)
		strcat(str1, str2);
}

static inline char *_root_str(uint16_t root) 
{
	if (root)
		return "YES";
	else
		return "NO";
}

static inline char *_shared_str(uint16_t shared) 
{
#if 0
	if (shared == SHARED_YES)	/* undefined in slurm.h */
		return "FORCE"
#endif
	if (shared)
		return "YES";
	else
		return "NO";
}



/*
 * Job info set-up and support
 */

/* _make_job_widget - build scrollable list of jobs */
static GtkWidget *_make_job_widget(void)
{
        GtkWidget *box_jobs, *job_table;

	box_jobs = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(box_jobs), 10);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(box_jobs),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	job_table = _make_job_table();
	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(box_jobs), job_table);
	gtk_widget_show(job_table);

	return box_jobs;
}

static GtkWidget *_make_job_table(void)
{
	static job_info_msg_t *old_job_buffer_ptr = NULL;
	job_info_msg_t * job_buffer_ptr = NULL;
	job_info_t *job_ptr = NULL;
	struct passwd *user_info = NULL;
	GtkWidget *job_button, *job_label, *job_table;
	int error_code, i;
	char job_desc[128], user_name[18];

	/* Read job info, find job count */
	if (old_job_buffer_ptr) {
		error_code = slurm_load_jobs (old_job_buffer_ptr->last_update, 
					&job_buffer_ptr);
		if (error_code == SLURM_SUCCESS)
			slurm_free_job_info_msg (old_job_buffer_ptr);
		else if (slurm_get_errno () == SLURM_NO_CHANGE_IN_DATA) {
			job_buffer_ptr = old_job_buffer_ptr;
			error_code = SLURM_SUCCESS;
		}
	}
	else
		error_code = slurm_load_jobs ((time_t) NULL, &job_buffer_ptr);
	old_job_buffer_ptr = job_buffer_ptr;

	job_table = gtk_vbox_new(FALSE, 0);
	job_label = gtk_label_new(
	  "JobId    User     Name     etc. (make into sort buttons)");
	/* 12345678 12345678 12345678 */
	gtk_label_set_justify(GTK_LABEL(job_label), GTK_JUSTIFY_LEFT);
	gtk_box_pack_start(GTK_BOX(job_table), job_label, 
			FALSE, FALSE, 0);
	gtk_widget_show(job_label);
	if (error_code) {
		slurm_perror ("slurm_load_jobs error");
		return job_table;
	}

	job_ptr = job_buffer_ptr->job_array ;
	for (i=0; i<job_buffer_ptr->record_count; i++) {
		user_info = getpwuid((uid_t) job_ptr->user_id);
		if (user_info && user_info->pw_name[0])
			sprintf ( user_name, "%.16s", user_info->pw_name);
		else
			sprintf ( user_name, "(%u)", job_ptr->user_id);
		sprintf(job_desc, "%8d %8.8s %8.8s", 
			job_ptr->job_id, user_name, job_ptr->name);
		job_ptr++;
		job_button = gtk_button_new_with_label(job_desc);
		gtk_signal_connect(GTK_OBJECT(job_button), "clicked",
			GTK_SIGNAL_FUNC(_help), NULL);
		gtk_box_pack_start(GTK_BOX(job_table), job_button, 
			FALSE, FALSE, 0);
		gtk_widget_show(job_button);
	}
	return job_table;
}


/*
 * Button set-up and support
 */

/* _make_button_widget - build the button widget */
static GtkWidget *_make_button_widget(void)
{
        GtkWidget *box_bottom, *button;

	box_bottom = gtk_hbox_new(TRUE, 10);
	gtk_container_set_border_width(GTK_CONTAINER(box_bottom), 10);

	button = gtk_button_new_with_label("Refresh");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_refresh), NULL);
	gtk_box_pack_start(GTK_BOX(box_bottom), button, TRUE, TRUE, 0);
	gtk_widget_show(button);

	button = gtk_button_new_with_label("Submit");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_submit), NULL);
	gtk_box_pack_start(GTK_BOX(box_bottom), button, TRUE, TRUE, 0);
	gtk_widget_show(button);

	button = gtk_button_new_with_label("Help");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_help), NULL);
	gtk_box_pack_start(GTK_BOX(box_bottom), button, TRUE, TRUE, 0);
	gtk_widget_show(button);

	button = gtk_button_new_with_label("Quit");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_complete), NULL);
	gtk_box_pack_start(GTK_BOX(box_bottom), button, TRUE, TRUE, 0);
	gtk_widget_show(button);

	return box_bottom;
}

/* _complete - Program termination */
static void _complete(GtkWidget *widget, gpointer data) 
{
	gtk_main_quit();
}

/* _help_complete - Remove help dialog box */
static void _help_complete(GtkWidget *widget, gpointer data)
{
	gtk_widget_destroy(help_widget);
	help_widget = NULL;
}

/* _help - Pop-up help dialog box */
static void _help(GtkWidget *widget, gpointer data)
{
	GtkWidget *box_vert, *button, *help_label;

	if (help_widget) /* Destroy old help window if one exists */
		gtk_widget_destroy(help_widget);
	help_widget = gtk_window_new(GTK_WINDOW_DIALOG);
	gtk_window_set_title(GTK_WINDOW(help_widget), "xslurm help");
	gtk_signal_connect(GTK_OBJECT(help_widget), "delete_event",
			 GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_signal_connect(GTK_OBJECT(help_widget), "destroy",
			 GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(help_widget), 10);

	/* Vertical box container */
	box_vert = gtk_vbox_new(FALSE, 0);

	/* put help info here */
	help_label = gtk_label_new(
		"xslurm graphically reports SLURM partition, node and job "  \
	 	"status. It can also be used to submit a job and to modify " \
	 	"some state information given appropriate authorization.\n\n"\
		"The top box lists basic partition information. Click "      \
		"on one of the partition buttons to get more complete "      \
		"information about that partition. Click on one of the "     \
		"header buttons to sort by that field's value.\n\n"          \
		"The second box lists basic job information. Click on one "  \
		"of the job buttons to get more complete information "       \
		"about that job. Click on one of the header buttons to "     \
		"sort by that field's value.\n\n"                            \
		"See http://www.llnl.gov/linux/slurm/ for more "             \
		"informationabout SLURM.\n\n" );

	gtk_label_set_line_wrap(GTK_LABEL(help_label), TRUE);
	gtk_box_pack_start(GTK_BOX(box_vert), help_label, 
			TRUE, TRUE, 0);
	gtk_widget_show(help_label);

	/* put quit button at bottom of window */
	button = gtk_button_new_with_label("Quit");
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(_help_complete), NULL);
	gtk_box_pack_start(GTK_BOX(box_vert), button, FALSE, FALSE, 0);
	gtk_widget_show(button);

	gtk_container_add(GTK_CONTAINER(help_widget), box_vert);
	gtk_widget_show(box_vert);
	gtk_widget_show(help_widget);
}

/* _refresh - Re-read SLURM configuration and present new state */
static void _refresh(GtkWidget *widget, gpointer data)
{
	g_print("refresh now\n");
}

/* _submit - Submit a job to slurm now */
static void _submit(GtkWidget *widget, gpointer data)
{
	g_print("submit job now\n");
}
