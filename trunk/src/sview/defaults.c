/****************************************************************************\
 *  defaults.c - put default configuration information here
 *****************************************************************************
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>, et. al.
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
#include <fcntl.h>

#include "sview.h"
#include "src/common/parse_config.h"
#include "src/common/slurm_strcasestr.h"

extern display_data_t main_display_data[];

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

static void _init_sview_conf()
{
	int i;

	sview_config.refresh_delay = 5;
	sview_config.grid_x_width = 0;
	sview_config.grid_hori = 10;
	sview_config.grid_vert = 10;
	sview_config.show_hidden = 0;
	sview_config.admin_mode = FALSE;
	sview_config.grid_speedup = 0;
	sview_config.show_grid = TRUE;
	sview_config.page_default = JOB_PAGE;
	sview_config.tab_pos = GTK_POS_TOP;

	if(getenv("SVIEW_GRID_SPEEDUP"))
		sview_config.grid_speedup = 1;
	for(i=0; i<PAGE_CNT; i++) {
		if(!main_display_data[i].show)
			sview_config.page_visible[i] = FALSE;
		else
			sview_config.page_visible[i] = TRUE;
	}
}

extern int load_defaults()
{
	s_p_hashtbl_t *hashtbl = NULL;
	s_p_options_t sview_conf_options[] = {
		{"AdminMode", S_P_BOOLEAN},
		{"DefaultPage", S_P_STRING},
		{"GridHorizontal", S_P_UINT32},
		{"GridSpeedUp", S_P_BOOLEAN},
		{"GridVertical", S_P_UINT32},
		{"GridXWidth", S_P_UINT32},
		{"RefreshDelay", S_P_UINT16},
		{"ShowGrid", S_P_BOOLEAN},
		{"ShowHidden", S_P_BOOLEAN},
		{"TabPosition", S_P_STRING},
		{"VisiblePages", S_P_STRING},
		{NULL}
	};
	char *pathname = NULL;
	char *home = getenv("HOME");
	uint32_t hash_val = NO_VAL;
	int rc = SLURM_SUCCESS;
	char *tmp_str;

	_init_sview_conf();

	if(!home)
		return SLURM_ERROR;

	pathname = xstrdup_printf("%s/.slurm", home);
	if ((mkdir(pathname, 0750) < 0) && (errno != EEXIST)) {
		error("mkdir(%s): %m", pathname);
		rc = SLURM_ERROR;
		goto end_it;
	}
	xstrcat(pathname, "/sviewrc");

	if(access(pathname, R_OK) != 0) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	hashtbl = s_p_hashtbl_create(sview_conf_options);

	if(s_p_parse_file(hashtbl, &hash_val, pathname) == SLURM_ERROR)
		fatal("something wrong with opening/reading conf file");

	s_p_get_boolean(&sview_config.admin_mode, "AdminMode", hashtbl);
	if (s_p_get_string(&tmp_str, "DefaultPage", hashtbl)) {
		if (slurm_strcasestr(tmp_str, "job"))
			sview_config.page_default = JOB_PAGE;
		else if (slurm_strcasestr(tmp_str, "part"))
			sview_config.page_default = PART_PAGE;
		else if (slurm_strcasestr(tmp_str, "res"))
			sview_config.page_default = RESV_PAGE;
#ifdef HAVE_BG
		else if (slurm_strcasestr(tmp_str, "block"))
			sview_config.page_default = BLOCK_PAGE;
#endif
		else if (slurm_strcasestr(tmp_str, "node"))
			sview_config.page_default = NODE_PAGE;
		xfree(tmp_str);
	}
	s_p_get_uint32(&sview_config.grid_hori, "GridHorizontal", hashtbl);
	s_p_get_boolean(&sview_config.grid_speedup, "GridSpeedup", hashtbl);
	s_p_get_uint32(&sview_config.grid_vert, "GridVertical", hashtbl);
	s_p_get_uint32(&sview_config.grid_x_width, "GridXWidth", hashtbl);
	s_p_get_uint16(&sview_config.refresh_delay, "RefreshDelay", hashtbl);
	s_p_get_boolean(&sview_config.show_grid, "ShowGrid", hashtbl);
	s_p_get_boolean(&sview_config.show_hidden, "ShowHidden", hashtbl);
	if (s_p_get_string(&tmp_str, "TabPosition", hashtbl)) {
		if (slurm_strcasestr(tmp_str, "top"))
			sview_config.tab_pos = GTK_POS_TOP;
		else if (slurm_strcasestr(tmp_str, "bottom"))
			sview_config.tab_pos = GTK_POS_BOTTOM;
		else if (slurm_strcasestr(tmp_str, "left"))
			sview_config.tab_pos = GTK_POS_LEFT;
		else if (slurm_strcasestr(tmp_str, "right"))
			sview_config.tab_pos = GTK_POS_RIGHT;
		xfree(tmp_str);
	}
	if (s_p_get_string(&tmp_str, "VisiblePages", hashtbl)) {
		int i = 0;
		for(i=0; i<PAGE_CNT; i++)
			sview_config.page_visible[i] = FALSE;

		if (slurm_strcasestr(tmp_str, "job"))
			sview_config.page_visible[JOB_PAGE] = 1;
		if (slurm_strcasestr(tmp_str, "part"))
			sview_config.page_visible[PART_PAGE] = 1;
		if (slurm_strcasestr(tmp_str, "res"))
			sview_config.page_visible[RESV_PAGE] = 1;
#ifdef HAVE_BG
		if (slurm_strcasestr(tmp_str, "block"))
			sview_config.page_visible[BLOCK_PAGE] = 1;
#endif
		if (slurm_strcasestr(tmp_str, "node"))
			sview_config.page_visible[NODE_PAGE] = 1;
		xfree(tmp_str);
	}
	s_p_hashtbl_destroy(hashtbl);

end_it:
	xfree(pathname);
	return SLURM_SUCCESS;
}

extern int save_defaults()
{
	char *reg_file = NULL, *old_file = NULL, *new_file = NULL;
	char *home = getenv("HOME");
	int rc = SLURM_SUCCESS;
	char *tmp_str = NULL, *tmp_str2 = NULL;
	int fd = 0;

	if(!home)
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
				 sview_config.admin_mode ? "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("DefaultPage=%s\n",
				 page_to_str(sview_config.page_default));
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridHorizontal=%u\n", sview_config.grid_hori);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridSpeedup=%s\n",
				 sview_config.grid_speedup ? "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridVertical=%u\n", sview_config.grid_vert);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("GridXWidth=%u\n", sview_config.grid_x_width);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("RefreshDelay=%u\n",
				 sview_config.refresh_delay);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("ShowGrid=%s\n",
				 sview_config.show_grid ? "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("ShowHidden=%s\n",
				 sview_config.show_hidden ? "YES" : "NO");
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str = xstrdup_printf("TabPosition=%s\n",
				 tab_pos_to_str(sview_config.tab_pos));
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;
	tmp_str2 = visible_to_str();
	tmp_str = xstrdup_printf("VisiblePages=%s\n", tmp_str2);
	xfree(tmp_str2);
	rc = _write_to_file(fd, tmp_str);
	xfree(tmp_str);
	if(rc != SLURM_SUCCESS)
		goto end_it;

	fsync(fd);
	close(fd);

end_it:
	if (rc)
		(void) unlink(new_file);
	else {			/* file shuffle */
		int ign;	/* avoid warning */
		(void) unlink(old_file);
		ign =  link(reg_file, old_file);
		(void) unlink(reg_file);
		ign =  link(new_file, reg_file);
		(void) unlink(new_file);
	}

	xfree(old_file);
	xfree(new_file);
	xfree(reg_file);
	return rc;
}
