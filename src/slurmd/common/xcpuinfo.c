/*****************************************************************************\
 *  xcpuinfo.c - cpuinfo related primitives
 *****************************************************************************
 *  Copyright (C) 2009 CEA/DAM/DIF
 *  Portions (hwloc) copyright (C) 2012 Bull, <rod.schultz@bull.com>
 *  Written by Matthieu Hautreux <matthieu.hautreux@cea.fr>
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

#include "config.h"

#define _GNU_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"
#include "src/common/log.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmd/get_mach_stat.h"
#include "src/slurmd/slurmd/slurmd.h"
#include "src/common/read_config.h"

#ifdef HAVE_HWLOC
#include <hwloc.h>
#endif

#include "xcpuinfo.h"

#define _DEBUG 0
#define _MAX_SOCKET_INX 1024

#if !defined(HAVE_HWLOC)
static char* _cpuinfo_path = "/proc/cpuinfo";

static int _compute_block_map(uint16_t numproc,
			      uint16_t **block_map, uint16_t **block_map_inv);
static int _chk_cpuinfo_str(char *buffer, char *keyword, char **valptr);
static int _chk_cpuinfo_uint32(char *buffer, char *keyword, uint32_t *val);
#endif

static int _range_to_map(char* range, uint16_t *map, uint16_t map_size,
			 int add_threads);

bool     initialized = false;
uint16_t procs, boards, sockets, cores, threads=1;
uint16_t block_map_size;
uint16_t *block_map, *block_map_inv;
extern slurmd_conf_t *conf;

/*
 * get_procs - Return the count of procs on this system
 * Input: procs - buffer for the CPU count
 * Output: procs - filled in with CPU count, "1" if error
 *         return code - 0 if no error, otherwise errno
 */
extern int
get_procs(uint16_t *procs)
{
#ifdef _SC_NPROCESSORS_ONLN
	int my_proc_tally;

	*procs = 1;
	my_proc_tally = (int)sysconf(_SC_NPROCESSORS_ONLN);
	if (my_proc_tally < 1) {
		error ("get_procs: error running sysconf(_SC_NPROCESSORS_ONLN)");
		return EINVAL;
	}

	*procs = (uint16_t) my_proc_tally;
#elif defined (HAVE_SYSCTLBYNAME)
	int ncpu;
	size_t len = sizeof(ncpu);

	*procs = 1;
	if (sysctlbyname("hw.ncpus", &ncpu, &len, NULL, 0) == -1) {
		error("get_procs: error running sysctl(HW_NCPU)");
		return EINVAL;
	}
	*procs = (uint16_t) ncpu;
#else
	*procs = 1;
#endif

	return 0;
}

#ifdef HAVE_HWLOC

static char *hwloc_xml_whole = NULL;

#if _DEBUG
static void _hwloc_children(hwloc_topology_t topology, hwloc_obj_t obj,
			    int depth)
{
	char string[128];
	unsigned i;

	if (!obj)
		return;
	hwloc_obj_type_snprintf(string, sizeof(string), obj, 0);
	debug("%*s%s", 2 * depth, "", string);
	for (i = 0; i < obj->arity; i++) {
		_hwloc_children(topology, obj->children[i], depth + 1);
	}
}
#endif

/* Return the number of cores which are decentdents of the given objecdt */
static int _core_child_count(hwloc_topology_t topology, hwloc_obj_t obj)
{
	int count = 0, i;

	if (obj->type == HWLOC_OBJ_CORE)
		return 1;

	for (i = 0; i < obj->arity; i++)
		count += _core_child_count(topology, obj->children[i]);
	return count;
}

static inline int _internal_hwloc_topology_export_xml(
	hwloc_topology_t topology, const char *hwloc_xml)
{
#if HWLOC_API_VERSION >= 0x00020000
	return hwloc_topology_export_xml(topology, hwloc_xml, 0);
#else
	return hwloc_topology_export_xml(topology, hwloc_xml);
#endif
}

static void _remove_ecores(hwloc_topology_t *topology)
{
#if HWLOC_API_VERSION > 0x00020401
	int type_cnt;
	hwloc_bitmap_t cpuset, cpuset_tot = NULL;

	if (xstrcasestr(slurm_conf.slurmd_params, "allow_ecores"))
		return;

	if (!(type_cnt = hwloc_cpukinds_get_nr(*topology, 0)))
		return;

	/*
	 * Handle the removal of Intel E-Cores here.
	 *
	 * At the time of writing this Intel Gen 12+ procs have introduced what
	 * are known as 'P' (performance) and 'E' (efficiency) cores. The
	 * former can have hyperthreads, where the latter are only single
	 * threaded, thus creating a situation where we could get a
	 * heterogeneous socket (which Slurm doesn't like). Here we can restrict
	 * to only  "IntelCore" (P-Cores) and disregard the "IntelAtom"
	 * (E-Cores).
	 *
	 * In the future, if desired, we should probably figure out a way to
	 * handle these E-Cores through a core spec instead.
	 *
	 * This logic should do nothing on any other existing processor.
	 */
	cpuset = hwloc_bitmap_alloc();
	for (int i = 0; i < type_cnt; i++) {
		unsigned nr_infos = 0;
		struct hwloc_info_s *infos;
		if (hwloc_cpukinds_get_info(
			    *topology, i, cpuset, NULL, &nr_infos, &infos, 0))
			fatal("Error getting info from hwloc_cpukinds_get_info() %m");

		for (int j = 0; j < nr_infos; j++) {
			if (!xstrcasecmp(infos[j].name, "CoreType") &&
			    !xstrcasecmp(infos[j].value, "IntelCore")) {
				/* Restrict the node to only IntelCores */
				if (!cpuset_tot)
					cpuset_tot = hwloc_bitmap_alloc();
				hwloc_bitmap_or(cpuset_tot, cpuset_tot, cpuset);
			}
		}

		/*
		 * If we have a cpuset_tot it means we are on a system with
		 * IntelCore cpus. We will restrict to only those and be done
		 * here.
		 */
		if (cpuset_tot) {
			hwloc_topology_restrict(*topology, cpuset_tot, 0);
			hwloc_bitmap_free(cpuset_tot);
			break;
		}
	}
	hwloc_bitmap_free(cpuset);

#endif
}

/* read or load topology and write if needed
 * init and destroy topology must be outside this function */
extern int xcpuinfo_hwloc_topo_load(
	void *topology_in, char *topo_file, bool full)
{
	int ret = SLURM_SUCCESS;
	struct stat buf;
	hwloc_topology_t *topology = topology_in;
	hwloc_topology_t tmp_topo;
	static bool first_full = true;
	bool check_file = true;

	xassert(topo_file);

	if (!topology_in) {
		topology = &tmp_topo;
		goto handle_write;
	}

	if (full && first_full) {
		/* Always regenerate file on slurmd startup */
		if (running_in_slurmd())
			check_file = false;
		first_full = false;
	}

	if (check_file && !stat(topo_file, &buf)) {
		debug2("%s: xml file (%s) found", __func__, topo_file);
		if (hwloc_topology_set_xml(*topology, topo_file))
			error("%s: hwloc_topology_set_xml() failed (%s)",
			      __func__, topo_file);
		else if (hwloc_topology_load(*topology))
			error("%s: hwloc_topology_load() failed (%s)",
			      __func__, topo_file);
		else
			return ret;
	}

	hwloc_topology_destroy(*topology);

handle_write:

	hwloc_topology_init(topology);

	if (full) {
		/* parse all system */
		hwloc_topology_set_flags(*topology,
					 HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM);

		/* ignores cache, misc */
#if HWLOC_API_VERSION < 0x00020000
		hwloc_topology_ignore_type (*topology, HWLOC_OBJ_CACHE);
		hwloc_topology_ignore_type (*topology, HWLOC_OBJ_MISC);
#else
		hwloc_topology_set_type_filter(*topology, HWLOC_OBJ_L1CACHE,
					       HWLOC_TYPE_FILTER_KEEP_NONE);
		hwloc_topology_set_type_filter(*topology, HWLOC_OBJ_L2CACHE,
					       HWLOC_TYPE_FILTER_KEEP_NONE);
		/* need to preserve HWLOC_OBJ_L3CACHE for l3cache_as_socket */
		hwloc_topology_set_type_filter(*topology, HWLOC_OBJ_L4CACHE,
					       HWLOC_TYPE_FILTER_KEEP_NONE);
		hwloc_topology_set_type_filter(*topology, HWLOC_OBJ_L5CACHE,
					       HWLOC_TYPE_FILTER_KEEP_NONE);
		hwloc_topology_set_type_filter(*topology, HWLOC_OBJ_MISC,
					       HWLOC_TYPE_FILTER_KEEP_NONE);
#endif
	}

	/* load topology */
	debug2("hwloc_topology_load");
	if (hwloc_topology_load(*topology)) {
		/* error in load hardware topology */
		debug("hwloc_topology_load() failed.");
		ret = SLURM_ERROR;
		goto end_it;
	}

	_remove_ecores(topology);

	if (!conf->def_config) {
		debug2("hwloc_topology_export_xml");
		if (_internal_hwloc_topology_export_xml(*topology, topo_file)) {
			/* error in export hardware topology */
			error("%s: failed (load will be required after read failures).", __func__);
		}
	}

end_it:
	if (!topology_in)
		hwloc_topology_destroy(tmp_topo);

	return ret;
}

/*
 * xcpuinfo_hwloc_topo_get - Return detailed cpuinfo on the whole system
 * Output: p_cpus - number of processors on the system
 *         p_boards - number of baseboards (containing sockets)
 *         p_sockets - number of physical processor sockets
 *         p_cores - total number of physical CPU cores
 *         p_threads - total number of hardware execution threads
 *         block_map - asbtract->physical block distribution map
 *         block_map_inv - physical->abstract block distribution map (inverse)
 *         return code - 0 if no error, otherwise errno
 * NOTE: User must xfree block_map and block_map_inv
 */
extern int xcpuinfo_hwloc_topo_get(
	uint16_t *p_cpus, uint16_t *p_boards,
	uint16_t *p_sockets, uint16_t *p_cores, uint16_t *p_threads,
	uint16_t *p_block_map_size,
	uint16_t **p_block_map, uint16_t **p_block_map_inv)
{
	enum { SOCKET=0, CORE=1, PU=2, LAST_OBJ=3 };
	hwloc_topology_t topology;
	hwloc_obj_t obj;
	hwloc_obj_type_t objtype[LAST_OBJ];
	unsigned idx[LAST_OBJ];
	int nobj[LAST_OBJ];
	bitstr_t *used_socket = NULL;
	int *cores_per_socket;
	int actual_cpus;
	int macid;
	int absid;
	int actual_boards = 1, depth, sock_cnt, tot_socks = 0;
	int i, used_core_idx, used_sock_idx;

	debug2("hwloc_topology_init");
	if (hwloc_topology_init(&topology)) {
		/* error in initialize hwloc library */
		debug("hwloc_topology_init() failed.");
		return 1;
	}

	if (!hwloc_xml_whole)
		hwloc_xml_whole = xstrdup_printf("%s/hwloc_topo_whole.xml",
						 conf->spooldir);
	if (xcpuinfo_hwloc_topo_load(&topology, hwloc_xml_whole, true)
	    == SLURM_ERROR) {
		hwloc_topology_destroy(topology);
		xfree(hwloc_xml_whole);
		return 2;
	}
#if _DEBUG
	_hwloc_children(topology, hwloc_get_root_obj(topology), 0);
#endif
	/*
	 * Some processors (e.g. AMD Opteron 6000 series) contain multiple
	 * NUMA nodes per socket. This is a configuration which does not map
	 * into the hardware entities that Slurm optimizes resource allocation
	 * for (PU/thread, core, socket, baseboard, node and network switch).
	 * In order to optimize resource allocations on such hardware, Slurm
	 * will consider each NUMA node within the socket as a separate socket.
	 * You can disable this configuring "SchedulerParameters=Ignore_NUMA",
	 * in which case Slurm will report the correct socket count on the node,
	 * but not be able to optimize resource allocations on the NUMA nodes.
	 */
	objtype[SOCKET] = HWLOC_OBJ_SOCKET;
	objtype[CORE]   = HWLOC_OBJ_CORE;
	objtype[PU]     = HWLOC_OBJ_PU;
#if HWLOC_API_VERSION >= 0x00020000
	if (xstrcasestr(slurm_conf.sched_params, "Ignore_NUMA")) {
		info("SchedulerParamaters=Ignore_NUMA not supported by hwloc v2");
	}
#else
	if (hwloc_get_type_depth(topology, HWLOC_OBJ_NODE) >
	    hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET)) {
		if (xstrcasestr(slurm_conf.sched_params, "Ignore_NUMA")) {
			info("Ignoring NUMA nodes within a socket");
		} else {
			info("Considering each NUMA node as a socket");
			objtype[SOCKET] = HWLOC_OBJ_NODE;
		}
	}
#endif

	if (xstrcasestr(slurm_conf.slurmd_params, "l3cache_as_socket")) {
#if HWLOC_API_VERSION >= 0x00020000
		objtype[SOCKET] = HWLOC_OBJ_L3CACHE;
#else
		error("SlurmdParameters=l3cache_as_socket requires hwloc v2");
#endif
	} else if (xstrcasestr(slurm_conf.slurmd_params,
			       "numa_node_as_socket")) {
#if HWLOC_API_VERSION >= 0x00020000
		hwloc_obj_t numa_obj = hwloc_get_next_obj_by_type(
			topology, HWLOC_OBJ_NODE, NULL);

		if (numa_obj && numa_obj->parent) {
			objtype[SOCKET] = numa_obj->parent->type;
			if (get_log_level() >= LOG_LEVEL_DEBUG2) {
				char tmp[128];
				hwloc_obj_type_snprintf(tmp, sizeof(tmp),
							numa_obj->parent, 0);
				debug2("%s: numa_node_as_socket mapped to '%s'",
				       __func__, tmp);
			}
		}
#else
		error("SlurmdParameters=numa_node_as_socket requires hwloc v2");
#endif
	}

	/* Groups below root obj are interpreted as boards */
	obj = hwloc_get_root_obj(topology);
	obj = hwloc_get_next_child(topology, obj, NULL);
	if (!hwloc_compare_types(HWLOC_OBJ_GROUP, obj->type))
		actual_boards =
			MAX(hwloc_get_nbobjs_by_depth(topology, obj->depth), 1);

	/*
	 * Count sockets/NUMA containing any cores.
	 * KNL NUMA with no cores are NOT counted.
	 */
	nobj[SOCKET] = 0;
	depth = hwloc_get_type_depth(topology, objtype[SOCKET]);
	used_socket = bit_alloc(_MAX_SOCKET_INX);
	cores_per_socket = xmalloc(sizeof(int) * _MAX_SOCKET_INX);
	sock_cnt = hwloc_get_nbobjs_by_depth(topology, depth);
	for (i = 0; i < sock_cnt; i++) {
		obj = hwloc_get_obj_by_depth(topology, depth, i);
		if (obj->type == objtype[SOCKET]) {
			cores_per_socket[i] = _core_child_count(topology, obj);
			if (cores_per_socket[i] > 0) {
				nobj[SOCKET]++;
				bit_set(used_socket, tot_socks);
			}
			if (++tot_socks >= _MAX_SOCKET_INX) {	/* Bitmap size */
				fatal("Socket count exceeds %d, expand data structure size",
				      _MAX_SOCKET_INX);
				break;
			}
		}
	}

	nobj[CORE] = hwloc_get_nbobjs_by_type(topology, objtype[CORE]);

	/*
	 * Workaround for hwloc bug, in some cases the topology "children" array
	 * does not get populated, so _core_child_count() always returns 0
	 */
	if (nobj[SOCKET] == 0) {
		nobj[SOCKET] = hwloc_get_nbobjs_by_type(topology,
							objtype[SOCKET]);
		if (nobj[SOCKET] == 0) {
			debug("%s: fudging nobj[SOCKET] from 0 to 1", __func__);
			nobj[SOCKET] = 1;
		}
		if (nobj[SOCKET] >= _MAX_SOCKET_INX) {	/* Bitmap size */
			fatal("Socket count exceeds %d, expand data structure size",
			      _MAX_SOCKET_INX);
		}
		bit_nset(used_socket, 0, nobj[SOCKET] - 1);
	}

	/*
	 * Workaround for hwloc
	 * hwloc_get_nbobjs_by_type() returns 0 on some architectures.
	 */
	if ( nobj[CORE] == 0 ) {
		debug("%s: fudging nobj[CORE] from 0 to 1", __func__);
		nobj[CORE] = 1;
	}
	if ( nobj[SOCKET] == -1 )
		fatal("%s: can not handle nobj[SOCKET] = -1", __func__);
	if ( nobj[CORE] == -1 )
		fatal("%s: can not handle nobj[CORE] = -1", __func__);
	actual_cpus  = hwloc_get_nbobjs_by_type(topology, objtype[PU]);
#if 0
	/* Used to find workaround above */
	info("CORE = %d SOCKET = %d actual_cpus = %d nobj[CORE] = %d",
	     CORE, SOCKET, actual_cpus, nobj[CORE]);
#endif
	if ((actual_cpus % nobj[CORE]) != 0) {
		error("Thread count (%d) not multiple of core count (%d)",
		      actual_cpus, nobj[CORE]);
	}
	nobj[PU] = actual_cpus / nobj[CORE];	/* threads per core */

	if ((nobj[CORE] % nobj[SOCKET]) != 0) {
		error("Core count (%d) not multiple of socket count (%d)",
		      nobj[CORE], nobj[SOCKET]);
	}
	nobj[CORE] /= nobj[SOCKET];		/* cores per socket */

	debug("CPUs:%d Boards:%d Sockets:%d CoresPerSocket:%d ThreadsPerCore:%d",
	      actual_cpus, actual_boards, nobj[SOCKET], nobj[CORE], nobj[PU]);

	/* allocate block_map */
	if (p_block_map_size)
		*p_block_map_size = (uint16_t)actual_cpus;
	if (p_block_map && p_block_map_inv) {
		*p_block_map     = xmalloc(actual_cpus * sizeof(uint16_t));
		*p_block_map_inv = xmalloc(actual_cpus * sizeof(uint16_t));

		/* initialize default as linear mapping */
		for (i = 0; i < actual_cpus; i++) {
			(*p_block_map)[i]     = i;
			(*p_block_map_inv)[i] = i;
		}
		/* create map with hwloc */
		used_sock_idx = -1;
		used_core_idx = -1;
		for (idx[SOCKET] = 0; (used_sock_idx + 1) < nobj[SOCKET];
		     idx[SOCKET]++) {
			if (!bit_test(used_socket, idx[SOCKET]))
				continue;
			used_sock_idx++;
			for (idx[CORE] = 0;
			     idx[CORE] < cores_per_socket[idx[SOCKET]];
			     idx[CORE]++) {
				used_core_idx++;
				for (idx[PU]=0; idx[PU]<nobj[PU]; ++idx[PU]) {
					/* get hwloc_obj by indexes */
					obj=hwloc_get_obj_below_array_by_type(
						topology, 3, objtype, idx);
					if (!obj)
						continue;
					macid = obj->os_index;
					absid = used_core_idx * nobj[PU] + idx[PU];

					if ((macid >= actual_cpus) ||
					    (absid >= actual_cpus)) {
						/* physical or logical ID are
						 * out of range */
						continue;
					}
					debug4("CPU map[%d]=>%d S:C:T %d:%d:%d", absid, macid,
					       used_sock_idx, idx[CORE], idx[PU]);
					(*p_block_map)[absid]     = macid;
					(*p_block_map_inv)[macid] = absid;
				}
			}
		}
	}
	FREE_NULL_BITMAP(used_socket);
	xfree(cores_per_socket);
	hwloc_topology_destroy(topology);

	/* update output parameters */
	*p_cpus    = actual_cpus;
	*p_boards  = actual_boards;
	*p_sockets = nobj[SOCKET];
	*p_cores   = nobj[CORE];
	*p_threads = nobj[PU];

#if _DEBUG
	/*** Display raw data ***/
	debug("CPUs:%u Boards:%u Sockets:%u CoresPerSocket:%u ThreadsPerCore:%u",
	      *p_cpus, *p_boards, *p_sockets, *p_cores, *p_threads);

	/* Display the mapping tables */
	if (p_block_map && p_block_map_inv) {
		debug("------");
		debug("Abstract -> Machine logical CPU ID block mapping:");
		debug("AbstractId PhysicalId Inverse");
		for (i = 0; i < *p_cpus; i++) {
			debug3("   %4d      %4u       %4u",
			       i, (*p_block_map)[i], (*p_block_map_inv)[i]);
		}
		debug("------");
	}
#endif
	return SLURM_SUCCESS;

}
#else

typedef struct cpuinfo {
	uint16_t seen;
	uint32_t cpuid;
	uint32_t physid;
	uint16_t physcnt;
	uint32_t coreid;
	uint16_t corecnt;
	uint16_t siblings;
	uint16_t cores;
} cpuinfo_t;
static cpuinfo_t *cpuinfo = NULL; /* array of CPU information for get_cpuinfo */
				  /* Note: file static for qsort/_compare_cpus*/

extern int xcpuinfo_hwloc_topo_get(
	uint16_t *p_cpus, uint16_t *p_boards,
	uint16_t *p_sockets, uint16_t *p_cores, uint16_t *p_threads,
	uint16_t *p_block_map_size,
	uint16_t **p_block_map, uint16_t **p_block_map_inv)
{
	int retval;

	uint16_t numproc;
	uint16_t numcpu	   = 0;		/* number of cpus seen */
	uint16_t numphys   = 0;		/* number of unique "physical id"s */
	uint16_t numcores  = 0;		/* number of unique "cores id"s */

	uint16_t maxsibs   = 0;		/* maximum value of "siblings" */
	uint16_t maxcores  = 0;		/* maximum value of "cores" */
	uint16_t minsibs   = 0xffff;	/* minimum value of "siblings" */
	uint16_t mincores  = 0xffff;	/* minimum value of "cores" */

	uint32_t maxcpuid  = 0;		/* maximum CPU ID ("processor") */
	uint32_t maxphysid = 0;		/* maximum "physical id" */
	uint32_t maxcoreid = 0;		/* maximum "core id" */
	uint32_t mincpuid  = 0xffffffff;/* minimum CPU ID ("processor") */
	uint32_t minphysid = 0xffffffff;/* minimum "physical id" */
	uint32_t mincoreid = 0xffffffff;/* minimum "core id" */
	int i;
	FILE *cpu_info_file;
	char buffer[128];
	uint16_t curcpu, sockets, cores, threads;

	get_procs(&numproc);
	*p_cpus = numproc;
	*p_boards = 1;		/* Boards not identified from /proc/cpuinfo */
	*p_sockets = numproc;	/* initially all single core/thread */
	*p_cores   = 1;
	*p_threads = 1;
	*p_block_map_size = 0;
	*p_block_map      = NULL;
	*p_block_map_inv  = NULL;

	cpu_info_file = fopen(_cpuinfo_path, "r");
	if (cpu_info_file == NULL) {
		error ("%s: error %d opening %s",
		       __func__, errno, _cpuinfo_path);
		return errno;
	}

	/* Note: assumes all processor IDs are within [0:numproc-1] */
	/*       treats physical/core IDs as tokens, not indices */
	if (cpuinfo)
		memset(cpuinfo, 0, numproc * sizeof(cpuinfo_t));
	else
		cpuinfo = xmalloc(numproc * sizeof(cpuinfo_t));

	curcpu = 0;
	while (fgets(buffer, sizeof(buffer), cpu_info_file) != NULL) {
		uint32_t val;
		if (_chk_cpuinfo_uint32(buffer, "processor", &val)) {
			curcpu = numcpu;
			numcpu++;
			if (curcpu >= numproc) {
				info("processor limit reached (%u >= %d)",
				     curcpu, numproc);
				continue;
			}
			cpuinfo[curcpu].seen = 1;
			cpuinfo[curcpu].cpuid = val;
			maxcpuid = MAX(maxcpuid, val);
			mincpuid = MIN(mincpuid, val);
		} else if (_chk_cpuinfo_uint32(buffer, "physical id", &val)) {
			/* see if the ID has already been seen */
			for (i=0; i<numproc; i++) {
				if ((cpuinfo[i].physid == val)
				    &&  (cpuinfo[i].physcnt))
					break;
			}

			if (i == numproc) {		/* new ID... */
				numphys++;		/* ...increment total */
			} else {			/* existing ID... */
				cpuinfo[i].physcnt++;	/* ...update ID cnt */
			}

			if (curcpu < numproc) {
				cpuinfo[curcpu].physcnt++;
				cpuinfo[curcpu].physid = val;
			}

			maxphysid = MAX(maxphysid, val);
			minphysid = MIN(minphysid, val);
		} else if (_chk_cpuinfo_uint32(buffer, "core id", &val)) {
			/* see if the ID has already been seen */
			for (i = 0; i < numproc; i++) {
				if ((cpuinfo[i].coreid == val)
				    &&  (cpuinfo[i].corecnt))
					break;
			}

			if (i == numproc) {		/* new ID... */
				numcores++;		/* ...increment total */
			} else {			/* existing ID... */
				cpuinfo[i].corecnt++;	/* ...update ID cnt */
			}

			if (curcpu < numproc) {
				cpuinfo[curcpu].corecnt++;
				cpuinfo[curcpu].coreid = val;
			}

			maxcoreid = MAX(maxcoreid, val);
			mincoreid = MIN(mincoreid, val);
		} else if (_chk_cpuinfo_uint32(buffer, "siblings", &val)) {
			/* Note: this value is a count, not an index */
		    	if (val > numproc) {	/* out of bounds, ignore */
				debug("siblings is %u (> %d), ignored",
				      val, numproc);
				continue;
			}
			if (curcpu < numproc)
				cpuinfo[curcpu].siblings = val;
			maxsibs = MAX(maxsibs, val);
			minsibs = MIN(minsibs, val);
		} else if (_chk_cpuinfo_uint32(buffer, "cpu cores", &val)) {
			/* Note: this value is a count, not an index */
		    	if (val > numproc) {	/* out of bounds, ignore */
				debug("cores is %u (> %d), ignored",
				      val, numproc);
				continue;
			}
			if (curcpu < numproc)
				cpuinfo[curcpu].cores = val;
			maxcores = MAX(maxcores, val);
			mincores = MIN(mincores, val);
		}
	}

	fclose(cpu_info_file);

	/*** Sanity check ***/
	if (minsibs == 0) minsibs = 1;		/* guaranteee non-zero */
	if (maxsibs == 0) {
	    	minsibs = 1;
	    	maxsibs = 1;
	}
	if (maxcores == 0) {			/* no core data */
	    	mincores = 0;
	    	maxcores = 0;
	}

	/*** Compute Sockets/Cores/Threads ***/
	if ((minsibs == maxsibs) &&		/* homogeneous system */
	    (mincores == maxcores)) {
		sockets = numphys; 		/* unique "physical id" */
		if (sockets <= 1) {		/* verify single socket */
			sockets = numcpu / maxsibs; /* maximum "siblings" */
		}
		if (sockets == 0)
			sockets = 1;		/* guarantee non-zero */

		cores = numcores / sockets;	/* unique "core id" */
		cores = MAX(maxcores, cores);	/* maximum "cpu cores" */

		if (cores == 0) {
			cores = numcpu / sockets;	/* assume multi-core */
			if (cores > 1) {
				debug3("cpuinfo missing 'core id' or 'cpu cores' but assuming multi-core");
			}
		}
		if (cores == 0)
			cores = 1;	/* guarantee non-zero */

		threads = numcpu / (sockets * cores); /* solve for threads */
		if (threads == 0)
			threads = 1;	/* guarantee non-zero */
	} else {				/* heterogeneous system */
		sockets = numcpu;
		cores   = 1;			/* one core per socket */
		threads = 1;			/* one core per core */
	}

	*p_sockets = sockets;		/* update output parameters */
	*p_cores   = cores;
	*p_threads = threads;

#if _DEBUG
	/*** Display raw data ***/
	debug3("numcpu:     %u", numcpu);
	debug3("numphys:    %u", numphys);
	debug3("numcores:   %u", numcores);

	debug3("cores:      %u->%u", mincores, maxcores);
	debug3("sibs:       %u->%u", minsibs,  maxsibs);

	debug3("cpuid:      %u->%u", mincpuid,  maxcpuid);
	debug3("physid:     %u->%u", minphysid, maxphysid);
	debug3("coreid:     %u->%u", mincoreid, maxcoreid);

	for (i = 0; i < numproc; i++) {
		debug3("CPU %d:", i);
		debug3(" cpuid:    %u", cpuinfo[i].cpuid);
		debug3(" seen:     %u", cpuinfo[i].seen);
		debug3(" physid:   %u", cpuinfo[i].physid);
		debug3(" physcnt:  %u", cpuinfo[i].physcnt);
		debug3(" siblings: %u", cpuinfo[i].siblings);
		debug3(" cores:    %u", cpuinfo[i].cores);
		debug3(" coreid:   %u", cpuinfo[i].coreid);
		debug3(" corecnt:  %u\n", cpuinfo[i].corecnt);
	}

	debug3("Sockets:          %u", sockets);
	debug3("Cores per socket: %u", cores);
	debug3("Threads per core: %u", threads);
#endif

	*p_block_map_size = numcpu;
	retval = _compute_block_map(*p_block_map_size, p_block_map,
				    p_block_map_inv);

	xfree(cpuinfo);		/* done with raw cpuinfo data */

	return retval;
}

extern int xcpuinfo_hwloc_topo_load(
	void *topology_in, char *topo_file, bool full)
{
	return SLURM_SUCCESS;
}

/* _chk_cpuinfo_str
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the string value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - string value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
static int _chk_cpuinfo_str(char *buffer, char *keyword, char **valptr)
{
	char *ptr;
	if (xstrncmp(buffer, keyword, strlen(keyword)))
		return false;

	ptr = strstr(buffer, ":");
	if (ptr != NULL)
		ptr++;
	*valptr = ptr;
	return true;
}

/* _chk_cpuinfo_uint32
 *	check a line of cpuinfo data (buffer) for a keyword.  If it
 *	exists, return the uint16 value for that keyword in *valptr.
 * Input:  buffer - single line of cpuinfo data
 *	   keyword - keyword to check for
 * Output: valptr - uint32 value corresponding to keyword
 *         return code - true if keyword found, false if not found
 */
static int _chk_cpuinfo_uint32(char *buffer, char *keyword, uint32_t *val)
{
	char *valptr;
	if (_chk_cpuinfo_str(buffer, keyword, &valptr)) {
		*val = strtoul(valptr, (char **)NULL, 10);
		return true;
	} else {
		return false;
	}
}

/*
 * _compute_block_map - Compute abstract->machine block mapping (and inverse)
 *   allows computation of CPU ID masks for an abstract block distribution
 *   of logical processors which can then be mapped the IDs used in the
 *   actual machine processor ID ordering (which can be BIOS/OS dependendent)
 * Input:  numproc - number of processors on the system
 *	   cpu - array of cpuinfo (file static for qsort/_compare_cpus)
 * Output: block_map, block_map_inv - asbtract->physical block distribution map
 *         return code - 0 if no error, otherwise errno
 * NOTE: User must free block_map and block_map_inv
 *
 * For example, given a system with 8 logical processors arranged as:
 *
 *	Sockets:          4
 *	Cores per socket: 2
 *	Threads per core: 1
 *
 * and a logical CPU ID assignment of:
 *
 *	Machine logical CPU ID assignment:
 *	Logical CPU ID:        0  1  2  3  4  5  6  7
 *	Physical Socket ID:    0  1  3  2  0  1  3  2
 *
 * The block_map would be:
 *
 *	Abstract -> Machine logical CPU ID block mapping:
 *	Input: (Abstract ID)   0  1  2  3  4  5  6  7
 *	Output: (Machine ID)   0  4  1  5  3  7  2  6  <--- block_map[]
 *	Physical Socket ID:    0  0  1  1  2  2  3  3
 *
 * and it's inverse would be:
 *
 *	Machine -> Abstract logical CPU ID block mapping: (inverse)
 *	Input: (Machine ID)    0  1  2  3  4  5  6  7
 *	Output: (Abstract ID)  0  2  6  4  1  3  7  5  <--- block_map_inv[]
 *	Physical Socket ID:    0  1  3  2  0  1  3  2
 */

/* physical cpu comparison with void * arguments to allow use with
 * libc qsort()
 */
static int _icmp16(uint16_t a, uint16_t b)
{
    	if (a < b) {
		return -1;
	} else if (a == b) {
		return 0;
	} else {
		return 1;
	}
}
static int _icmp32(uint32_t a, uint32_t b)
{
	if (a < b) {
		return -1;
	} else if (a == b) {
		return 0;
	} else {
		return 1;
	}
}

static int _compare_cpus(const void *a1, const void *b1) {
	uint16_t *a = (uint16_t *) a1;
	uint16_t *b = (uint16_t *) b1;
	int cmp;

	cmp = -1 * _icmp16(cpuinfo[*a].seen,cpuinfo[*b].seen); /* seen to front */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].physid, cpuinfo[*b].physid); /* key 1: physid */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].coreid, cpuinfo[*b].coreid); /* key 2: coreid */
	if (cmp != 0)
		return cmp;

	cmp = _icmp32(cpuinfo[*a].cpuid, cpuinfo[*b].cpuid);   /* key 3: cpu id */
	return cmp;
}

static int _compute_block_map(uint16_t numproc,
			      uint16_t **block_map, uint16_t **block_map_inv)
{
	uint16_t i;
	/* Compute abstract->machine block mapping (and inverse) */
	if (block_map) {
		*block_map = xmalloc(numproc * sizeof(uint16_t));
		for (i = 0; i < numproc; i++) {
			(*block_map)[i] = i;
		}
		qsort(*block_map, numproc, sizeof(uint16_t), &_compare_cpus);
	}
	if (block_map && block_map_inv) {
		*block_map_inv = xmalloc(numproc * sizeof(uint16_t));
		for (i = 0; i < numproc; i++) {
			uint16_t idx = (*block_map)[i];
			(*block_map_inv)[idx] = i;
		}
	}
#if _DEBUG
	/* Display the mapping tables */

	debug3("\nMachine logical CPU ID assignment:");
	debug3("Logical CPU ID:      ");
	for (i = 0; i < numproc; i++) {
		debug3("%3d", i);
	}
	debug3("");
	debug3("Physical Socket ID:  ");
	for (i = 0; i < numproc; i++) {
		debug3("%3u", cpuinfo[i].physid);
	}
	debug3("");

	if (block_map) {
		debug3("\nAbstract -> Machine logical CPU ID block mapping:");
		debug3("Input: (Abstract ID) ");
		for (i = 0; i < numproc; i++) {
			debug3("%3d", i);
		}
		debug3("");
		debug3("Output: (Machine ID) ");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", (*block_map)[i]);
		}
		debug3("");
		debug3("Physical Socket ID:  ");
		for (i = 0; i < numproc; i++) {
			uint16_t id = (*block_map)[i];
			debug3("%3u", cpuinfo[id].physid);
		}
		debug3("");
	}

	if (block_map_inv) {
		debug3("\nMachine -> Abstract logical CPU ID block mapping: "
		       "(inverse)");
		debug3("Input: (Machine ID)  ");
		for (i = 0; i < numproc; i++) {
			debug3("%3d", i);
		}
		debug3("");
		debug3("Output: (Abstract ID)");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", (*block_map_inv)[i]);
		}
		debug3("");
		debug3("Physical Socket ID:  ");
		for (i = 0; i < numproc; i++) {
			debug3("%3u", cpuinfo[i].physid);
		}
		debug3("");
	}
#endif
	return 0;
}
#endif

int
xcpuinfo_init(void)
{
	if ( initialized )
		return SLURM_SUCCESS;

	if (xcpuinfo_hwloc_topo_get(&procs,&boards,&sockets,&cores,&threads,
				    &block_map_size,&block_map,&block_map_inv))
		return SLURM_ERROR;

	initialized = true ;

	return SLURM_SUCCESS;
}

int
xcpuinfo_fini(void)
{
	if ( ! initialized )
		return SLURM_SUCCESS;

	initialized = false ;
	procs = sockets = cores = threads = 0;
	block_map_size = 0;
	xfree(block_map);
	xfree(block_map_inv);
#ifdef HAVE_HWLOC
	if (hwloc_xml_whole) {
		/*
		 * When a slurmd is taking over the place of the next
		 * slurmd it will have already made this file.  So don't
		 * remove it or it will remove it for the new slurmd.
		 * If this happens on the slurmstepd we don't want to remove it
		 * to begin with.
		 */
		/* (void)remove(hwloc_xml_whole); */
		xfree(hwloc_xml_whole);
	}
#endif
	return SLURM_SUCCESS;
}

/*
 * Convert an abstract core range string into a machine-specific CPU range
 * string. Abstract id to machine id conversion is done using block_map.
 * When a core is set in the input, all its threads will be set in the output.
 *
 * Inverse of xcpuinfo_mac_to_abs.
 *
 * Input:  lrange - abstract/logical core spec string.
 * Output: prange - machine/local/physical CPU spec string.
 *         return code - SLURM_SUCCESS if no error, otherwise SLURM_ERROR.
 */
int xcpuinfo_abs_to_mac(char *lrange, char **prange)
{
	static int total_cores = -1, total_cpus = -1;
	bitstr_t* absmap = NULL;
	bitstr_t* macmap = NULL;
	int icore, ithread;
	int absid, macid;
	int rc = SLURM_SUCCESS;

	if (total_cores == -1) {
		total_cores = conf->sockets * conf->cores;
		total_cpus  = conf->block_map_size;
	}

	/* allocate bitmap */
	absmap = bit_alloc(total_cores);
	macmap = bit_alloc(total_cpus);

	if (!absmap || !macmap) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* string to bitmap conversion */
	if (bit_unfmt(absmap, lrange)) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* mapping abstract id to machine id using conf->block_map */
	for (icore = 0; icore < total_cores; icore++) {
		if (bit_test(absmap, icore)) {
			for (ithread = 0; ithread < conf->threads; ithread++) {
				absid = icore * conf->threads + ithread;
				absid %= total_cpus;

				macid = conf->block_map[absid];
				macid %= total_cpus;

				bit_set(macmap, macid);
			}
		}
	}

	/* convert machine cpu bitmap to range string */
	*prange = (char*)xmalloc(total_cpus*6);
	bit_fmt(*prange, total_cpus*6, macmap);

	/* free unused bitmaps */
end_it:
	FREE_NULL_BITMAP(absmap);
	FREE_NULL_BITMAP(macmap);

	if (rc != SLURM_SUCCESS)
		error("%s: failed", __func__);

	return rc;
}

/*
 * Convert a machine-specific CPU range string into an abstract core range
 * string. Machine id to abstract id conversion is done using block_map_inv.
 * When a single thread in a core is set in the input, the corresponding core
 * will be set in its output.
 *
 * Inverse of xcpuinfo_abs_to_mac.
 *
 * Input:  in_range - machine/local/physical CPU range string.
 * Output: out_range - abstract/logical core range string. Caller should xfree()
 *         return code - SLURM_SUCCESS if no error, otherwise SLURM_ERROR.
 */
int xcpuinfo_mac_to_abs(char *in_range, char **out_range)
{
	static int total_cores = -1, total_cpus = -1;
	bitstr_t *macmap = NULL;
	bitstr_t *absmap = NULL;
	bitstr_t *absmap_core = NULL;
	int rc = SLURM_SUCCESS;

	if (total_cores == -1) {
		total_cores = conf->sockets * conf->cores;
		total_cpus  = conf->block_map_size;
	}

	/* allocate bitmaps */
	macmap = bit_alloc(total_cpus);
	absmap = bit_alloc(total_cpus);
	absmap_core = bit_alloc(total_cores);

	if (!macmap || !absmap) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* string to bitmap conversion */
	if (bit_unfmt(macmap, in_range)) {
		rc = SLURM_ERROR;
		goto end_it;
	}

	/* mapping machine id to abstract id using conf->block_map_inv */
	for (int icore = 0; icore < total_cores; icore++) {
		for (int ithread = 0; ithread < conf->threads; ithread++) {
			int absid, macid;
			macid = (icore * conf->threads) + ithread;
			macid %= total_cpus;

			/* Skip this machine CPU id if not in in_range */
			if (!bit_test(macmap, macid))
				continue;

			absid = conf->block_map_inv[macid];
			absid %= total_cpus;

			bit_set(absmap, absid);
		}
	}

	/* condense abstract CPU bitmap into an abstract core bitmap */
	for (int icore = 0; icore < total_cores; icore++) {
		for (int ithread = 0; ithread < conf->threads; ithread++) {
			int icpu = (icore * conf->threads) + ithread;
			icpu %= total_cpus;

			if (bit_test(absmap, icpu)) {
				bit_set(absmap_core, icore);
				break;
			}
		}
	}

	/* convert abstract core bitmap to range string */
	*out_range = (char*)xmalloc(total_cores * 6);
	bit_fmt(*out_range, total_cores * 6, absmap_core);

	/* free unused bitmaps */
end_it:
	FREE_NULL_BITMAP(macmap);
	FREE_NULL_BITMAP(absmap);
	FREE_NULL_BITMAP(absmap_core);

	if (rc != SLURM_SUCCESS)
		error("%s failed", __func__);

	return rc;
}

int
xcpuinfo_abs_to_map(char* lrange,uint16_t **map,uint16_t *map_size)
{
	*map_size = block_map_size;
	*map = (uint16_t*) xmalloc(block_map_size*sizeof(uint16_t));
	/* abstract range does not already include the hyperthreads */
	return _range_to_map(lrange,*map,*map_size,1);
}

/*
 * set to 1 each element of already allocated map of size
 * map_size if they are present in the input range
 * if add_thread does not equal 0, the input range is a treated
 * as a core range, and it will be mapped to an array of uint16_t
 * that will include all the hyperthreads associated to the cores.
 */
static int
_range_to_map(char* range,uint16_t *map,uint16_t map_size,int add_threads)
{
	int bad_nb=0;
	int num_fl=0;
	int con_fl=0;
	int last=0;

	char *dup;
	char *p;
	char *s=NULL;

	uint16_t start=0,end=0,i;

	/* duplicate input range */
	dup = xstrdup(range);
	p = dup;
	while ( ! last ) {
		if ( isdigit(*p) ) {
			if ( !num_fl ) {
				num_fl++;
				s=p;
			}
		}
		else if ( *p == '-' ) {
			if ( s && num_fl ) {
				*p = '\0';
				start = (uint16_t) atoi(s);
				con_fl=1;
				num_fl=0;
				s=NULL;
			}
		}
		else if ( *p == ',' || *p == '\0') {
			if ( *p == '\0' )
				last = 1;
			if ( s && num_fl ) {
				*p = '\0';
				end = (uint16_t) atoi(s);
				if ( !con_fl )
					start = end ;
				con_fl=2;
				num_fl=0;
				s=NULL;
			}
		}
		else {
			bad_nb++;
			break;
		}
		if ( con_fl == 2 ) {
			if ( add_threads ) {
				start = start * threads;
				end = (end+1)*threads - 1 ;
			}
			for( i = start ; i <= end && i < map_size ; i++) {
				map[i]=1;
			}
			con_fl=0;
		}
		p++;
	}

	xfree(dup);

	if ( bad_nb > 0 ) {
		/* bad format for input range */
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}
