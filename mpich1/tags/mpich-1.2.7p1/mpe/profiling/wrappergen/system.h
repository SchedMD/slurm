#if !defined(__SYSTEM)
#define __SYSTEM
#include <stdio.h>

extern void   SYDefaultHandler();
extern void   SYDefaultSignals();

extern void   SYArgSqueeze();
extern int    SYArgFindName();
extern int    SYArgGetInt();
extern int    SYArgGetDouble();
extern int    SYArgHasName();
extern int    SYArgGetString();
extern int    SYArgGetIntList();
extern int    SYArgGetIntVector();

extern double SYGetCPUTime(); 
extern double SYGetElapsedTime();
extern double SYGetResidentSetSize();
extern void   SYGetPageFaults(); 

extern void   SYSetLimits();
#if defined(tc2000)
extern int    SYCheckLimits();
#else
extern void   SYCheckLimits();
#endif
extern void   SYGetNice();
extern void   SYExit();
extern void   SYexitall();
extern void   SYSetRundown();  
extern void   SYSetUserTimerRoutine();

extern void   SYGetFullPath();
extern void   SYGetRelativePath();
extern void   SYGetwd();
extern char   *SYGetRealpath();

extern FILE   *SYfopenLock();
extern void   SYfcloseLock();
extern FILE   *SYOpenWritableFile();

extern void   SYGetDayTime( ), SYGetDate();

extern void   SYIsort(), SYDsort();

extern void   SYSetResourceClockOff();
extern void   SYSetResourceClockOn();
extern void   SYGetResourceLimits(), SYSetResourceLimits();

extern void   *SYCreateRndm();
extern double SYDRndm();
extern void   SYFreeRndm();

/* Data types known by the "Safe Read/Write routines */
#define SYINT    0
#define SYDOUBLE 1
#define SYSHORT  2
#define SYFLOAT  3
#define SYCHAR   4

#endif      

