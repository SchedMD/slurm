/*****************************************************************************\
 *  layout.c - layout data structures and main functions
 *****************************************************************************
 *  Initially written by Francois Chevallier <chevallierfrancois@free.fr>
 *  at Bull for slurm-2.6.
 *  Adapted by Matthieu Hautreux <matthieu.hautreux@cea.fr> for slurm-14.11.
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "src/common/layout.h"
#include "src/common/xstring.h"
#include "src/common/xmalloc.h"

void layout_init(layout_t* layout, const char* name, const char* type,
		 uint32_t priority, int struct_type)
{
	layout->name = xstrdup(name);
	layout->type = xstrdup(type);
	layout->priority = priority;
	layout->struct_type = struct_type;
	switch(layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		layout->tree = (xtree_t*)xmalloc(sizeof(xtree_t));
		xtree_init(layout->tree, NULL);
		break;
	}
}

void layout_free(layout_t* layout)
{
	xfree(layout->name);
	xfree(layout->type);
	switch(layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		xtree_free(layout->tree);
		xfree(layout->tree);
		break;
	}
}

const char* layout_get_name(const layout_t* layout)
{
	return layout->name;
}

const char* layout_get_type(const layout_t* layout)
{
	return layout->type;
}

uint32_t layout_get_priority(const layout_t* layout)
{
	return layout->priority;
}

void layout_node_delete(layout_t* layout, void* node)
{
	switch(layout->struct_type) {
	case LAYOUT_STRUCT_TREE:
		xtree_delete(layout->tree, (xtree_node_t*)node);
		break;
	}
}

xtree_t* layout_get_tree(layout_t* layout)
{
	if (layout->struct_type == LAYOUT_STRUCT_TREE) {
		return layout->tree;
	}
	fatal("layout has unknown relationnal structure type");
	return NULL;
}

const char* layout_hashable_identify(void* item) {
	layout_t* l = (layout_t*)item;
	return l->name;
}

void layout_hashable_identify_by_type(void* item, const char** key,
				      uint32_t* key_len) {
	layout_t* l = (layout_t*)item;
	*key = l->type;
	*key_len = strlen(l->type);
}
