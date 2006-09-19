/****************************************************************************\
 *  popups.c - put different popup displays here
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include "sview.h"


extern void create_config_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Interval in Seconds ");
	GtkObject *adjustment = gtk_adjustment_new(global_sleep_time,
						   1, 10000,
						   5, 60,
						   1);
	GtkWidget *spin_button = 
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		global_sleep_time = 
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       global_sleep_time);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!g_thread_create(_refresh_thr, GINT_TO_POINTER(response),
				     FALSE, &error))
		{
			g_printerr ("Failed to create refresh thread: %s\n", 
				    error->message);
		}
	}

	gtk_widget_destroy(popup);
	
	return;
}

extern void create_deamon_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Interval in Seconds ");
	GtkObject *adjustment = gtk_adjustment_new(global_sleep_time,
						   1, 10000,
						   5, 60,
						   1);
	GtkWidget *spin_button = 
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		global_sleep_time = 
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       global_sleep_time);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!g_thread_create(_refresh_thr, GINT_TO_POINTER(response),
				     FALSE, &error))
		{
			g_printerr ("Failed to create refresh thread: %s\n", 
				    error->message);
		}
	}

	gtk_widget_destroy(popup);
	
	return;
}

extern void create_search_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Interval in Seconds ");
	GtkObject *adjustment = gtk_adjustment_new(global_sleep_time,
						   1, 10000,
						   5, 60,
						   1);
	GtkWidget *spin_button = 
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		global_sleep_time = 
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       global_sleep_time);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!g_thread_create(_refresh_thr, GINT_TO_POINTER(response),
				     FALSE, &error))
		{
			g_printerr ("Failed to create refresh thread: %s\n", 
				    error->message);
		}
	}

	gtk_widget_destroy(popup);
	
	return;
}

extern void change_refresh_popup(GtkToggleAction *action, gpointer user_data)
{
	GtkWidget *table = gtk_table_new(1, 2, FALSE);
	GtkWidget *label = gtk_label_new("Interval in Seconds ");
	GtkObject *adjustment = gtk_adjustment_new(global_sleep_time,
						   1, 10000,
						   5, 60,
						   1);
	GtkWidget *spin_button = 
		gtk_spin_button_new(GTK_ADJUSTMENT(adjustment), 1, 0);
	GtkWidget *popup = gtk_dialog_new_with_buttons(
		"Refresh Interval",
		GTK_WINDOW (user_data),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_OK,
		GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL,
		GTK_RESPONSE_CANCEL,
		NULL);
	GError *error = NULL;
	int response = 0;
	char *temp = NULL;

	gtk_container_set_border_width(GTK_CONTAINER(table), 10);
	
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(popup)->vbox), 
			   table, FALSE, FALSE, 0);
	
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);	
	gtk_table_attach_defaults(GTK_TABLE(table), spin_button, 1, 2, 0, 1);
	
	gtk_widget_show_all(popup);
	response = gtk_dialog_run (GTK_DIALOG(popup));

	if (response == GTK_RESPONSE_OK)
	{
		global_sleep_time = 
			gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_button));
		temp = g_strdup_printf("Refresh Interval set to %d seconds.",
				       global_sleep_time);
		gtk_statusbar_pop(GTK_STATUSBAR(main_statusbar), 
				  STATUS_REFRESH);
		response = gtk_statusbar_push(GTK_STATUSBAR(main_statusbar), 
					      STATUS_REFRESH,
					      temp);
		g_free(temp);
		if (!g_thread_create(_refresh_thr, GINT_TO_POINTER(response),
				     FALSE, &error))
		{
			g_printerr ("Failed to create refresh thread: %s\n", 
				    error->message);
		}
	}

	gtk_widget_destroy(popup);
	
	return;
}

