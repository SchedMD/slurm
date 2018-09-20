/*****************************************************************************\
 *  test7.18.prog.c Report failures in slurm_hostlist_find().
 *  See bugs 5711 and 5746.
 *****************************************************************************
 *  Copyright (C) 2018 SchedMD LLC
 *  Written by Marshall Garey
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "slurm/slurm.h"
#include "slurm/slurm_errno.h"

hostlist_t hl = NULL;
int testid = 0;

static void _find_host(const char *hostname)
{
	printf("Find %s...%s\n", hostname,
	       slurm_hostlist_find(hl, hostname) >= 0 ? "found" : "FAILURE");
}

static void _create_hostlist(const char *hostnames)
{
	printf("\nTest %d: hostlist: %s\n", testid, hostnames);
	hl = slurm_hostlist_create(hostnames);
	testid++;
}

int main(int argc, char **argv)
{
	/* First, some basic tests. */
	_create_hostlist("node");
	_find_host("node");
	slurm_hostlist_destroy(hl);

	_create_hostlist("n1");
	_find_host("n1");
	slurm_hostlist_destroy(hl);

	_create_hostlist("node,node2");
	_find_host("node");
	_find_host("node2");
	slurm_hostlist_destroy(hl);

	_create_hostlist("n1-2,n1-3");
	_find_host("n1-2");
	_find_host("n1-3");
	slurm_hostlist_destroy(hl);

	_create_hostlist("n1.2,n1.3");
	_find_host("n1.2");
	_find_host("n1.3");
	slurm_hostlist_destroy(hl);

	/* Ranges */
	_create_hostlist("n[1-3]");
	_find_host("n1");
	_find_host("n2");
	_find_host("n3");
	slurm_hostlist_destroy(hl);

	_create_hostlist("snowflake[1-10]");
	_find_host("snowflake1");
	_find_host("snowflake2");
	_find_host("snowflake3");
	_find_host("snowflake4");
	_find_host("snowflake5");
	_find_host("snowflake6");
	_find_host("snowflake7");
	_find_host("snowflake8");
	_find_host("snowflake9");
	_find_host("snowflake10");
	slurm_hostlist_destroy(hl);

	/* Make sure leading zeros are properly handled. */
	_create_hostlist("n0000[1-3]");
	_find_host("n00001");
	_find_host("n00002");
	_find_host("n00003");
	slurm_hostlist_destroy(hl);

	_create_hostlist("n0000[11-15]");
	_find_host("n000011");
	_find_host("n000012");
	_find_host("n000013");
	_find_host("n000014");
	_find_host("n000015");
	slurm_hostlist_destroy(hl);

	_create_hostlist("nid00[446-447],nid00392");
	_find_host("nid00392");
	_find_host("nid00446");
	_find_host("nid00447");
	slurm_hostlist_destroy(hl);

	/* Other leading numbers that are partially but not completely zero. */
	_create_hostlist("nid10[446-447],nid10392");
	_find_host("nid10392");
	_find_host("nid10446");
	_find_host("nid10447");
	slurm_hostlist_destroy(hl);

	/* (Same hosts as the previous test, but different order.) */
	_create_hostlist("nid10392,nid10[446-447]");
	_find_host("nid10392");
	_find_host("nid10446");
	_find_host("nid10447");
	slurm_hostlist_destroy(hl);

	_create_hostlist("nid010[446-447],nid010392");
	_find_host("nid010392");
	_find_host("nid010446");
	_find_host("nid010447");
	slurm_hostlist_destroy(hl);

	_create_hostlist("nid00[446-447],nid00392,nid10[446-447],nid10392,snowflake[1-10]");
	_find_host("nid00392");
	_find_host("nid00446");
	_find_host("nid00447");
	_find_host("nid10392");
	_find_host("nid10446");
	_find_host("nid10447");
	_find_host("snowflake1");
	_find_host("snowflake2");
	_find_host("snowflake3");
	_find_host("snowflake4");
	_find_host("snowflake5");
	_find_host("snowflake6");
	_find_host("snowflake7");
	_find_host("snowflake8");
	_find_host("snowflake9");
	_find_host("snowflake10");
	slurm_hostlist_destroy(hl);

	_create_hostlist("nid0000[1-9],nid00[100-900],nid000[10-90],nid0[1000-9000],nid[1000-9000]");
	_find_host("nid00001");
	_find_host("nid00005");
	_find_host("nid00115");
	_find_host("nid00105");
	_find_host("nid01105");
	_find_host("nid00100");
	_find_host("nid00010");
	_find_host("nid00001");
	slurm_hostlist_destroy(hl);

	/* Multi-dimensional hosts. */
	_create_hostlist("ab[1-3]cd[6-7]");
	_find_host("ab1cd6");
	_find_host("ab1cd7");
	_find_host("ab2cd6");
	_find_host("ab2cd7");
	_find_host("ab3cd6");
	_find_host("ab3cd7");
	slurm_hostlist_destroy(hl);

	_create_hostlist("ab[1-2][1-3]");
	_find_host("ab11");
	_find_host("ab12");
	_find_host("ab13");
	_find_host("ab21");
	_find_host("ab22");
	_find_host("ab23");
	slurm_hostlist_destroy(hl);

	_create_hostlist("ab[1-2][1-3],n[2-4],c[10-11][333-334]");
	_find_host("ab11");
	_find_host("ab12");
	_find_host("ab13");
	_find_host("ab21");
	_find_host("ab22");
	_find_host("ab23");
	_find_host("n2");
	_find_host("n3");
	_find_host("n4");
	_find_host("c10333");
	_find_host("c10334");
	_find_host("c11333");
	_find_host("c11334");
	slurm_hostlist_destroy(hl);

	_create_hostlist("node1,node[2-4],node[5-6][7-8]");
	_find_host("node1");
	_find_host("node2");
	_find_host("node3");
	_find_host("node4");
	_find_host("node57");
	_find_host("node58");
	_find_host("node67");
	_find_host("node68");
	slurm_hostlist_destroy(hl);

	/* Combine multiple dimensions and zero padding */
	_create_hostlist("node000[1-2][02-03],node000[333]");
	_find_host("node000102");
	_find_host("node000103");
	_find_host("node000202");
	_find_host("node000203");
	_find_host("node000333");
	slurm_hostlist_destroy(hl);

	/* With hyphens, ranges, and multi-dimension ranges. */
	_create_hostlist("sgisummit-rcf-111-[1-15],sgiuv20-rcf-111-32,dper730xd-srcf-d16-[1-20],sgisummit-rcf-011-[1-15],dper730xd-srcf-016-[1-20],dper930-srcf-d15-05,dper7425-srcf-d15-[1-12],a-b-1-c2-[1-2][3-4]");
	_find_host("sgisummit-rcf-111-1");
	_find_host("sgiuv20-rcf-111-32");
	_find_host("dper730xd-srcf-d16-2");
	_find_host("sgisummit-rcf-011-5");
	_find_host("dper730xd-srcf-016-2");
	_find_host("dper930-srcf-d15-05");
	_find_host("dper7425-srcf-d15-1");
	_find_host("dper7425-srcf-d15-12");
	_find_host("a-b-1-c2-13");
	_find_host("a-b-1-c2-14");
	_find_host("a-b-1-c2-23");
	_find_host("a-b-1-c2-24");
	slurm_hostlist_destroy(hl);

	return 0;
}
