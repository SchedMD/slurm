/*****************************************************************************\
 *  configure_functions.c - Functions related to configure mode of smap.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
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

#include "src/smap/smap.h"

typedef struct {
	int type;
	char str[80];
} command_info_t;

void print_header_command(void);
int print_text_command(void);

void get_command(void)
{
	command_info_t *com = xmalloc(sizeof(command_info_t));
	//static node_info_msg_t *node_info_ptr;
	int text_height, text_width, text_starty, text_startx, error_code;
	WINDOW *command_win;
	int torus=MESH, i=0, i2, geo[PA_SYSTEM_DIMENSIONS];
	bool rotate = false;
	bool elongate = false;
	bool force_contig = false;
	List results;
	pa_request_t * request; 
				
	text_height = smap_info_ptr->text_win->_maxy;	// - smap_info_ptr->text_win->_begy;
	text_width = smap_info_ptr->text_win->_maxx;	// - smap_info_ptr->text_win->_begx;
	text_starty = smap_info_ptr->text_win->_begy;
	text_startx = smap_info_ptr->text_win->_begx;
	command_win = newwin(3, text_width - 1, LINES - 4, text_startx + 1);
	echo();
	/*error_code = slurm_load_node((time_t) NULL, &node_info_ptr, 0);
	if (error_code)
		if (quiet_flag != 1) {
			wclear(smap_info_ptr->text_win);
			smap_info_ptr->ycord =
			    smap_info_ptr->text_win->_maxy / 2;
			mvwprintw(smap_info_ptr->text_win,
				  smap_info_ptr->ycord, 1,
				  "slurm_load_node");
			return;
		}
	init_grid(node_info_ptr);
	*/
	if (!params.no_header)
		print_header_command();
	
	while (strcmp(com->str, "quit")) {
		print_grid();
		box(smap_info_ptr->text_win, 0, 0);
		box(smap_info_ptr->grid_win, 0, 0);
		wrefresh(smap_info_ptr->text_win);
		wrefresh(smap_info_ptr->grid_win);
		wclear(command_win);
		box(command_win, 0, 0);
		mvwprintw(command_win, 0, 3,
			  "Input Command: (type quit to change view, exit to exit)");
		wmove(command_win, 1, 1);
		wgetstr(command_win, com->str);

		if (!strcmp(com->str, "exit")) {
			endwin();
			exit(0);
		} else if (!strncmp(com->str, "resume", 6)) {
			mvwprintw(smap_info_ptr->text_win,
				  smap_info_ptr->ycord,
				  smap_info_ptr->xcord, "%s", com->str);
		} else if (!strncmp(com->str, "drain", 5)) {
			mvwprintw(smap_info_ptr->text_win,
				  smap_info_ptr->ycord,
				  smap_info_ptr->xcord, "%s", com->str);
		} else if (!strncmp(com->str, "create", 6)) {
			/*defaults*/
			geo[0] = -1;
			geo[1] = -1;
			geo[2] = -1;
			torus=MESH;
			rotate = false;
			elongate = false;
			force_contig = true;
			i=6;
			/*********/

			mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
				  smap_info_ptr->xcord, "%s", com->str);
			while(com->str[i]!='\0') {
				
				while(com->str[i-1]!=' ' && com->str[i]!='\0') {
					if(!i2 && (com->str[i] < 58 && com->str[i] > 47))
						i2=i;
					i++;
				}
				if(!strncmp(com->str+i, "torus", 5)) {
					torus=TORUS;
					i+=5;
				}
				else if(!strncmp(com->str+i, "rotate", 6)) {
					rotate=true;
					i+=6;
				}
				else if(!strncmp(com->str+i, "elongate", 8)) {
					elongate=true;
					i+=8;
				}
				else if(!strncmp(com->str+i, "force", 5)) {
					force_contig=false;				
					i+=5;
				}
			}

			if(!i2) {
				smap_info_ptr->ycord++;
				mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
					  smap_info_ptr->xcord, "No size or dimension specified, please re-enter");
			} else {
				if(com->str[i2+1] != 'x') { /* for size */
					i = atoi(&com->str[i2]);
				} else { /* for geometery */
					geo[0] = atoi(&com->str[i2]);
					i2+=2;
					geo[1] = atoi(&com->str[i2]);
					i2+=2;
					geo[2] = atoi(&com->str[i2]);
					i = -1;
				}
				new_pa_request(&request, geo, i, rotate, elongate, force_contig, torus);
				if (!allocate_part(request, &results)){
					printf("allocate success for %d%d%d\n", 
					       geo[0], geo[1], geo[2]);
					list_destroy(results);
				}
			}


		} else if (!strncmp(com->str, "save", 4)) {
			mvwprintw(smap_info_ptr->text_win,
				  smap_info_ptr->ycord,
				  smap_info_ptr->xcord, "%s", com->str);
		}
		smap_info_ptr->ycord++;
		//wattron(smap_info_ptr->text_win, COLOR_PAIR(smap_info_ptr->fill_in_value[count].color));
		//print_text_command(&com);
		//wattroff(smap_info_ptr->text_win, COLOR_PAIR(smap_info_ptr->fill_in_value[count].color));
		//count++;

	}
	//slurm_free_node_info_msg(node_info_ptr);
	params.display = 0;
	noecho();
	//init_grid(node_info_ptr);
	wclear(smap_info_ptr->text_win);
	smap_info_ptr->xcord = 1;
	smap_info_ptr->ycord = 1;
	print_date();
	get_job();
	return;
}

void print_header_command(void)
{
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "ID");
	smap_info_ptr->xcord += 5;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "NODE");
	smap_info_ptr->xcord += 8;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "STATE");
	smap_info_ptr->xcord += 10;
	mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord,
		  smap_info_ptr->xcord, "REASON");
	smap_info_ptr->xcord = 1;
	smap_info_ptr->ycord++;

}

int print_text_command()
{
	/*    time_t time;
	   int printed = 0;
	   int tempxcord;
	   int prefixlen;
	   int i = 0;
	   int width = 0;
	   struct passwd *user = NULL;
	   long days, hours, minutes, seconds;

	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%c", job_ptr->num_procs);
	   smap_info_ptr->xcord += 8;
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%d", job_ptr->job_id);
	   smap_info_ptr->xcord += 8;
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%s", job_ptr->partition);
	   smap_info_ptr->xcord += 12;
	   user = getpwuid((uid_t) job_ptr->user_id);
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%s", user->pw_name);
	   smap_info_ptr->xcord += 10;
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%s", job_ptr->name);
	   smap_info_ptr->xcord += 12;
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%s",
	   job_state_string(job_ptr->job_state));
	   smap_info_ptr->xcord += 10;
	   time = now - job_ptr->start_time;

	   seconds = time % 60;
	   minutes = (time / 60) % 60;
	   hours = (time / 3600) % 24;
	   days = time / 86400;

	   if (days)
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord,
	   "%ld:%2.2ld:%2.2ld:%2.2ld", days, hours, minutes,
	   seconds);
	   else if (hours)
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%ld:%2.2ld:%2.2ld",
	   hours, minutes, seconds);
	   else
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%ld:%2.2ld", minutes,
	   seconds);

	   smap_info_ptr->xcord += 12;
	   mvwprintw(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord, "%d", job_ptr->num_nodes);
	   smap_info_ptr->xcord += 8;
	   tempxcord = smap_info_ptr->xcord;
	   width = smap_info_ptr->text_win->_maxx - smap_info_ptr->xcord;
	   while (job_ptr->nodes[i] != '\0') {
	   if ((printed =
	   mvwaddch(smap_info_ptr->text_win, smap_info_ptr->ycord, smap_info_ptr->xcord,
	   job_ptr->nodes[i])) < 0)
	   return printed;
	   smap_info_ptr->xcord++;
	   width = smap_info_ptr->text_win->_maxx - smap_info_ptr->xcord;
	   if (job_ptr->nodes[i] == '[')
	   prefixlen = i + 1;
	   else if (job_ptr->nodes[i] == ',' && (width - 9) <= 0) {
	   smap_info_ptr->ycord++;
	   smap_info_ptr->xcord = tempxcord + prefixlen;
	   }
	   i++;
	   }

	   smap_info_ptr->xcord = 1;
	   smap_info_ptr->ycord++;
	   return printed; */
}
