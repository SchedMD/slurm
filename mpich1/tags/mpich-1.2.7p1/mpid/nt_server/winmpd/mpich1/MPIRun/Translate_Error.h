/**********************************************************************
/* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
/* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
/* THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
/* PARTICULAR PURPOSE.
/*
/* Copyright (C) 1998  Brigham Young University.  All Rights Reserved.
/*
/* file: Translate_Error.h
/*
/**********************************************************************/
#ifdef WSOCK2_BEFORE_WINDOWS
#include <winsock2.h>
#endif
#include <windows.h>
void Translate_Error(int error, char *msg, char *prepend=NULL);
void Translate_HRError(HRESULT hr, char *error_msg, char *prepend=NULL);
void Translate_Error(int error, WCHAR *msg, WCHAR *prepend=NULL);
void Translate_HRError(HRESULT hr, WCHAR *error_msg, WCHAR *prepend=NULL);
