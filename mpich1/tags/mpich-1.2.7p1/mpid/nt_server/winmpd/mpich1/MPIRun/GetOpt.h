/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef GETOPT_H
#define GETOPT_H

#ifdef WSOCK2_BEFORE_WINDOWS
#include <winsock2.h>
#endif
#include <windows.h>

bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag);
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, int *n);
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, long *n);
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, double *d);
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, LPTSTR str);

#endif
