/* $Id: petsccfg.h,v 1.1.1.1 1997/09/17 20:38:46 gropp Exp $ */
/* Configuration file for PETSc */
/* The generic version is empty, except for indicating that X is not supported
   (some intel systems have X11, but many do not) */
#if (defined(intelnx) && !defined(paragon)) 
#if !defined(TOOLSNOX11)
#define TOOLSNOX11
#endif
#endif

#if defined(cray)
/* cc on Crays does not accept valid C programs.  The valid identifier 
   "restrict" is a reserved word for the Cray.  Heaven only knows what else 
    is...
   The Cray can be told to accept C, but only at the cost of various
   optimzations.  Rather than do that, we redefine the legal identifier
   "restrict" to something else.
 */
#define restrict RestrictForCray
#endif

#if defined(__MSDOS__)
/* Many people (including the Author!) don't have Fortran on their PC's.
   Rather than use f2c versions, we simply turn off the Fortran versions
 */   
#define TOOLS_NOFORTRAN
#define TOOLSNOX11
#endif
