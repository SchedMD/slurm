/* 
 * $Id$
 *
 * Copyright (C) 2001-2002 Regents of the University of California
 * See ./DISCLAIMER
 */

#ifndef _QSW_INCLUDED
#define _QSW_INCLUDED

struct qsw_libstate {
	int ls_magic;
	int ls_prognum;
	int ls_hwcontext;
};

struct qsw_jobinfo {
	int             j_magic;
	int             j_prognum;
	bitstr_t       *j_nodeset;
	int             j_nprocs;
	int             j_cyclic_alloc;
	ELAN_CAPABILITY j_cap;
};

int qsw_init(struct qsw_libstate *ls);
void qsw_fini(struct qsw_libstate *ls);
int qsw_create_jobinfo(struct qsw_jobinfo **jp, int nprocs, bitstr_t *nodeset, 
			int cyclic_alloc);
void qsw_destroy_jobinfo(struct qsw_jobinfo *jp);


#endif /* _QSW_INCLUDED */
