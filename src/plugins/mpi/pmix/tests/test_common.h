/*
 * Copyright (c) 2013-2015 Intel, Inc.  All rights reserved.
 * Copyright (c) 2015      Artem Y. Polyakov <artpol84@gmail.com>.
 *                         All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 *
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <stdio.h>
#include <string.h>

#define TEST_NAMESPACE "smoky_nspace"
#define TEST_CREDENTIAL "dummy"

/* WARNING: pmix_test_output_prepare is currently not threadsafe!
 * fix it once needed!
 */
char *pmix_test_output_prepare(const char *fmt,...);
extern int pmix_test_verbose;

#define STRIPPED_FILE_NAME (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define TEST_OUTPUT(x) { \
    fprintf(stderr,"%s:%s: %s\n",STRIPPED_FILE_NAME, __func__, \
            pmix_test_output_prepare x); \
    fflush(stderr); \
}

#define TEST_ERROR(x) { \
    fprintf(stderr,"ERROR [%s:%d:%s]: %s\n", STRIPPED_FILE_NAME, __LINE__, __func__, \
            pmix_test_output_prepare x); \
    fflush(stderr); \
}

#define TEST_VERBOSE_ON() (pmix_test_verbose = 1)

#define TEST_VERBOSE(x) { \
    if (pmix_test_verbose) { \
        TEST_OUTPUT(x); \
    } \
}


#endif // TEST_COMMON_H
