#ifndef LOCK_H
#define LOCK_H

#include <windows.h>

void lock(LONG *ptr);
//void unlock(LONG *ptr);
//void initlock(LONG *ptr);
#define unlock(p) * p = 0
#define initlock(p) * p = 0
bool ilock(LONG *ptr);

#define test(ptr) (* ptr)
#define wait(ptr) while (* ptr == 0) Sleep(0)
#define setevent(ptr) * ptr = 1
#define resetevent(ptr) *ptr = 0

#endif
