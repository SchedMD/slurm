/* Wrapper for rm_api.h */

#ifndef ATTACH_BGL_H
#define ATTACH_BGL_H

/* Over-ride attach_bgl.h inclusion due to problems in compiling it 
 * with gcc and missing externals in that file */

/* MPI Debug support */

typedef struct {
  const char * host_name;        /* Something we can pass to inet_addr */
  const char * executable_name;  /* The name of the image */
  int    pid;                    /* The pid of the process */
} MPIR_PROCDESC;



#include "rm_api.h"
 
#define RM_PARTITION_ALL RM_PARTITION_FREE | RM_PARTITION_CONFIGURING | RM_PARTITION_READY | RM_PARTITION_BUSY | RM_PARTITION_DEALLOCATING | RM_PARTITION_ERROR | RM_PARTITION_NAV;

#endif
