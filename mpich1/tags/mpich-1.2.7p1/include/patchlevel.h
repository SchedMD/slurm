#define PATCHLEVEL 1.2
#define PATCHLEVEL_MAJOR 1
#define PATCHLEVEL_MINOR 2
#define PATCHLEVEL_SUBMINOR 7
#define PATCHLEVEL_STRING "1.2.7p1"

#define PATCHLEVEL_RELEASE_KIND "release first patches"
#ifndef PATCHLEVEL_RELEASE_DATE 
#ifdef RELEASE_DATE
#define PATCHLEVEL_RELEASE_DATE RELEASE_DATE
#else
#define PATCHLEVEL_RELEASE_DATE "$Date: 2005/11/04 11:54:51$"
#endif
#endif

/* Patches applied is a string of the applied patch numbers */
#define PATCHES_APPLIED "\
"
/* This is a comma separated list.  The last element, if present, must
   have a common (e.g., 2345, 2535,).  Used in src/env/initutil.c */
#define PATCHES_APPLIED_LIST 
