/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */
#ifndef TRANSLATE_ERROR_H
#define TRANSLATE_ERROR_H

void Translate_Error(int error, char *msg, char *prepend=NULL);
void Translate_HRError(HRESULT hr, char *error_msg, char *prepend=NULL);
void Translate_Error(int error, WCHAR *msg, WCHAR *prepend=NULL);
void Translate_HRError(HRESULT hr, WCHAR *error_msg, WCHAR *prepend=NULL);

#endif
