/*
 * This file contains only the information needed to allow a debugger to
 * locate the message-queue interface DLL.  The actual DLL defined
 * by code in mpich/src/infoexport.
 */

#include "mpiimpl.h"

#ifdef MPICH_INFODLL_LOC
char MPIR_dll_name[] = MPICH_INFODLL_LOC;
#endif

/* This is a dummy routine that is used to ensure that the MPIR_dll_name 
   is loaded */
void MPIR_Msg_queue_export( void )
{
}
