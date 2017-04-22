#ifndef PMIXP_DCONN_UCX_H
#define PMIXP_DCONN_UCX_H

#ifdef HAVE_UCX

#include <ucp/api/ucp.h>
void pmixp_ucx_check()
{
    unsigned major, minor, release;
    ucp_get_version(&major, &minor, &release);
    PMIXP_ERROR("UCX lib available: %d.%d.%d", major, minor, release);
}

#else 

#define pmixp_ucx_check()

#endif


#endif

