/*****************************************************************************\
 *  bg_read_config.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "bg_core.h"
#include "bg_read_config.h"
#include "src/common/node_select.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"

#include <stdlib.h>

s_p_options_t bg_conf_file_options[] = {
#ifdef HAVE_BGL
	{"BlrtsImage", S_P_STRING},
	{"LinuxImage", S_P_STRING},
	{"RamDiskImage", S_P_STRING},
	{"AltBlrtsImage", S_P_ARRAY, parse_image, NULL},
	{"AltLinuxImage", S_P_ARRAY, parse_image, NULL},
	{"AltRamDiskImage", S_P_ARRAY, parse_image, NULL},
#elif HAVE_BGP
	{"CnloadImage", S_P_STRING},
	{"IoloadImage", S_P_STRING},
	{"AltCnloadImage", S_P_ARRAY, parse_image, NULL},
	{"AltIoloadImage", S_P_ARRAY, parse_image, NULL},
#endif
	{"DenyPassthrough", S_P_STRING},
	{"LayoutMode", S_P_STRING},
	{"MloaderImage", S_P_STRING},
	{"BridgeAPILogFile", S_P_STRING},
	{"BridgeAPIVerbose", S_P_UINT16},
	{"BasePartitionNodeCnt", S_P_UINT16},
	{"NodeCardNodeCnt", S_P_UINT16},
	{"Numpsets", S_P_UINT16},
	{"IONodesPerMP", S_P_UINT16},
	{"BPs", S_P_ARRAY, parse_blockreq, destroy_select_ba_request},
	{"MPs", S_P_ARRAY, parse_blockreq, destroy_select_ba_request},
	/* these are just going to be put into a list that will be
	   freed later don't free them after reading them */
	{"AltMloaderImage", S_P_ARRAY, parse_image, NULL},
	{NULL}
};

static int _reopen_bridge_log(void)
{
	int rc = SLURM_SUCCESS;

#if defined HAVE_BG_L_P
	if (bg_conf->bridge_api_file == NULL)
		return rc;

#if defined HAVE_BG_FILES
	rc = bridge_set_log_params(bg_conf->bridge_api_file,
				   bg_conf->bridge_api_verb);
#endif
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("Bridge api file set to %s, verbose level %d",
		     bg_conf->bridge_api_file, bg_conf->bridge_api_verb);
#endif
	return rc;
}

static char *_get_bg_conf(void)
{
	char *val = getenv("SLURM_CONF");
	char *rc = NULL;
	int i;

	if (!val)
		return xstrdup(BLUEGENE_CONFIG_FILE);

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("bluegene.conf") + 1;
	rc = xmalloc(i);
	strcpy(rc, val);
	val = strrchr(rc, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = rc;
	strcpy(val, "bluegene.conf");
	return rc;
}

static void _destroy_bitmap(void *object)
{
	bitstr_t *bitstr = (bitstr_t *)object;

	if (bitstr) {
		FREE_NULL_BITMAP(bitstr);
	}
}

extern void destroy_image_group_list(void *ptr)
{
	image_group_t *image_group = (image_group_t *)ptr;
	if (image_group) {
		xfree(image_group->name);
		xfree(image_group);
	}
}

extern void destroy_image(void *ptr)
{
	image_t *n = (image_t *)ptr;
	if (n) {
		xfree(n->name);
		if (n->groups) {
			list_destroy(n->groups);
			n->groups = NULL;
		}
		xfree(n);
	}
}

extern int parse_blockreq(void **dest, slurm_parser_enum_t type,
			  const char *key, const char *value,
			  const char *line, char **leftover)
{
	s_p_options_t block_options[] = {
		{"Type", S_P_STRING},
		{"32CNBlocks", S_P_UINT16},
		{"128CNBlocks", S_P_UINT16},
#ifdef HAVE_BGL
		{"Nodecards", S_P_UINT16},
		{"Quarters", S_P_UINT16},
		{"BlrtsImage", S_P_STRING},
		{"LinuxImage", S_P_STRING},
		{"RamDiskImage", S_P_STRING},
#else
		{"16CNBlocks", S_P_UINT16},
		{"64CNBlocks", S_P_UINT16},
		{"256CNBlocks", S_P_UINT16},
		{"CnloadImage", S_P_STRING},
		{"IoloadImage", S_P_STRING},
#endif
		{"MloaderImage", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl;
	char *tmp = NULL;
	select_ba_request_t *n = NULL;
	hostlist_t hl = NULL;

	tbl = s_p_hashtbl_create(block_options);
	s_p_parse_line(tbl, *leftover, leftover);
	if (!value) {
		return 0;
	}
	n = xmalloc(sizeof(select_ba_request_t));
	hl = hostlist_create(value);
	n->save_name = hostlist_ranged_string_xmalloc(hl);
	hostlist_destroy(hl);
#ifdef HAVE_BGL
	s_p_get_string(&n->blrtsimage, "BlrtsImage", tbl);
	s_p_get_string(&n->linuximage, "LinuxImage", tbl);
	s_p_get_string(&n->ramdiskimage, "RamDiskImage", tbl);
#else
	s_p_get_string(&n->linuximage, "CnloadImage", tbl);
	s_p_get_string(&n->ramdiskimage, "IoloadImage", tbl);
#endif
	s_p_get_string(&n->mloaderimage, "MloaderImage", tbl);

	s_p_get_string(&tmp, "Type", tbl);
	if (!tmp || !strcasecmp(tmp,"TORUS"))
		n->conn_type[0] = SELECT_TORUS;
	else if (!strcasecmp(tmp,"MESH"))
		n->conn_type[0] = SELECT_MESH;
	else
		n->conn_type[0] = SELECT_SMALL;
	xfree(tmp);

	if (!s_p_get_uint16(&n->small32, "32CNBlocks", tbl)) {
#ifdef HAVE_BGL
		s_p_get_uint16(&n->small32, "Nodecards", tbl);
#else
		;
#endif
	}
	if (!s_p_get_uint16(&n->small128, "128CNBlocks", tbl)) {
#ifdef HAVE_BGL
		s_p_get_uint16(&n->small128, "Quarters", tbl);
#else
		;
#endif
	}

#ifndef HAVE_BGL
	s_p_get_uint16(&n->small16, "16CNBlocks", tbl);
	s_p_get_uint16(&n->small64, "64CNBlocks", tbl);
	s_p_get_uint16(&n->small256, "256CNBlocks", tbl);
#endif

	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;
	return 1;
}

extern int parse_image(void **dest, slurm_parser_enum_t type,
		       const char *key, const char *value,
		       const char *line, char **leftover)
{
	s_p_options_t image_options[] = {
		{(char *)"GROUPS", S_P_STRING},
		{NULL}
	};
	s_p_hashtbl_t *tbl = NULL;
	char *tmp = NULL;
	image_t *n = NULL;
	image_group_t *image_group = NULL;
	int i = 0, j = 0;

	tbl = s_p_hashtbl_create(image_options);
	s_p_parse_line(tbl, *leftover, leftover);

	n = (image_t *)xmalloc(sizeof(image_t));
	n->name = xstrdup(value);
	n->def = false;
	n->groups = list_create(destroy_image_group_list);
	s_p_get_string(&tmp, "Groups", tbl);
	if (tmp) {
		for(i=0; i<(int)strlen(tmp); i++) {
			if ((tmp[i] == ':') || (tmp[i] == ',')) {
				image_group = (image_group_t *)
					xmalloc(sizeof(image_group_t));
				image_group->name = (char *)xmalloc(i-j+2);
				snprintf(image_group->name,
					 (i-j)+1, "%s", tmp+j);
				gid_from_string (image_group->name,
						 &image_group->gid);
				list_append(n->groups, image_group);
				j=i;
				j++;
			}
		}
		if (j != i) {
			image_group = (image_group_t *)
				xmalloc(sizeof(image_group_t));
			image_group->name = (char *)xmalloc(i-j+2);
			snprintf(image_group->name, (i-j)+1, "%s", tmp+j);
			if (gid_from_string (image_group->name,
			                     &image_group->gid) < 0)
				fatal("Invalid bluegene.conf parameter "
				      "Groups=%s",
				      image_group->name);
			list_append(n->groups, image_group);
		}
		xfree(tmp);
	}
	s_p_hashtbl_destroy(tbl);

	*dest = (void *)n;
	return 1;
}

/*
 * Read and process the bluegene.conf configuration file so to interpret what
 * blocks are static/dynamic, torus/mesh, etc.
 */
extern int read_bg_conf(void)
{
	int i;
	int count = 0;
	s_p_hashtbl_t *tbl = NULL;
	char *layout = NULL;
	select_ba_request_t **blockreq_array = NULL;
	image_t **image_array = NULL;
	image_t *image = NULL;
	static time_t last_config_update = (time_t) 0;
	struct stat config_stat;
	ListIterator itr = NULL;
	char* bg_conf_file = NULL;
	static int *dims = NULL;

	if (!dims)
		dims = select_g_ba_get_dims();

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("Reading the bluegene.conf file");

	/* check if config file has changed */
	bg_conf_file = _get_bg_conf();

	if (stat(bg_conf_file, &config_stat) < 0)
		fatal("can't stat bluegene.conf file %s: %m", bg_conf_file);
	if (last_config_update) {
		_reopen_bridge_log();
		if (last_config_update == config_stat.st_mtime) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
				info("%s unchanged", bg_conf_file);
		} else {
			info("Restart slurmctld for %s changes "
			     "to take effect",
			     bg_conf_file);
		}
		last_config_update = config_stat.st_mtime;
		xfree(bg_conf_file);
		return SLURM_SUCCESS;
	}
	last_config_update = config_stat.st_mtime;

	/* initialization */
	/* bg_conf defined in bg_node_alloc.h */
	tbl = s_p_hashtbl_create(bg_conf_file_options);

	if (s_p_parse_file(tbl, NULL, bg_conf_file, false) == SLURM_ERROR)
		fatal("something wrong with opening/reading bluegene "
		      "conf file");
	xfree(bg_conf_file);

	if (s_p_get_array((void ***)&image_array,
			  &count, "AltMloaderImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->mloader_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_mloaderimage,
			    "MloaderImage", tbl)) {
		if (!list_count(bg_conf->mloader_list))
			fatal("MloaderImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->mloader_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_mloaderimage = xstrdup(image->name);
		info("Warning: using %s as the default MloaderImage.  "
		     "If this isn't correct please set MloaderImage",
		     bg_conf->default_mloaderimage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default MloaderImage %s",
			     bg_conf->default_mloaderimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_mloaderimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->mloader_list, image);
	}

	if (!s_p_get_uint16(
		    &bg_conf->mp_node_cnt, "BasePartitionNodeCnt", tbl)) {
		error("BasePartitionNodeCnt not configured in bluegene.conf "
		      "defaulting to 512 as BasePartitionNodeCnt");
		bg_conf->mp_node_cnt = 512;
		bg_conf->quarter_node_cnt = 128;
	} else {
		if (bg_conf->mp_node_cnt <= 0)
			fatal("You should have more than 0 nodes "
			      "per base partition");

		bg_conf->quarter_node_cnt = bg_conf->mp_node_cnt/4;
	}
	/* bg_conf->cpus_per_mp should had already been set from the
	 * node_init */
	if (bg_conf->cpus_per_mp < bg_conf->mp_node_cnt) {
		fatal("For some reason we have only %u cpus per mp, but "
		      "have %u cnodes per mp.  You need at least the same "
		      "number of cpus as you have cnodes per mp.  "
		      "Check the NodeName Procs= "
		      "definition in the slurm.conf.",
		      bg_conf->cpus_per_mp, bg_conf->mp_node_cnt);
	}

	bg_conf->cpu_ratio = bg_conf->cpus_per_mp/bg_conf->mp_node_cnt;
	if (!bg_conf->cpu_ratio)
		fatal("We appear to have less than 1 cpu on a cnode.  "
		      "You specified %u for BasePartitionNodeCnt "
		      "in the blugene.conf and %u cpus "
		      "for each node in the slurm.conf",
		      bg_conf->mp_node_cnt, bg_conf->cpus_per_mp);

	num_unused_cpus = 1;
	for (i = 0; i<SYSTEM_DIMENSIONS; i++)
		num_unused_cpus *= dims[i];
	num_unused_cpus *= bg_conf->cpus_per_mp;

	if (!s_p_get_uint16(
		    &bg_conf->nodecard_node_cnt, "NodeCardNodeCnt", tbl)) {
		error("NodeCardNodeCnt not configured in bluegene.conf "
		      "defaulting to 32 as NodeCardNodeCnt");
		bg_conf->nodecard_node_cnt = 32;
	}

	if (bg_conf->nodecard_node_cnt<=0)
		fatal("You should have more than 0 nodes per nodecard");

	bg_conf->mp_nodecard_cnt =
		bg_conf->mp_node_cnt / bg_conf->nodecard_node_cnt;

	if (!s_p_get_uint16(&bg_conf->ionodes_per_mp, "Numpsets", tbl))
		fatal("Warning: Numpsets not configured in bluegene.conf");
	if (!bg_conf->ionodes_per_mp) {
		if (!s_p_get_uint16(&bg_conf->ionodes_per_mp,
				    "IONodesPerMP", tbl))
			fatal("Warning: IONodesPerMP not configured "
			      "in bluegene.conf");
	}

	if (bg_conf->ionodes_per_mp) {
		bitstr_t *tmp_bitmap = NULL;
		int small_size = 1;

		/* THIS IS A HACK TO MAKE A 1 NODECARD SYSTEM WORK */
		if (bg_conf->mp_node_cnt == bg_conf->nodecard_node_cnt) {
			bg_conf->quarter_ionode_cnt = 2;
			bg_conf->nodecard_ionode_cnt = 2;
		} else {
			bg_conf->quarter_ionode_cnt = bg_conf->ionodes_per_mp/4;
			bg_conf->nodecard_ionode_cnt =
				bg_conf->quarter_ionode_cnt/4;
		}

		/* How many nodecards per ionode */
		bg_conf->nc_ratio =
			((double)bg_conf->mp_node_cnt
			 / (double)bg_conf->nodecard_node_cnt)
			/ (double)bg_conf->ionodes_per_mp;
		/* How many ionodes per nodecard */
		bg_conf->io_ratio =
			(double)bg_conf->ionodes_per_mp /
			((double)bg_conf->mp_node_cnt
			 / (double)bg_conf->nodecard_node_cnt);
		//info("got %f %f", bg_conf->nc_ratio, bg_conf->io_ratio);
		/* figure out the smallest block we can have on the
		   system */
#ifdef HAVE_BGL
		if (bg_conf->io_ratio >= 1)
			bg_conf->smallest_block=32;
		else
			bg_conf->smallest_block=128;
#else
		if (bg_conf->io_ratio >= 2)
			bg_conf->smallest_block=16;
		else if (bg_conf->io_ratio == 1)
			bg_conf->smallest_block=32;
		else if (bg_conf->io_ratio == .5)
			bg_conf->smallest_block=64;
		else if (bg_conf->io_ratio == .25)
			bg_conf->smallest_block=128;
		else if (bg_conf->io_ratio == .125)
			bg_conf->smallest_block=256;
		else {
			error("unknown ioratio %f.  Can't figure out "
			      "smallest block size, setting it to midplane",
			      bg_conf->io_ratio);
			bg_conf->smallest_block = 512;
		}
#endif
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("Smallest block possible on this system is %u",
			     bg_conf->smallest_block);
		/* below we are creating all the possible bitmaps for
		 * each size of small block
		 */
		if ((int)bg_conf->nodecard_ionode_cnt < 1) {
			bg_conf->nodecard_ionode_cnt = 0;
		} else {
			bg_lists->valid_small32 = list_create(_destroy_bitmap);
			if ((small_size = bg_conf->nodecard_ionode_cnt))
				small_size--;
			i = 0;
			while (i<bg_conf->ionodes_per_mp) {
				tmp_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
				bit_nset(tmp_bitmap, i, i+small_size);
				i += small_size+1;
				list_append(bg_lists->valid_small32,
					    tmp_bitmap);
			}
		}
		/* If we only have 1 nodecard just jump to the end
		   since this will never need to happen below.
		   Pretty much a hack to avoid seg fault;). */
		if (bg_conf->mp_node_cnt == bg_conf->nodecard_node_cnt)
			goto no_calc;

		bg_lists->valid_small128 = list_create(_destroy_bitmap);
		if ((small_size = bg_conf->quarter_ionode_cnt))
			small_size--;
		i = 0;
		while (i<bg_conf->ionodes_per_mp) {
			tmp_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_lists->valid_small128, tmp_bitmap);
		}

#ifndef HAVE_BGL
		bg_lists->valid_small64 = list_create(_destroy_bitmap);
		if ((small_size = bg_conf->nodecard_ionode_cnt * 2))
			small_size--;
		i = 0;
		while (i<bg_conf->ionodes_per_mp) {
			tmp_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_lists->valid_small64, tmp_bitmap);
		}

		bg_lists->valid_small256 = list_create(_destroy_bitmap);
		if ((small_size = bg_conf->quarter_ionode_cnt * 2))
			small_size--;
		i = 0;
		while (i<bg_conf->ionodes_per_mp) {
			tmp_bitmap = bit_alloc(bg_conf->ionodes_per_mp);
			bit_nset(tmp_bitmap, i, i+small_size);
			i += small_size+1;
			list_append(bg_lists->valid_small256, tmp_bitmap);
		}
#endif
	} else {
		fatal("your ionodes_per_mp is 0");
	}

no_calc:

	if (!s_p_get_uint16(&bg_conf->bridge_api_verb, "BridgeAPIVerbose", tbl))
		info("Warning: BridgeAPIVerbose not configured "
		     "in bluegene.conf");
	if (!s_p_get_string(&bg_conf->bridge_api_file,
			    "BridgeAPILogFile", tbl))
		info("BridgeAPILogFile not configured in bluegene.conf");
	else
		_reopen_bridge_log();

	if (s_p_get_string(&layout, "DenyPassthrough", tbl)) {
		if (strstr(layout, "A"))
			ba_deny_pass |= PASS_DENY_A;
		if (strstr(layout, "X"))
			ba_deny_pass |= PASS_DENY_X;
		if (strstr(layout, "Y"))
			ba_deny_pass |= PASS_DENY_Y;
		if (strstr(layout, "Z"))
			ba_deny_pass |= PASS_DENY_Z;
		if (!strcasecmp(layout, "ALL"))
			ba_deny_pass |= PASS_DENY_ALL;
		bg_conf->deny_pass = ba_deny_pass;
		xfree(layout);
	}

	if (!s_p_get_string(&layout, "LayoutMode", tbl)) {
		info("Warning: LayoutMode was not specified in bluegene.conf "
		     "defaulting to STATIC partitioning");
		bg_conf->layout_mode = LAYOUT_STATIC;
	} else {
		if (!strcasecmp(layout,"STATIC"))
			bg_conf->layout_mode = LAYOUT_STATIC;
		else if (!strcasecmp(layout,"OVERLAP"))
			bg_conf->layout_mode = LAYOUT_OVERLAP;
		else if (!strcasecmp(layout,"DYNAMIC"))
			bg_conf->layout_mode = LAYOUT_DYNAMIC;
		else {
			fatal("I don't understand this LayoutMode = %s",
			      layout);
		}
		xfree(layout);
	}

	/* add blocks defined in file */
	if (bg_conf->layout_mode != LAYOUT_DYNAMIC) {
		if (!s_p_get_array((void ***)&blockreq_array,
				   &count, "BPs", tbl)) {
			info("WARNING: no blocks defined in bluegene.conf, "
			     "only making full system block");
			/* create_full_system_block(NULL); */
		}

		for (i = 0; i < count; i++) {
			add_bg_record(bg_lists->main, NULL,
				      blockreq_array[i], 0, 0);
		}
	}
	s_p_hashtbl_destroy(tbl);

	return SLURM_SUCCESS;
}
