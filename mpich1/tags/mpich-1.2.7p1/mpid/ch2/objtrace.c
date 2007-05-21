#include "mpiimpl.h"

#include <stdio.h>

FILE *MPIR_Ref_fp = 0;
int  MPIR_Ref_flags = 0;

void MPIR_Ref_init( 
	int flag,
	char *filename)
{
    MPIR_Ref_flags = flag;
    if (flag) {
	if (filename) {
	    MPIR_Ref_fp = fopen( filename, "w" );
	    if (!MPIR_Ref_fp) {
		MPIR_Ref_flags = 0;
	    }
	}
	else
	    MPIR_Ref_fp = stdout;
    }
}
