/* 
 * $Id$
 *
 * Copyright (C) 2001-2002 Regents of the University of California
 */

#ifndef _QSW_INCLUDED
#define _QSW_INCLUDED

/* opaque data structures - no peeking! */
typedef struct qsw_libstate 	*qsw_libstate_t;
typedef struct qsw_jobinfo 	*qsw_jobinfo_t;

#define QSW_LIBSTATE_PACK_MAX	12
#define QSW_JOBINFO_PACK_MAX	120
#define QSW_MAX_PROCS		1024

int		qsw_alloc_libstate(qsw_libstate_t *lsp);
void		qsw_free_libstate(qsw_libstate_t ls);

int		qsw_pack_libstate(qsw_libstate_t ls, void *data, int len);
int		qsw_unpack_libstate(qsw_libstate_t ls, void *data, int len);

int 		qsw_init(qsw_libstate_t restorestate);
void 		qsw_fini(qsw_libstate_t savestate);

int		qsw_alloc_jobinfo(qsw_jobinfo_t *jp);
void		qsw_free_jobinfo(qsw_jobinfo_t j);

int		qsw_pack_jobinfo(qsw_jobinfo_t j, void *data, int len);
int		qsw_unpack_jobinfo(qsw_jobinfo_t j, void *data, int len);

int 		qsw_setup_jobinfo(qsw_jobinfo_t j, int nprocs, 
			bitstr_t *nodeset, int cyclic_alloc);

int 		qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid);
void 		qsw_prog_fini(qsw_jobinfo_t jobinfo);

int 		qsw_prgdestroy(qsw_jobinfo_t jobinfo); /* was qsw_prog_reap */

int 		qsw_setcap(qsw_jobinfo_t jobinfo, int procnum); /* was qsw_attach */

int		qsw_prgsignal(qsw_jobinfo_t jobinfo, int signum); /* was qsw_signal_job */

int		qsw_getnodeid(void);
int		qsw_getnodeid_byhost(char *host);
int		qsw_gethost_bynodeid(char *host, int len, int elanid);


#endif /* _QSW_INCLUDED */
