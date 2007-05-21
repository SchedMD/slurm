/* Header for testing procedures */

#ifndef _INCLUDED_TEST_H_
#define _INCLUDED_TEST_H_

#if defined(NEEDS_STDLIB_PROTOTYPES)
#include "protofix.h"
#endif

void Test_Init (char *, int);
void Test_Message (char *);
void Test_Failed (char *);
void Test_Passed (char *);
int Summarize_Test_Results (void);
void Test_Finalize (void);
void Test_Waitforall (void);

#endif
