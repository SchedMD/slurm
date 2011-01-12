/*****************************************************************************\
 *  bridge_linker.c
 *
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>, Danny Auble <da@llnl.gov>
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


#include "bridge_linker.h"

#if defined HAVE_BG_FILES && defined HAVE_BGQ
#ifdef __cplusplus
extern "C" {
#endif

using namespace std;
using namespace bgsched;
using namespace bgsched::core;

pthread_mutex_t api_file_mutex = PTHREAD_MUTEX_INITIALIZER;
bridge_api_t bridge_api;
bool initialized = false;
bool have_db2 = true;
void *handle = NULL;

static void _b_midplane_del(void *object)
{
	b_midplane_t *b_midplane = (b_midplane_t *)object;

	if (b_midplane) {
		xfree(b_midplane->bp_id);
		xfree(b_midplane);
	}
}

extern int bridge_init(char *properties_file)
{
	if (initialized)
		return 1;

	bgsched::init(properties_file);


	initialized = true;

	return 1;

}

extern int bridge_fini()
{
	initialized = false;

	return SLURM_SUCCESS;
}

extern int bridge_get_bg(my_bluegene_t **bg)
{
	int rc = SLURM_ERROR;
	if (!bridge_init(NULL))
		return rc;
	try {
		ComputeHardware::ConstPtr bgq = getComputeHardware();
		&bg = (void *)bgq;
		rc = SLURM_SUCCESS;
	} catch (...) { // Handle all exceptions
		error(" Unexpected error calling getComputeHardware");
		&bg = NULL;
	}

	return rc;
}

extern uint16_t *bridge_get_size(my_bluegene_t *bg)
{
	uint16_t size[SYSTEM_DIMENSIONS];
	int rc = SLURM_ERROR;
	if (!bridge_init(NULL))
		return NULL;

	for (i=0; i<SYSTEM_DIMENSIONS; i++)
		size[i] = bgq->getMidplaneSize(i);

	return size;
}

extern List bridge_get_map(my_bluegene_t *bg)
{
	int a, b, c, d;
	List b_midplane_list = list_create(_bp_map_list_del);

	for (a = 0; a < bgq->getMachineSize(Dimension::A); ++a)
		for (b = 0; b < bgq->getMachineSize(Dimension::B); ++b)
			for (c = 0; c < bgq->getMachineSize(Dimension::C); ++c)
				for (d = 0; d < bgq->getMachineSize(Dimension::D); ++d) {
					Midplane::Coordinates coords =
						{{a, b, c, d}};
					Midplane::ConstPtr midplane =
						bgq->getMidplane(coords);
					b_midplane_t *b_midplane =
						xmalloc(sizeof(b_midplane_t));

					list_append(bp_map_list, b_midplane);
					b_midplane->midplane = midplane;
					b_midplane->loc =
						midplane->getLocation();
					b_midplane->coord[A] = a;
					b_midplane->coord[B] = b;
					b_midplane->coord[C] = c;
					b_midplane->coord[D] = d;
				}
	return b_midplane_list;
}

extern int bridge_create_block(bg_record_t *bg_record)
{
	Block::Ptr block_ptr;
	Block::Midplanes midplanes;
        Block::PassthroughMidplanes pt_midplanes;
        Block::DimensionConnectivity conn_type;
	ListIterator itr = NULL;
	Midplane::ConstPtr midplane;
	int i;
	int rc = SLURM_SUCCESS;

	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (bg_record->block_ptr)
		return SLURM_ERROR;

	if (bg_record->small) {
		info("we can't make small blocks yet");
		return SLURM_ERROR;
	}

	if (!bg_record->bg_midplanes || !list_count(bg_record->bg_midplanes)) {
		error("There are no midplanes in this block?");
		return SLURM_ERROR;
	}

	itr = list_iterator_create(bg_record->bg_midplanes);
	while ((midplane == (Midplane::ConstPtr)list_next(itr))) {
		midplanes.push_back(midplane->getLocation());
	}
	list_iterator_destroy(itr);

	itr = list_iterator_create(bg_record->bg_pt_midplanes);
	while ((midplane == (Midplane::ConstPtr)list_next(itr))) {
		pt_midplanes.push_back(midplane->getLocation());
	}
	list_iterator_destroy(itr);

        for (i=A; i<D; i++)
		conn_type[dim] = bg_record->conn_type[i];

	block_ptr = Block::create(midplanes, pt_midplanes, conn_type);
	block_ptr->setName(bg_record->bg_block_id);
	block_ptr->addUser(bg_record->bg_block_id, bg_record->user_name);
	block_ptr->add(NULL);

	midplanes.clear();
        pt_midplanes.clear();
        conn_type.clear();

	bg_record->block_ptr = block_ptr;

	return rc;
}

extern int bridge_boot_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::initiateBoot(name);
	} catch(...) {
                error("Boot block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int bridge_free_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::initiateFree(name);
	} catch(...) {
                error("Free block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}

extern int bridge_remove_block(char *name)
{
	int rc = SLURM_SUCCESS;
	if (!bridge_init(NULL))
		return SLURM_ERROR;

	if (!name)
		return SLURM_ERROR;

        try {
		Block::remove(name);
	} catch(...) {
                error("Remove block request failed ... continuing.");
		rc = SLURM_ERROR;
	}

	return rc;
}


// extern int bridge_set_log_params(char *api_file_name, unsigned int level)
// {
// 	static FILE *fp = NULL;
//         FILE *fp2 = NULL;
// 	int rc = SLURM_SUCCESS;

// 	if (!bridge_init())
// 		return SLURM_ERROR;

// 	slurm_mutex_lock(&api_file_mutex);
// 	if (fp)
// 		fp2 = fp;

// 	fp = fopen(api_file_name, "a");

// 	if (fp == NULL) {
// 		error("can't open file for bridgeapi.log at %s: %m",
// 		      api_file_name);
// 		rc = SLURM_ERROR;
// 		goto end_it;
// 	}


// 	(*(bridge_api.set_log_params))(fp, level);
// 	/* In the libraries linked to from the bridge there are stderr
// 	   messages send which we would miss unless we dup this to the
// 	   log */
// 	//(void)dup2(fileno(fp), STDERR_FILENO);

// 	if (fp2)
// 		fclose(fp2);
// end_it:
// 	slurm_mutex_unlock(&api_file_mutex);
//  	return rc;
// }

#ifdef __cplusplus
}
#endif

#endif /* HAVE_BG_FILES */


