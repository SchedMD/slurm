#include "clog.h"
#include "mpi.h"
#include <fcntl.h>

#define CMERGE_MAXPROCS   128
#define CMERGE_LOGBUFTYPE 777
#define MASTER_READY      801
#define SLAVE_READY       802
#define TIME_QUERY        803
#define TIME_ANSWER       804
#define CMERGE_SHIFT        1
#define CMERGE_NOSHIFT      0

void CLOG_mergelogs ( int, char *, int );
void CLOG_treesetup ( int, int, int *, int *, int * );
void CLOG_procbuf ( double * );
void CLOG_mergend ( void );
void CLOG_output ( double * );
void CLOG_cput ( double ** );
void CLOG_csync ( int, double * );
void CLOG_printdiffs ( double * );
void CLOG_reinit_buff ( void );
void clog2alog ( char * );
