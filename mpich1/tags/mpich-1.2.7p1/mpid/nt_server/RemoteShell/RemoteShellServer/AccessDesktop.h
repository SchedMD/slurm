#ifndef ACCESS_DESKTOP_H
#define ACCESS_DESKTOP_H

#define RTN_TYPE   bool
#define RTN_OK	   true
#define RTN_ERROR  false

RTN_TYPE MyGrantAccessToDesktop(HANDLE hToken);

#endif
