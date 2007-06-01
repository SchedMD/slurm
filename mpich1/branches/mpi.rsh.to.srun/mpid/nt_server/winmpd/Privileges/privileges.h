/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef PRIVILEGES_H
#define PRIVILEGES_H

#include <winsock2.h>
#include <windows.h>

DWORD SetAccountRights(LPTSTR User, LPTSTR Privilege);

#endif
