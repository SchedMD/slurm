#ifdef PROFILE
#ifdef A1
#define B 1
#elif defined(A2)
#define B 2
#else
#define B 3
#endif
#else
#ifdef A1
#define B 11
#elif defined(A2)
#define B 12
#else
#define B 13
#endif
#endif
#include <stdio.h>
main(argc,argv)
int argc;
char **argv;
{return 0;}
