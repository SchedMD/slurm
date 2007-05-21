#if !defined(MPID_DEFS_H)
#define MPID_DEFS_H 1


#include "global_c_symb.h"



/*
 * This header file converts all MPI_ names into MPQ_ names, so that we avoid
 * name clashing when using the vendor's MPI library.
 *
 * Based on a C hack by Warren Smith, extended to Fortran by Olle Larsson,
 * updated and integrated in the MPICH distribution by Nick Karonis and
 * Brian Toonen.
 *
 */

#define MPICH_RENAMING_MPI_FUNCS
#define MPICH_SR_PACKED_INTRINSIC_UNSUPPORTED

#endif /* !defined(MPID_DEFS_H) */
