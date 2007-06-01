#ifndef MPITEST_TEST
#define MPITEST_TEST

void Test_Init ( char *, int );
void Test_Message (char *);
void Test_Failed (char *);
void Test_Passed (char *);
int Summarize_Test_Results (void);
void Test_Finalize (void);
void Test_Waitforall (void);
#endif
