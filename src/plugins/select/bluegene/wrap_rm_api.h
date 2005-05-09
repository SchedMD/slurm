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
 
#endif
