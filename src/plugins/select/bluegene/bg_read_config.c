/*****************************************************************************\
 *  bg_read_config.c
 *
 *****************************************************************************
 *  Copyright (C) 2011 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "src/common/slurm_xlator.h"	/* Must be first */
#include "bg_core.h"
#include "bg_read_config.h"
#include "src/common/node_select.h"
#include "src/common/xstring.h"
#include "src/common/uid.h"
#include "src/common/proc_args.h"
#include "src/common/assoc_mgr.h"

#include <stdlib.h>

static s_p_options_t bg_conf_file_options[] = {
#ifndef HAVE_BG_L_P
	{"AllowSubBlockAllocations", S_P_BOOLEAN},
#elif defined HAVE_BGL
	{"BlrtsImage", S_P_STRING},
	{"LinuxImage", S_P_STRING},
	{"RamDiskImage", S_P_STRING},
	{"AltBlrtsImage", S_P_ARRAY, parse_image, NULL},
	{"AltLinuxImage", S_P_ARRAY, parse_image, NULL},
	{"AltRamDiskImage", S_P_ARRAY, parse_image, NULL},
#elif defined HAVE_BGP
	{"CnloadImage", S_P_STRING},
	{"IoloadImage", S_P_STRING},
	{"AltCnloadImage", S_P_ARRAY, parse_image, NULL},
	{"AltIoloadImage", S_P_ARRAY, parse_image, NULL},
#endif
	{"DefaultConnType", S_P_STRING},
	{"DenyPassthrough", S_P_STRING},
	{"LayoutMode", S_P_STRING},
	{"MloaderImage", S_P_STRING},
	{"BridgeAPILogFile", S_P_STRING},
	{"BridgeAPIVerbose", S_P_UINT16},
	{"BasePartitionNodeCnt", S_P_UINT16},
	{"MidplaneNodeCnt", S_P_UINT16},
	{"NodeCardNodeCnt", S_P_UINT16},
	{"NodeBoardNodeCnt", S_P_UINT16},
	{"Numpsets", S_P_UINT16},
	{"IONodesPerMP", S_P_UINT16},
	{"MaxBlockInError", S_P_UINT16},
	{"BPs", S_P_ARRAY, parse_blockreq, destroy_select_ba_request},
	{"MPs", S_P_ARRAY, parse_blockreq, destroy_select_ba_request},
	/* these are just going to be put into a list that will be
	   freed later don't free them after reading them */
	{"AltMloaderImage", S_P_ARRAY, parse_image, NULL},
	{"SubMidplaneSystem", S_P_BOOLEAN},
	{"RebootQOSList", S_P_STRING},
	{NULL}
};

static int _reopen_bridge_log(void)
{
	int rc = SLURM_SUCCESS;

	if (bg_conf->bridge_api_file == NULL)
		return rc;

#if defined HAVE_BG_FILES
	rc = bridge_set_log_params(bg_conf->bridge_api_file,
				   bg_conf->bridge_api_verb);
#endif
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
		info("Bridge api file set to %s, verbose level %d",
		     bg_conf->bridge_api_file, bg_conf->bridge_api_verb);
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
#ifdef HAVE_BGP
		{"16CNBlocks", S_P_UINT16},
		{"CnloadImage", S_P_STRING},
		{"IoloadImage", S_P_STRING},
#endif
		{"64CNBlocks", S_P_UINT16},
		{"256CNBlocks", S_P_UINT16},
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
#elif defined HAVE_BGP
	s_p_get_string(&n->linuximage, "CnloadImage", tbl);
	s_p_get_string(&n->ramdiskimage, "IoloadImage", tbl);
#endif
	s_p_get_string(&n->mloaderimage, "MloaderImage", tbl);

	s_p_get_string(&tmp, "Type", tbl);
	if (tmp) {
		verify_conn_type(tmp, n->conn_type);
		xfree(tmp);
	}

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
#ifdef HAVE_BGP
	s_p_get_uint16(&n->small16, "16CNBlocks", tbl);
#endif
	s_p_get_uint16(&n->small64, "64CNBlocks", tbl);
	s_p_get_uint16(&n->small256, "256CNBlocks", tbl);
#endif
	if (n->small16 || n->small32 || n->small64
	    || n->small128 || n->small256) {
		if (n->conn_type[0] < SELECT_SMALL) {
			error("Block def on midplane(s) %s is "
			      "asking for small blocks but given "
			      "TYPE=%s, setting it to Small",
			      n->save_name, conn_type_string(n->conn_type[0]));
			n->conn_type[0] = SELECT_SMALL;
		}
	} else {
		if (n->conn_type[0] == (uint16_t)NO_VAL) {
			n->conn_type[0] = bg_conf->default_conn_type[0];
		} else if (n->conn_type[0] >= SELECT_SMALL) {
			error("Block def on midplane(s) %s is given "
			      "TYPE=%s but isn't asking for any small "
			      "blocks.  Giving it %s.",
			      n->save_name, conn_type_string(n->conn_type[0]),
			      conn_type_string(
				      bg_conf->default_conn_type[0]));
			n->conn_type[0] = bg_conf->default_conn_type[0];
		}
#ifndef HAVE_BG_L_P
		int i;

		for (i=1; i<SYSTEM_DIMENSIONS; i++) {
			if (n->conn_type[i] == (uint16_t)NO_VAL)
				n->conn_type[i] = bg_conf->default_conn_type[i];
			else if (n->conn_type[i] >= SELECT_SMALL) {
				error("Block def on midplane(s) %s dim %d "
				      "is given TYPE=%s but isn't asking "
				      "for any small blocks.  Giving it %s.",
				      n->save_name, i,
				      conn_type_string(n->conn_type[i]),
				      conn_type_string(
					      bg_conf->default_conn_type[i]));
				n->conn_type[i] = bg_conf->default_conn_type[i];
			}
		}
#endif
	}
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
	bool tmp_bool = 0;
	int count = 0;
	s_p_hashtbl_t *tbl = NULL;
	char *tmp_char = NULL;
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
	bg_conf_file = get_extra_conf_path("bluegene.conf");

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
	if (!(tbl = config_make_tbl(bg_conf_file)))
		fatal("something wrong with opening/reading bluegene "
		      "conf file");
	xfree(bg_conf_file);

#ifdef HAVE_BGL
	if (s_p_get_array((void ***)&image_array,
			  &count, "AltBlrtsImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->blrts_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_blrtsimage, "BlrtsImage", tbl)) {
		if (!list_count(bg_conf->blrts_list))
			fatal("BlrtsImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->blrts_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_blrtsimage = xstrdup(image->name);
		info("Warning: using %s as the default BlrtsImage.  "
		     "If this isn't correct please set BlrtsImage",
		     bg_conf->default_blrtsimage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default BlrtsImage %s",
			     bg_conf->default_blrtsimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_blrtsimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->blrts_list, image);
	}

	if (s_p_get_array((void ***)&image_array,
			  &count, "AltLinuxImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->linux_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_linuximage, "LinuxImage", tbl)) {
		if (!list_count(bg_conf->linux_list))
			fatal("LinuxImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->linux_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_linuximage = xstrdup(image->name);
		info("Warning: using %s as the default LinuxImage.  "
		     "If this isn't correct please set LinuxImage",
		     bg_conf->default_linuximage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default LinuxImage %s",
			     bg_conf->default_linuximage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_linuximage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->linux_list, image);
	}

	if (s_p_get_array((void ***)&image_array,
			  &count, "AltRamDiskImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->ramdisk_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_ramdiskimage,
			    "RamDiskImage", tbl)) {
		if (!list_count(bg_conf->ramdisk_list))
			fatal("RamDiskImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->ramdisk_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_ramdiskimage = xstrdup(image->name);
		info("Warning: using %s as the default RamDiskImage.  "
		     "If this isn't correct please set RamDiskImage",
		     bg_conf->default_ramdiskimage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default RamDiskImage %s",
			     bg_conf->default_ramdiskimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_ramdiskimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->ramdisk_list, image);
	}
#elif defined HAVE_BGP

	if (s_p_get_array((void ***)&image_array,
			  &count, "AltCnloadImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->linux_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_linuximage, "CnloadImage", tbl)) {
		if (!list_count(bg_conf->linux_list))
			fatal("CnloadImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->linux_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_linuximage = xstrdup(image->name);
		info("Warning: using %s as the default CnloadImage.  "
		     "If this isn't correct please set CnloadImage",
		     bg_conf->default_linuximage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default CnloadImage %s",
			     bg_conf->default_linuximage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_linuximage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->linux_list, image);
	}

	if (s_p_get_array((void ***)&image_array,
			  &count, "AltIoloadImage", tbl)) {
		for (i = 0; i < count; i++) {
			list_append(bg_conf->ramdisk_list, image_array[i]);
			image_array[i] = NULL;
		}
	}
	if (!s_p_get_string(&bg_conf->default_ramdiskimage,
			    "IoloadImage", tbl)) {
		if (!list_count(bg_conf->ramdisk_list))
			fatal("IoloadImage not configured "
			      "in bluegene.conf");
		itr = list_iterator_create(bg_conf->ramdisk_list);
		image = list_next(itr);
		image->def = true;
		list_iterator_destroy(itr);
		bg_conf->default_ramdiskimage = xstrdup(image->name);
		info("Warning: using %s as the default IoloadImage.  "
		     "If this isn't correct please set IoloadImage",
		     bg_conf->default_ramdiskimage);
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_SELECT_TYPE)
			info("default IoloadImage %s",
			     bg_conf->default_ramdiskimage);
		image = xmalloc(sizeof(image_t));
		image->name = xstrdup(bg_conf->default_ramdiskimage);
		image->def = true;
		image->groups = NULL;
		/* we want it to be first */
		list_push(bg_conf->ramdisk_list, image);
	}

#endif
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

	if (!s_p_get_uint16(&bg_conf->mp_cnode_cnt, "MidplaneNodeCnt", tbl)) {
		if (!s_p_get_uint16(&bg_conf->mp_cnode_cnt,
				    "BasePartitionNodeCnt", tbl)) {
			error("MidplaneNodeCnt not configured in bluegene.conf "
			      "defaulting to 512 as MidplaneNodeCnt");
			bg_conf->mp_cnode_cnt = 512;
		}
	}

	if (bg_conf->mp_cnode_cnt <= 0)
		fatal("You should have more than 0 nodes "
		      "per midplane");
	bg_conf->actual_cnodes_per_mp = bg_conf->mp_cnode_cnt;
	bg_conf->quarter_cnode_cnt = bg_conf->mp_cnode_cnt/4;

	/* bg_conf->cpus_per_mp should had already been set from the
	 * node_init */
	if (bg_conf->cpus_per_mp < bg_conf->mp_cnode_cnt) {
		fatal("For some reason we have only %u cpus per mp, but "
		      "have %u cnodes per mp.  You need at least the same "
		      "number of cpus as you have cnodes per mp.  "
		      "Check the NodeName CPUs= "
		      "definition in the slurm.conf.",
		      bg_conf->cpus_per_mp, bg_conf->mp_cnode_cnt);
	}

	bg_conf->cpu_ratio = bg_conf->cpus_per_mp/bg_conf->mp_cnode_cnt;
	if (!bg_conf->cpu_ratio)
		fatal("We appear to have less than 1 cpu on a cnode.  "
		      "You specified %u for MidplaneNodeCnt "
		      "in the blugene.conf and %u cpus "
		      "for each node in the slurm.conf",
		      bg_conf->mp_cnode_cnt, bg_conf->cpus_per_mp);

	num_unused_cpus = 1;
	for (i = 0; i<SYSTEM_DIMENSIONS; i++)
		num_unused_cpus *= dims[i];
	num_unused_cpus *= bg_conf->cpus_per_mp;
	num_possible_unused_cpus = num_unused_cpus;

	if (!s_p_get_uint16(&bg_conf->nodecard_cnode_cnt,
			    "NodeBoardNodeCnt", tbl)) {
		if (!s_p_get_uint16(&bg_conf->nodecard_cnode_cnt,
				    "NodeCardNodeCnt", tbl)) {
			error("NodeCardNodeCnt not configured in bluegene.conf "
			      "defaulting to 32 as NodeCardNodeCnt");
			bg_conf->nodecard_cnode_cnt = 32;
		}
	}

	if (bg_conf->nodecard_cnode_cnt <= 0)
		fatal("You should have more than 0 nodes per nodecard");

	bg_conf->mp_nodecard_cnt =
		bg_conf->mp_cnode_cnt / bg_conf->nodecard_cnode_cnt;

	if (!s_p_get_uint16(&bg_conf->ionodes_per_mp, "IONodesPerMP", tbl))
		if (!s_p_get_uint16(&bg_conf->ionodes_per_mp, "Numpsets", tbl))
			fatal("Warning: IONodesPerMP not configured "
			      "in bluegene.conf");

	s_p_get_uint16(&bg_conf->max_block_err, "MaxBlockInError", tbl);

	tmp_bool = 0;
	s_p_get_boolean(&tmp_bool, "SubMidplaneSystem", tbl);
	bg_conf->sub_mp_sys = tmp_bool;

#ifdef HAVE_BGQ
	tmp_bool = 0;
	s_p_get_boolean(&tmp_bool, "AllowSubBlockAllocations", tbl);
	bg_conf->sub_blocks = tmp_bool;

	/* You can only have 16 ionodes per midplane */
	if (bg_conf->ionodes_per_mp > bg_conf->mp_nodecard_cnt)
		bg_conf->ionodes_per_mp = bg_conf->mp_nodecard_cnt;
#endif

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		bg_conf->default_conn_type[i] = (uint16_t)NO_VAL;
	s_p_get_string(&tmp_char, "DefaultConnType", tbl);
	if (tmp_char) {
		verify_conn_type(tmp_char, bg_conf->default_conn_type);
		if ((bg_conf->default_conn_type[0] != SELECT_MESH)
		    && (bg_conf->default_conn_type[0] != SELECT_TORUS))
			fatal("Can't have a DefaultConnType of %s "
			      "(only Mesh or Torus values are valid).",
			      tmp_char);
		xfree(tmp_char);
	} else
		bg_conf->default_conn_type[0] = SELECT_TORUS;

#ifndef HAVE_BG_L_P
	int first_conn_type = bg_conf->default_conn_type[0];
	for (i=1; i<SYSTEM_DIMENSIONS; i++) {
		if (bg_conf->default_conn_type[i] == (uint16_t)NO_VAL)
			bg_conf->default_conn_type[i] = first_conn_type;
		else if (bg_conf->default_conn_type[i] >= SELECT_SMALL)
			fatal("Can't have a DefaultConnType of %s "
			      "(only Mesh or Torus values are valid).",
			      tmp_char);
	}
#endif

	if (bg_conf->ionodes_per_mp) {
		bitstr_t *tmp_bitmap = NULL;
		int small_size = 1;

		/* THIS IS A HACK TO MAKE A 1 NODECARD SYSTEM WORK,
		 * Sometime on a Q system the nodecard isn't in the 0
		 * spot so only do this if you know it is in that
		 * spot.  Otherwise say the whole midplane is there
		 * and just make blocks over the whole thing.  They
		 * you can error out the blocks that aren't usable. */
		if (bg_conf->sub_mp_sys
		    && bg_conf->mp_cnode_cnt == bg_conf->nodecard_cnode_cnt) {
#ifdef HAVE_BGQ
			bg_conf->quarter_ionode_cnt = 1;
			bg_conf->nodecard_ionode_cnt = 1;
#else
			bg_conf->quarter_ionode_cnt = 2;
			bg_conf->nodecard_ionode_cnt = 2;
#endif
		} else {
			bg_conf->quarter_ionode_cnt = bg_conf->ionodes_per_mp/4;
			bg_conf->nodecard_ionode_cnt =
				bg_conf->quarter_ionode_cnt/4;
		}

		/* How many nodecards per ionode */
		bg_conf->nc_ratio =
			((double)bg_conf->mp_cnode_cnt
			 / (double)bg_conf->nodecard_cnode_cnt)
			/ (double)bg_conf->ionodes_per_mp;
		/* How many ionodes per nodecard */
		bg_conf->io_ratio =
			(double)bg_conf->ionodes_per_mp /
			((double)bg_conf->mp_cnode_cnt
			 / (double)bg_conf->nodecard_cnode_cnt);

		/* How many cnodes per ionode */
		bg_conf->ionode_cnode_cnt =
			bg_conf->nodecard_cnode_cnt * bg_conf->nc_ratio;

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
			/* This is suppose to be = and not ==, we only
			   want to decrement when small_size equals
			   something.
			*/
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
		if (bg_conf->mp_cnode_cnt == bg_conf->nodecard_cnode_cnt)
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

	if (s_p_get_string(&tmp_char, "DenyPassthrough", tbl)) {
		if (strstr(tmp_char, "A"))
			ba_deny_pass |= PASS_DENY_A;
		if (strstr(tmp_char, "X"))
			ba_deny_pass |= PASS_DENY_X;
		if (strstr(tmp_char, "Y"))
			ba_deny_pass |= PASS_DENY_Y;
		if (strstr(tmp_char, "Z"))
			ba_deny_pass |= PASS_DENY_Z;
		if (!strcasecmp(tmp_char, "ALL"))
			ba_deny_pass |= PASS_DENY_ALL;
		bg_conf->deny_pass = ba_deny_pass;
		xfree(tmp_char);
	}

	if (!s_p_get_string(&tmp_char, "LayoutMode", tbl)) {
		info("Warning: LayoutMode was not specified in bluegene.conf "
		     "defaulting to STATIC partitioning");
		bg_conf->layout_mode = LAYOUT_STATIC;
	} else {
		if (!strcasecmp(tmp_char,"STATIC"))
			bg_conf->layout_mode = LAYOUT_STATIC;
		else if (!strcasecmp(tmp_char,"OVERLAP"))
			bg_conf->layout_mode = LAYOUT_OVERLAP;
		else if (!strcasecmp(tmp_char,"DYNAMIC"))
			bg_conf->layout_mode = LAYOUT_DYNAMIC;
		else {
			fatal("I don't understand this LayoutMode = %s",
			      tmp_char);
		}
		xfree(tmp_char);
	}

	/* add blocks defined in file */
	if (bg_conf->layout_mode != LAYOUT_DYNAMIC) {
		if (!s_p_get_array((void ***)&blockreq_array,
				   &count, "MPs", tbl)) {
			if (!s_p_get_array((void ***)&blockreq_array,
					   &count, "BPs", tbl)) {
				info("WARNING: no blocks defined in "
				     "bluegene.conf, "
				     "only making full system block");
				/* create_full_system_block(NULL); */
				if (bg_conf->sub_mp_sys ||
				    (bg_conf->mp_cnode_cnt ==
				     bg_conf->nodecard_cnode_cnt))
					fatal("On a sub-midplane system you "
					      "need to define the blocks you "
					      "want on your system.");
			}
		}

		for (i = 0; i < count; i++) {
			add_bg_record(bg_lists->main, NULL,
				      blockreq_array[i], 0, 0);
		}
	} else if (bg_conf->sub_mp_sys ||
		   (bg_conf->mp_cnode_cnt == bg_conf->nodecard_cnode_cnt))
		/* we can't do dynamic here on a sub-midplane system */
		fatal("On a sub-midplane system we can only do OVERLAP or "
		      "STATIC LayoutMode.  Please update your bluegene.conf.");

#ifdef HAVE_BGQ
	if ((bg_recover != NOT_FROM_CONTROLLER)
	    && s_p_get_string(&tmp_char, "RebootQOSList", tbl)) {
		bool valid;
		char *token, *last = NULL;
		slurmdb_qos_rec_t *qos = NULL;
		assoc_mgr_lock_t locks = { NO_LOCK, NO_LOCK,
					   READ_LOCK, NO_LOCK,
					   NO_LOCK, NO_LOCK };

		/* Lock here to avoid g_qos_count changing under us */
		assoc_mgr_lock(&locks);
		bg_conf->reboot_qos_bitmap = bit_alloc(g_qos_count);
		itr = list_iterator_create(assoc_mgr_qos_list);

		token = strtok_r(tmp_char, ",", &last);
		while (token) {
			valid = false;
			while((qos = list_next(itr))) {
				if (!strcasecmp(token, qos->name)) {
					bit_set(bg_conf->reboot_qos_bitmap,
						qos->id);
					valid = true;
					break;
				}
			}
			if (!valid)
				error("Invalid RebootQOSList value: %s", token);
			list_iterator_reset(itr);
			token = strtok_r(NULL, ",", &last);
		}
		list_iterator_destroy(itr);
		xfree(tmp_char);
		assoc_mgr_unlock(&locks);
	}
#endif

	s_p_hashtbl_destroy(tbl);

	return SLURM_SUCCESS;
}

extern s_p_hashtbl_t *config_make_tbl(char *filename)
{
	s_p_hashtbl_t *tbl = NULL;

	xassert(filename);

	tbl = s_p_hashtbl_create(bg_conf_file_options);

	if (s_p_parse_file(tbl, NULL, filename, false) == SLURM_ERROR) {
		s_p_hashtbl_destroy(tbl);
		tbl = NULL;
	}

	return tbl;
}
