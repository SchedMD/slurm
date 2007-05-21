/*
 * There are several routines that are called by the sample ADI routines that 
 * provide additional debugging support and which are not needed for the
 * ADI itself.  This file provides stubs for all of the routines that
 * the ADI may reference from the upper level code.
 */

void MPIR_PointerOpts( flag, filename )
int flag;
char *filename;
{
}

int MPIR_debug_state = 0;
char * MPIR_debug_abort_string= 0;

void MPIR_Breakpoint()
{
}
