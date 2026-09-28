/*****************************************************************************\
 *  gres_links.c - link-topology aware GRES selection for the select plugin
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "src/common/slurm_xlator.h"

#include "gres_links.h"

/*
 * Clique search results. A clique is a set of devices each directly linked to
 * every other, which is what enforce-links has to give a job. The caller sets
 * 'need'; free with _links_clique_free().
 */
typedef struct {
	bitstr_t *best_fit; /* Smallest maximal clique of size >= need */
	int best_fit_from; /* Devices best_fit draws from 'from', only
			    * meaningful when 'from' is set */
	bitstr_t *big_enough; /* Union of the maximal cliques of size >= need */
	bitstr_t *from; /* Devices a clique has to draw from to be of any use,
			 * or NULL when it may be drawn from anywhere. Set by a
			 * socket bound caller, which can only hand over the
			 * devices of its own socket */
	bitstr_t *largest; /* Largest maximal clique found */
	bitstr_t *most_from; /* Clique drawing the most on 'from', kept for a
			      * socket bound caller that no clique can supply
			      * in full */
	int most_from_cnt; /* Devices most_from draws from 'from' */
	uint64_t need; /* Clique size the caller is trying to satisfy */
	uint64_t need_from; /* Devices of 'from' the caller has to be given */
} links_clique_t;

/*
 * Both rows must agree: each is filled from that device's own Links=, so they
 * can disagree.
 * IN links_cnt - node GRES link-count matrix
 * IN a, b - device indexes
 * RET true when a and b are different devices linked to each other
 */
static bool _linked(int **links_cnt, int a, int b)
{
	return ((a != b) && (links_cnt[a][b] >= 1) && (links_cnt[b][a] >= 1));
}

/*
 * Build the adjacency bitmaps of the linked graph, once per search. The walk
 * intersects and subtracts whole neighbor sets at every level, which is one
 * bitmap operation here but a pass over two matrix rows per device otherwise.
 * IN links_cnt - node GRES link-count matrix
 * IN gres_cnt - number of devices
 * RET gres_cnt bitmaps, free with _links_adjacency_free()
 */
static bitstr_t **_links_adjacency(int **links_cnt, int gres_cnt)
{
	bitstr_t **adj = xcalloc(gres_cnt, sizeof(bitstr_t *));

	for (int v = 0; v < gres_cnt; v++) {
		adj[v] = bit_alloc(gres_cnt);
		for (int u = 0; u < gres_cnt; u++) {
			if (_linked(links_cnt, v, u))
				bit_set(adj[v], u);
		}
	}

	return adj;
}

/*
 * IN/OUT adj - adjacency bitmaps to release
 * IN gres_cnt - number of devices
 */
static void _links_adjacency_free(bitstr_t **adj, int gres_cnt)
{
	if (!adj)
		return;

	for (int v = 0; v < gres_cnt; v++)
		FREE_NULL_BITMAP(adj[v]);
	xfree(adj);
}

/*
 * Pivot for one Bron-Kerbosch step: the device of p + x with the most
 * neighbors still in p.
 * IN adj - adjacency bitmaps of the linked graph
 * IN gres_cnt - number of devices
 * IN p - candidates that can still extend the clique
 * IN x - devices already processed
 * RET pivot device index, or -1 when p and x are both empty
 */
static int _links_clique_pivot(bitstr_t **adj, int gres_cnt, bitstr_t *p,
			       bitstr_t *x)
{
	int pivot = -1, best_cnt = -1;

	for (int v = 0; v < gres_cnt; v++) {
		int neighbor_cnt;

		if (!bit_test(p, v) && !bit_test(x, v))
			continue;
		neighbor_cnt = bit_overlap(adj[v], p);
		if (neighbor_cnt > best_cnt) {
			best_cnt = neighbor_cnt;
			pivot = v;
		}
	}

	return pivot;
}

/*
 * Record one maximal clique, keeping what the search is asked to report: the
 * largest seen, the smallest that fits the request, and the union of every
 * clique large enough for it.
 * IN r - the maximal clique to record
 * IN gres_cnt - number of devices
 * IN/OUT res - results to update
 */
static void _links_clique_record(bitstr_t *r, int gres_cnt, links_clique_t *res)
{
	int r_cnt = bit_set_count(r);

	/* Defensive: the caller never starts with an empty p */
	if (!r_cnt)
		return;

	/* Sizes the node's cap, so track it whatever was asked for */
	if (!res->largest || (r_cnt > bit_set_count(res->largest))) {
		FREE_NULL_BITMAP(res->largest);
		res->largest = bit_copy(r);
	}

	/* Too small for the request, and only largest wanted it */
	if (r_cnt < (int) res->need)
		return;

	if (res->from) {
		/*
		 * A socket bound caller can only supply its own socket's
		 * devices, and the rest of the node is drawn from the same
		 * clique. Take the clique drawing least on this socket while
		 * still holding need_from, so the calls that follow keep the
		 * most to work with. One holding fewer than need_from still
		 * helps: it leaves the call short, which is how a per-socket
		 * count gives way to the link requirement.
		 */
		int from_cnt = bit_overlap(r, res->from);

		if (!from_cnt)
			return;
		if (from_cnt > res->most_from_cnt) {
			FREE_NULL_BITMAP(res->most_from);
			res->most_from = bit_copy(r);
			res->most_from_cnt = from_cnt;
		}
		if (from_cnt < (int) res->need_from)
			return;
		if (!res->best_fit || (from_cnt < res->best_fit_from) ||
		    ((from_cnt == res->best_fit_from) &&
		     (r_cnt < bit_set_count(res->best_fit)))) {
			FREE_NULL_BITMAP(res->best_fit);
			res->best_fit = bit_copy(r);
			res->best_fit_from = from_cnt;
		}
	} else if (!res->best_fit || (r_cnt < bit_set_count(res->best_fit))) {
		/* Smallest that fits, leaving larger ones for others */
		FREE_NULL_BITMAP(res->best_fit);
		res->best_fit = bit_copy(r);
	}

	/* Every device of a large enough clique is worth keeping */
	if (!res->big_enough)
		res->big_enough = bit_alloc(gres_cnt);
	bit_or(res->big_enough, r);
}

/*
 * Bron-Kerbosch with a pivot. Only candidates the pivot is not linked to need
 * branching on, which keeps a fully linked node at n steps instead of 2^n.
 * IN adj - adjacency bitmaps of the linked graph
 * IN gres_cnt - number of devices
 * IN/OUT r - devices in the clique so far
 * IN/OUT p - candidates that can still extend the clique
 * IN/OUT x - devices already processed, to avoid duplicate cliques
 * IN/OUT res - accumulated results
 */
static void _links_clique_walk(bitstr_t **adj, int gres_cnt, bitstr_t *r,
			       bitstr_t *p, bitstr_t *x, links_clique_t *res)
{
	int pivot;
	bitstr_t *branch;

	/* Nothing left to add, and nothing already reported it: r is maximal */
	if ((bit_ffs(p) == -1) && (bit_ffs(x) == -1)) {
		_links_clique_record(r, gres_cnt, res);
		return;
	}

	/*
	 * Branch only on the candidates the pivot is not linked to, and settle
	 * that set before the loop below starts emptying p.
	 */
	pivot = _links_clique_pivot(adj, gres_cnt, p, x);
	branch = bit_copy(p);
	if (pivot != -1)
		bit_and_not(branch, adj[pivot]);

	for (int v = 0; v < gres_cnt; v++) {
		bitstr_t *p_next, *x_next;

		if (!bit_test(branch, v))
			continue;

		/* Both sets narrow to v's neighbors, keeping r all-to-all */
		p_next = bit_copy(p);
		bit_and(p_next, adj[v]);
		x_next = bit_copy(x);
		bit_and(x_next, adj[v]);

		/* Report every maximal clique holding r and v */
		bit_set(r, v);
		_links_clique_walk(adj, gres_cnt, r, p_next, x_next, res);
		bit_clear(r, v);

		/* Move v from the candidate set to the excluded set */
		bit_clear(p, v);
		bit_set(x, v);

		FREE_NULL_BITMAP(p_next);
		FREE_NULL_BITMAP(x_next);
	}

	FREE_NULL_BITMAP(branch);
}

/*
 * Enumerate the maximal cliques among the given candidates.
 * IN gres_ns - node GRES state providing the links_cnt matrix
 * IN avail - candidate devices
 * IN/OUT res - res->need is set by the caller, the bitmaps are filled in and
 *              must be released with _links_clique_free()
 */
static void _links_clique_find(gres_node_state_t *gres_ns, bitstr_t *avail,
			       links_clique_t *res)
{
	int gres_cnt = bit_size(avail);
	/* using r, p, and x is standard notation for Bron-Kerbosch algorithm */
	bitstr_t *r = NULL, *p = NULL, *x = NULL, **adj = NULL;

	if (bit_ffs(avail) == -1)
		return;

	/* Without a complete link matrix nothing can be shown to be linked */
	if (!gres_ns->links_cnt || (gres_ns->link_len != gres_cnt))
		return;

	adj = _links_adjacency(gres_ns->links_cnt, gres_cnt);
	r = bit_alloc(gres_cnt);
	p = bit_copy(avail);
	x = bit_alloc(gres_cnt);

	_links_clique_walk(adj, gres_cnt, r, p, x, res);

	_links_adjacency_free(adj, gres_cnt);
	FREE_NULL_BITMAP(r);
	FREE_NULL_BITMAP(p);
	FREE_NULL_BITMAP(x);
}

/* IN/OUT res - results to release */
static void _links_clique_free(links_clique_t *res)
{
	FREE_NULL_BITMAP(res->best_fit);
	FREE_NULL_BITMAP(res->big_enough);
	FREE_NULL_BITMAP(res->largest);
	FREE_NULL_BITMAP(res->most_from);
}

/*
 * Devices available to the job here: the socket-independent ones plus those on
 * usable sockets.
 * IN sock_gres - per-socket GRES availability for this node
 * IN gres_ns - node GRES state, to exclude devices held by other jobs
 * RET available devices (caller must free), or NULL when this GRES has no
 *     per-device topology
 */
static bitstr_t *_build_avail_dev_bitmap(sock_gres_t *sock_gres,
					 gres_node_state_t *gres_ns)
{
	bitstr_t *avail = NULL;

	if (sock_gres->bits_any_sock)
		avail = bit_copy(sock_gres->bits_any_sock);

	if (sock_gres->bits_by_sock) {
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			if (!sock_gres->bits_by_sock[s] ||
			    (sock_gres->cnt_by_sock &&
			     !sock_gres->cnt_by_sock[s]))
				continue;
			if (avail)
				bit_or(avail, sock_gres->bits_by_sock[s]);
			else
				avail = bit_copy(sock_gres->bits_by_sock[s]);
		}
	}

	/* Only consider free devices unless testing against all GRES */
	if (avail && !sock_gres->use_total_gres && gres_ns->gres_bit_alloc)
		bit_and_not(avail, gres_ns->gres_bit_alloc);

	return avail;
}

/*
 * IN devices - devices to count, or NULL
 * IN usable - devices that survived the link filtering
 * RET count present in both
 */
static uint64_t _usable_dev_cnt(bitstr_t *devices, bitstr_t *usable)
{
	if (!devices || !usable)
		return 0;

	return bit_overlap(devices, usable);
}

/*
 * Cap this node's GRES counts to linked_cap. Only the node-wide counts are
 * capped: cnt_by_sock[] is per-socket availability, and
 * gres_filter_sock_core() reads a zero there as a socket with no usable
 * devices and strips its cores. Selection enforces the linked set exactly
 * through the clique search, so the cap need not be expressed per socket.
 *
 * That leaves total_cnt below the sum of the per-socket counts, so the cap is
 * kept in links_cap for gres_filter_sock_core() to recompute total_cnt from
 * when it drops a socket, rather than subtracting the socket's count from it.
 * IN/OUT sock_gres - GRES availability counts, capped in place
 * IN linked_cap - size of the largest mutually linked set
 * IN/OUT near_gres_cnt - GRES near allocated CPUs, capped to match
 */
static void _cap_to_linked(sock_gres_t *sock_gres, uint64_t linked_cap,
			   uint64_t *near_gres_cnt)
{
	sock_gres->links_cap = linked_cap;
	sock_gres->cnt_any_sock = MIN(sock_gres->cnt_any_sock, linked_cap);
	sock_gres->total_cnt = MIN(sock_gres->total_cnt, linked_cap);
	*near_gres_cnt = MIN(*near_gres_cnt, sock_gres->total_cnt);
}

/*
 * Drop the devices that cannot be part of a min_gres set of pairwise linked
 * devices, so the caller's min_gres test rejects a node that cannot form one.
 * Only the counts are adjusted; selection re-derives the set from the bitmaps.
 *
 * The counts are capped to the largest linked set whatever min_gres is. A
 * per-job request leaves min_gres at 1, but node selection adds these counts
 * up to decide how many nodes the job needs, and only one linked set of this
 * node can actually be handed over.
 * IN/OUT sock_gres - GRES availability counts, shrunk in place
 * IN gres_ns - node GRES state providing the links_cnt matrix
 * IN min_gres - devices the job needs on this node
 * IN/OUT near_gres_cnt - GRES on sockets with available CPUs, capped to match
 */
extern void gres_links_filter(sock_gres_t *sock_gres,
			      gres_node_state_t *gres_ns, uint64_t min_gres,
			      uint64_t *near_gres_cnt)
{
	links_clique_t clique = { .need = MAX(min_gres, 1) };
	bitstr_t *avail_dev = NULL;
	uint64_t linked_cap;

	/* An earlier call's pick is stale here, so drop it before any return */
	FREE_NULL_BITMAP(sock_gres->links_pick);

	avail_dev = _build_avail_dev_bitmap(sock_gres, gres_ns);
	if (!avail_dev) {
		if (gres_ns->topo_cnt) {
			/*
			 * The node does have a per-device topology, so the
			 * missing bitmaps are not a property of the GRES.
			 * gres_sock_list_create() leaves them out for a node
			 * that has not registered since slurmctld started,
			 * deliberately, so that such a node is not rejected as
			 * never runnable. Leave the counts alone so that this
			 * option does not reject it either. The node is
			 * filtered again, against its devices, once it
			 * registers.
			 */
			return;
		}
		/*
		 * Configured by count alone, so no topology and no links.
		 * Every device is a linked set of one, the same answer as for
		 * devices with File= but no Links= between them.
		 */
		_cap_to_linked(sock_gres, 1, near_gres_cnt);
		return;
	}

	_links_clique_find(gres_ns, avail_dev, &clique);

	/* No usable link matrix leaves no clique, but a lone device is a set */
	if (clique.largest)
		linked_cap = bit_set_count(clique.largest);
	else
		linked_cap = 1;

	if (!clique.big_enough) {
		/* No large enough set, so the cap leaves the node too few */
		_cap_to_linked(sock_gres, linked_cap, near_gres_cnt);
		goto fini;
	}

	/*
	 * Hand the clique to selection so it need not walk again. Only worth
	 * keeping when sized against a real per-node count.
	 */
	if (min_gres > 1)
		SWAP(sock_gres->links_pick, clique.best_fit);

	/*
	 * Shrink the counts to big_enough so node selection sees the real
	 * linked capacity. The bitmaps are shared with the rest of cons_tres
	 * and selection re-runs the search anyway, so only counts are touched,
	 * as enforce-binding does.
	 */
	if (sock_gres->cnt_any_sock && sock_gres->bits_any_sock)
		sock_gres->cnt_any_sock =
			MIN(sock_gres->cnt_any_sock,
			    _usable_dev_cnt(sock_gres->bits_any_sock,
					    clique.big_enough));
	if (sock_gres->cnt_by_sock && sock_gres->bits_by_sock) {
		for (int s = 0; s < sock_gres->sock_cnt; s++) {
			if (!sock_gres->bits_by_sock[s])
				continue;
			sock_gres->cnt_by_sock[s] =
				MIN(sock_gres->cnt_by_sock[s],
				    _usable_dev_cnt(sock_gres->bits_by_sock[s],
						    clique.big_enough));
		}
	}

	/* Only one linked set of this node can be handed to the job */
	_cap_to_linked(sock_gres, linked_cap, near_gres_cnt);

fini:
	FREE_NULL_BITMAP(avail_dev);
	_links_clique_free(&clique);
}

/*
 * Smallest linked set large enough for the request, leaving larger ones for
 * other jobs; the largest set when none is large enough.
 *
 * With from set, only a set holding that socket's devices is of use, so the
 * smallest one holding need_from of them wins. Handing such a caller the
 * largest set regardless would commit it to whichever of its devices that set
 * held, which need not be one the rest of the node can link to.
 * IN gres_ns - node GRES state providing the links_cnt matrix
 * IN avail - candidate devices
 * IN need - devices the caller still needs
 * IN from - devices the set must draw from, or NULL for all of avail
 * IN need_from - devices of from the caller needs, ignored when from is NULL
 * RET the chosen clique (caller must free), or NULL when avail is empty or no
 *     set draws from it
 */
static bitstr_t *_best_fit_clique(gres_node_state_t *gres_ns, bitstr_t *avail,
				  uint64_t need, bitstr_t *from,
				  uint64_t need_from)
{
	links_clique_t clique = { .from = from,
				  .need = need,
				  .need_from = need_from };
	bitstr_t *picked = NULL;

	_links_clique_find(gres_ns, avail, &clique);

	if (clique.best_fit)
		SWAP(picked, clique.best_fit);
	else if (clique.most_from)
		SWAP(picked, clique.most_from);
	else if (clique.largest && !from)
		SWAP(picked, clique.largest);

	_links_clique_free(&clique);

	return picked;
}

/*
 * The loops split their request by socket and can call in for one device at a
 * time, so a single call's gres_needed is not the size of the set to find.
 * links_node_gres carries the node's whole allocation; fall back to what this
 * call can see when it is not known.
 * IN sock_gres - GRES availability for this node
 * IN selected - devices already selected on this node
 * IN gres_needed - number of devices this call is to select
 * RET count of devices the job is to hold on this node
 */
static uint64_t _node_target(sock_gres_t *sock_gres, bitstr_t *selected,
			     uint64_t gres_needed)
{
	return MAX(sock_gres->links_node_gres,
		   bit_set_count(selected) + gres_needed);
}

/*
 * A lone device has nothing to link to, so a node allocating one is picked the
 * normal way. This is decided against the node's whole allocation: per call,
 * a request split into single devices would make its first pick blind to the
 * links and bind every later pick to whatever that was.
 * IN sock_gres - GRES availability for this node
 * IN node_inx - node index
 * IN gres_needed - number of devices this call is to select
 * RET true when the devices must come from a linked set
 */
extern bool gres_links_required(sock_gres_t *sock_gres, int node_inx,
				uint64_t gres_needed)
{
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;

	if (!gres_needed)
		return false;

	return _node_target(sock_gres, gres_js->gres_bit_select[node_inx],
			    gres_needed) > 1;
}

/*
 * Select up to gres_needed devices, each linked to every device already
 * selected on this node and to each other. Candidates come from the whole
 * node, and the set is sized for the node's whole allocation, so a request
 * split across sockets cannot commit to a device the rest cannot link to.
 * Picking fewer leaves the caller short and fails the node.
 *
 * sock_bits is set by callers whose socket is a requirement, not a split of
 * their own making. Only the devices handed over are held to it, so a set
 * reaching into another socket can still be used. It has to be: the calls
 * that follow draw their devices from the same set.
 * IN sock_gres - GRES availability for this node
 * IN node_inx - node index
 * IN gres_needed - number of devices this call is to select
 * IN sock_bits - devices the caller must pick from, or NULL for the whole node
 * RET count of devices selected
 */
extern uint64_t gres_links_select(sock_gres_t *sock_gres, int node_inx,
				  uint64_t gres_needed, bitstr_t *sock_bits)
{
	gres_job_state_t *gres_js = sock_gres->gres_state_job->gres_data;
	gres_node_state_t *gres_ns = sock_gres->gres_state_node->gres_data;
	bitstr_t *selected = gres_js->gres_bit_select[node_inx];
	int gres_cnt = bit_size(selected);
	uint64_t picked = 0, clique_needed;
	bitstr_t *avail = NULL, *clique = NULL;

	/* Size for the whole node; only the remainder is found here */
	clique_needed = _node_target(sock_gres, selected, gres_needed) -
			bit_set_count(selected);

	/* Without a complete link matrix nothing can be shown to be linked */
	if (!gres_ns->links_cnt || (gres_ns->link_len != gres_cnt))
		return 0;

	/*
	 * Feasibility considers the whole node, so do the same here.
	 * Restricting to this call's socket could commit to a device whose
	 * partners are elsewhere, under-allocating a node feasibility accepted.
	 */
	avail = _build_avail_dev_bitmap(sock_gres, gres_ns);
	if (!avail)
		return 0;
	bit_and_not(avail, selected);

	/* Drop candidates not linked to what is already selected */
	for (int sel = 0; sel < gres_cnt; sel++) {
		if (!bit_test(selected, sel))
			continue;
		for (int g = 0; g < gres_cnt; g++) {
			if (bit_test(avail, g) &&
			    !_linked(gres_ns->links_cnt, sel, g))
				bit_clear(avail, g);
		}
	}

	/*
	 * Feasibility already ran this search and stored the result in
	 * links_pick, so reuse it when it is large enough; its members that are
	 * still free and linked to what is selected survive the intersection
	 * with avail, since a subset of a clique is a clique. Fall back to a
	 * fresh search when it is too small. The cache is no use to a socket
	 * bound caller: it was chosen with no socket in mind, so it may hold
	 * too few of that socket's devices even while large enough for the
	 * node, and only a fresh search holds the set to what the socket must
	 * supply.
	 */
	if (sock_gres->links_pick && !sock_bits) {
		xassert(bit_size(sock_gres->links_pick) == gres_cnt);
		clique = bit_copy(sock_gres->links_pick);
		bit_and(clique, avail);
		if (bit_set_count(clique) < clique_needed)
			FREE_NULL_BITMAP(clique);
	}
	if (!clique)
		clique = _best_fit_clique(gres_ns, avail, clique_needed,
					  sock_bits, gres_needed);
	if (clique && sock_bits)
		bit_and(clique, sock_bits);

	for (int g = 0; clique && (g < gres_cnt) && (picked < gres_needed);
	     g++) {
		if (!bit_test(clique, g))
			continue;
		bit_set(selected, g);
		gres_js->gres_cnt_node_select[node_inx]++;
		picked++;
	}

	FREE_NULL_BITMAP(avail);
	FREE_NULL_BITMAP(clique);

	return picked;
}
