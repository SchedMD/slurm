#if !defined(MPID_FORT_H)
#define MPID_FORT_H



/*
 * This header file converts all MPI_ names into MPQ_ names, so that we avoid
 * name clashing when using the vendor's MPI library.
 *
 * Based on a C hack by Warren Smith, extended to Fortran by Olle Larsson,
 * updated and integrated in the MPICH distribution by Nick Karonis and
 * Brian Toonen.
 *
 * This file is used only when you specify -f77sed during MPICH configuration,
 * and under those circumstances, fortran90 will not work.  This file is
 * intended to be used as a fallback position ... it is NOT our first
 * choice.
 *
 */


#if defined(F77_NAME_UPPER)
#   define MPI_ISEND MPQ_ISEND
#   define PMPI_ISEND PMPQ_ISEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_isend__ mpq_isend__
#   define pmpi_isend__ pmpq_isend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_isend mpq_isend
#   define pmpi_isend pmpq_isend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_isend_ mpq_isend_
#   endif
#   define pmpi_isend_ pmpq_isend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_IRECV MPQ_IRECV
#   define PMPI_IRECV PMPQ_IRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_irecv__ mpq_irecv__
#   define pmpi_irecv__ pmpq_irecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_irecv mpq_irecv
#   define pmpi_irecv pmpq_irecv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_irecv_ mpq_irecv_
#   endif
#   define pmpi_irecv_ pmpq_irecv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WAIT MPQ_WAIT
#   define PMPI_WAIT PMPQ_WAIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_wait__ mpq_wait__
#   define pmpi_wait__ pmpq_wait__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_wait mpq_wait
#   define pmpi_wait pmpq_wait
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_wait_ mpq_wait_
#   endif
#   define pmpi_wait_ pmpq_wait_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TEST MPQ_TEST
#   define PMPI_TEST PMPQ_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_test__ mpq_test__
#   define pmpi_test__ pmpq_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_test mpq_test
#   define pmpi_test pmpq_test
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_test_ mpq_test_
#   endif
#   define pmpi_test_ pmpq_test_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ADDRESS MPQ_ADDRESS
#   define PMPI_ADDRESS PMPQ_ADDRESS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_address__ mpq_address__
#   define pmpi_address__ pmpq_address__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_address mpq_address
#   define pmpi_address pmpq_address
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_address_ mpq_address_
#   endif
#   define pmpi_address_ pmpq_address_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CANCEL MPQ_CANCEL
#   define PMPI_CANCEL PMPQ_CANCEL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cancel__ mpq_cancel__
#   define pmpi_cancel__ pmpq_cancel__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cancel mpq_cancel
#   define pmpi_cancel pmpq_cancel
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cancel_ mpq_cancel_
#   endif
#   define pmpi_cancel_ pmpq_cancel_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_REQUEST_FREE MPQ_REQUEST_FREE
#   define PMPI_REQUEST_FREE PMPQ_REQUEST_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_request_free__ mpq_request_free__
#   define pmpi_request_free__ pmpq_request_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_request_free mpq_request_free
#   define pmpi_request_free pmpq_request_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_request_free_ mpq_request_free_
#   endif
#   define pmpi_request_free_ pmpq_request_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_PROBE MPQ_PROBE
#   define PMPI_PROBE PMPQ_PROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_probe__ mpq_probe__
#   define pmpi_probe__ pmpq_probe__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_probe mpq_probe
#   define pmpi_probe pmpq_probe
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_probe_ mpq_probe_
#   endif
#   define pmpi_probe_ pmpq_probe_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_START MPQ_START
#   define PMPI_START PMPQ_START
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_start__ mpq_start__
#   define pmpi_start__ pmpq_start__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_start mpq_start
#   define pmpi_start pmpq_start
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_start_ mpq_start_
#   endif
#   define pmpi_start_ pmpq_start_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TESTANY MPQ_TESTANY
#   define PMPI_TESTANY PMPQ_TESTANY
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_testany__ mpq_testany__
#   define pmpi_testany__ pmpq_testany__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_testany mpq_testany
#   define pmpi_testany pmpq_testany
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_testany_ mpq_testany_
#   endif
#   define pmpi_testany_ pmpq_testany_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WAITALL MPQ_WAITALL
#   define PMPI_WAITALL PMPQ_WAITALL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_waitall__ mpq_waitall__
#   define pmpi_waitall__ pmpq_waitall__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_waitall mpq_waitall
#   define pmpi_waitall pmpq_waitall
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_waitall_ mpq_waitall_
#   endif
#   define pmpi_waitall_ pmpq_waitall_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SEND MPQ_SEND
#   define PMPI_SEND PMPQ_SEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_send__ mpq_send__
#   define pmpi_send__ pmpq_send__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_send mpq_send
#   define pmpi_send pmpq_send
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_send_ mpq_send_
#   endif
#   define pmpi_send_ pmpq_send_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_RECV MPQ_RECV
#   define PMPI_RECV PMPQ_RECV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_recv__ mpq_recv__
#   define pmpi_recv__ pmpq_recv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_recv mpq_recv
#   define pmpi_recv pmpq_recv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_recv_ mpq_recv_
#   endif
#   define pmpi_recv_ pmpq_recv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SENDRECV MPQ_SENDRECV
#   define PMPI_SENDRECV PMPQ_SENDRECV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_sendrecv__ mpq_sendrecv__
#   define pmpi_sendrecv__ pmpq_sendrecv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_sendrecv mpq_sendrecv
#   define pmpi_sendrecv pmpq_sendrecv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_sendrecv_ mpq_sendrecv_
#   endif
#   define pmpi_sendrecv_ pmpq_sendrecv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_IPROBE MPQ_IPROBE
#   define PMPI_IPROBE PMPQ_IPROBE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_iprobe__ mpq_iprobe__
#   define pmpi_iprobe__ pmpq_iprobe__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_iprobe mpq_iprobe
#   define pmpi_iprobe pmpq_iprobe
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_iprobe_ mpq_iprobe_
#   endif
#   define pmpi_iprobe_ pmpq_iprobe_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TESTALL MPQ_TESTALL
#   define PMPI_TESTALL PMPQ_TESTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_testall__ mpq_testall__
#   define pmpi_testall__ pmpq_testall__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_testall mpq_testall
#   define pmpi_testall pmpq_testall
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_testall_ mpq_testall_
#   endif
#   define pmpi_testall_ pmpq_testall_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WAITANY MPQ_WAITANY
#   define PMPI_WAITANY PMPQ_WAITANY
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_waitany__ mpq_waitany__
#   define pmpi_waitany__ pmpq_waitany__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_waitany mpq_waitany
#   define pmpi_waitany pmpq_waitany
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_waitany_ mpq_waitany_
#   endif
#   define pmpi_waitany_ pmpq_waitany_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_RECV_INIT MPQ_RECV_INIT
#   define PMPI_RECV_INIT PMPQ_RECV_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_recv_init__ mpq_recv_init__
#   define pmpi_recv_init__ pmpq_recv_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_recv_init mpq_recv_init
#   define pmpi_recv_init pmpq_recv_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_recv_init_ mpq_recv_init_
#   endif
#   define pmpi_recv_init_ pmpq_recv_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SEND_INIT MPQ_SEND_INIT
#   define PMPI_SEND_INIT PMPQ_SEND_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_send_init__ mpq_send_init__
#   define pmpi_send_init__ pmpq_send_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_send_init mpq_send_init
#   define pmpi_send_init pmpq_send_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_send_init_ mpq_send_init_
#   endif
#   define pmpi_send_init_ pmpq_send_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SENDRECV_REPLACE MPQ_SENDRECV_REPLACE
#   define PMPI_SENDRECV_REPLACE PMPQ_SENDRECV_REPLACE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_sendrecv_replace__ mpq_sendrecv_replace__
#   define pmpi_sendrecv_replace__ pmpq_sendrecv_replace__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_sendrecv_replace mpq_sendrecv_replace
#   define pmpi_sendrecv_replace pmpq_sendrecv_replace
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_sendrecv_replace_ mpq_sendrecv_replace_
#   endif
#   define pmpi_sendrecv_replace_ pmpq_sendrecv_replace_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GET_COUNT MPQ_GET_COUNT
#   define PMPI_GET_COUNT PMPQ_GET_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_get_count__ mpq_get_count__
#   define pmpi_get_count__ pmpq_get_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_get_count mpq_get_count
#   define pmpi_get_count pmpq_get_count
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_get_count_ mpq_get_count_
#   endif
#   define pmpi_get_count_ pmpq_get_count_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BSEND MPQ_BSEND
#   define PMPI_BSEND PMPQ_BSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_bsend__ mpq_bsend__
#   define pmpi_bsend__ pmpq_bsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_bsend mpq_bsend
#   define pmpi_bsend pmpq_bsend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_bsend_ mpq_bsend_
#   endif
#   define pmpi_bsend_ pmpq_bsend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SSEND MPQ_SSEND
#   define PMPI_SSEND PMPQ_SSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_ssend__ mpq_ssend__
#   define pmpi_ssend__ pmpq_ssend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_ssend mpq_ssend
#   define pmpi_ssend pmpq_ssend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_ssend_ mpq_ssend_
#   endif
#   define pmpi_ssend_ pmpq_ssend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_RSEND MPQ_RSEND
#   define PMPI_RSEND PMPQ_RSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_rsend__ mpq_rsend__
#   define pmpi_rsend__ pmpq_rsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_rsend mpq_rsend
#   define pmpi_rsend pmpq_rsend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_rsend_ mpq_rsend_
#   endif
#   define pmpi_rsend_ pmpq_rsend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BUFFER_ATTACH MPQ_BUFFER_ATTACH
#   define PMPI_BUFFER_ATTACH PMPQ_BUFFER_ATTACH
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_buffer_attach__ mpq_buffer_attach__
#   define pmpi_buffer_attach__ pmpq_buffer_attach__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_buffer_attach mpq_buffer_attach
#   define pmpi_buffer_attach pmpq_buffer_attach
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_buffer_attach_ mpq_buffer_attach_
#   endif
#   define pmpi_buffer_attach_ pmpq_buffer_attach_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BUFFER_DETACH MPQ_BUFFER_DETACH
#   define PMPI_BUFFER_DETACH PMPQ_BUFFER_DETACH
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_buffer_detach__ mpq_buffer_detach__
#   define pmpi_buffer_detach__ pmpq_buffer_detach__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_buffer_detach mpq_buffer_detach
#   define pmpi_buffer_detach pmpq_buffer_detach
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_buffer_detach_ mpq_buffer_detach_
#   endif
#   define pmpi_buffer_detach_ pmpq_buffer_detach_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_IBSEND MPQ_IBSEND
#   define PMPI_IBSEND PMPQ_IBSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_ibsend__ mpq_ibsend__
#   define pmpi_ibsend__ pmpq_ibsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_ibsend mpq_ibsend
#   define pmpi_ibsend pmpq_ibsend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_ibsend_ mpq_ibsend_
#   endif
#   define pmpi_ibsend_ pmpq_ibsend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ISSEND MPQ_ISSEND
#   define PMPI_ISSEND PMPQ_ISSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_issend__ mpq_issend__
#   define pmpi_issend__ pmpq_issend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_issend mpq_issend
#   define pmpi_issend pmpq_issend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_issend_ mpq_issend_
#   endif
#   define pmpi_issend_ pmpq_issend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_IRSEND MPQ_IRSEND
#   define PMPI_IRSEND PMPQ_IRSEND
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_irsend__ mpq_irsend__
#   define pmpi_irsend__ pmpq_irsend__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_irsend mpq_irsend
#   define pmpi_irsend pmpq_irsend
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_irsend_ mpq_irsend_
#   endif
#   define pmpi_irsend_ pmpq_irsend_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WAITSOME MPQ_WAITSOME
#   define PMPI_WAITSOME PMPQ_WAITSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_waitsome__ mpq_waitsome__
#   define pmpi_waitsome__ pmpq_waitsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_waitsome mpq_waitsome
#   define pmpi_waitsome pmpq_waitsome
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_waitsome_ mpq_waitsome_
#   endif
#   define pmpi_waitsome_ pmpq_waitsome_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TESTSOME MPQ_TESTSOME
#   define PMPI_TESTSOME PMPQ_TESTSOME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_testsome__ mpq_testsome__
#   define pmpi_testsome__ pmpq_testsome__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_testsome mpq_testsome
#   define pmpi_testsome pmpq_testsome
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_testsome_ mpq_testsome_
#   endif
#   define pmpi_testsome_ pmpq_testsome_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TEST_CANCELLED MPQ_TEST_CANCELLED
#   define PMPI_TEST_CANCELLED PMPQ_TEST_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_test_cancelled__ mpq_test_cancelled__
#   define pmpi_test_cancelled__ pmpq_test_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_test_cancelled mpq_test_cancelled
#   define pmpi_test_cancelled pmpq_test_cancelled
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_test_cancelled_ mpq_test_cancelled_
#   endif
#   define pmpi_test_cancelled_ pmpq_test_cancelled_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BSEND_INIT MPQ_BSEND_INIT
#   define PMPI_BSEND_INIT PMPQ_BSEND_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_bsend_init__ mpq_bsend_init__
#   define pmpi_bsend_init__ pmpq_bsend_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_bsend_init mpq_bsend_init
#   define pmpi_bsend_init pmpq_bsend_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_bsend_init_ mpq_bsend_init_
#   endif
#   define pmpi_bsend_init_ pmpq_bsend_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_RSEND_INIT MPQ_RSEND_INIT
#   define PMPI_RSEND_INIT PMPQ_RSEND_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_rsend_init__ mpq_rsend_init__
#   define pmpi_rsend_init__ pmpq_rsend_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_rsend_init mpq_rsend_init
#   define pmpi_rsend_init pmpq_rsend_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_rsend_init_ mpq_rsend_init_
#   endif
#   define pmpi_rsend_init_ pmpq_rsend_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SSEND_INIT MPQ_SSEND_INIT
#   define PMPI_SSEND_INIT PMPQ_SSEND_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_ssend_init__ mpq_ssend_init__
#   define pmpi_ssend_init__ pmpq_ssend_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_ssend_init mpq_ssend_init
#   define pmpi_ssend_init pmpq_ssend_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_ssend_init_ mpq_ssend_init_
#   endif
#   define pmpi_ssend_init_ pmpq_ssend_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_STARTALL MPQ_STARTALL
#   define PMPI_STARTALL PMPQ_STARTALL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_startall__ mpq_startall__
#   define pmpi_startall__ pmpq_startall__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_startall mpq_startall
#   define pmpi_startall pmpq_startall
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_startall_ mpq_startall_
#   endif
#   define pmpi_startall_ pmpq_startall_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_COMMIT MPQ_TYPE_COMMIT
#   define PMPI_TYPE_COMMIT PMPQ_TYPE_COMMIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_commit__ mpq_type_commit__
#   define pmpi_type_commit__ pmpq_type_commit__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_commit mpq_type_commit
#   define pmpi_type_commit pmpq_type_commit
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_commit_ mpq_type_commit_
#   endif
#   define pmpi_type_commit_ pmpq_type_commit_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_CONTIGUOUS MPQ_TYPE_CONTIGUOUS
#   define PMPI_TYPE_CONTIGUOUS PMPQ_TYPE_CONTIGUOUS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_contiguous__ mpq_type_contiguous__
#   define pmpi_type_contiguous__ pmpq_type_contiguous__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_contiguous mpq_type_contiguous
#   define pmpi_type_contiguous pmpq_type_contiguous
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_contiguous_ mpq_type_contiguous_
#   endif
#   define pmpi_type_contiguous_ pmpq_type_contiguous_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_EXTENT MPQ_TYPE_EXTENT
#   define PMPI_TYPE_EXTENT PMPQ_TYPE_EXTENT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_extent__ mpq_type_extent__
#   define pmpi_type_extent__ pmpq_type_extent__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_extent mpq_type_extent
#   define pmpi_type_extent pmpq_type_extent
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_extent_ mpq_type_extent_
#   endif
#   define pmpi_type_extent_ pmpq_type_extent_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_FREE MPQ_TYPE_FREE
#   define PMPI_TYPE_FREE PMPQ_TYPE_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_free__ mpq_type_free__
#   define pmpi_type_free__ pmpq_type_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_free mpq_type_free
#   define pmpi_type_free pmpq_type_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_free_ mpq_type_free_
#   endif
#   define pmpi_type_free_ pmpq_type_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_HINDEXED MPQ_TYPE_HINDEXED
#   define PMPI_TYPE_HINDEXED PMPQ_TYPE_HINDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_hindexed__ mpq_type_hindexed__
#   define pmpi_type_hindexed__ pmpq_type_hindexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_hindexed mpq_type_hindexed
#   define pmpi_type_hindexed pmpq_type_hindexed
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_hindexed_ mpq_type_hindexed_
#   endif
#   define pmpi_type_hindexed_ pmpq_type_hindexed_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_HVECTOR MPQ_TYPE_HVECTOR
#   define PMPI_TYPE_HVECTOR PMPQ_TYPE_HVECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_hvector__ mpq_type_hvector__
#   define pmpi_type_hvector__ pmpq_type_hvector__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_hvector mpq_type_hvector
#   define pmpi_type_hvector pmpq_type_hvector
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_hvector_ mpq_type_hvector_
#   endif
#   define pmpi_type_hvector_ pmpq_type_hvector_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_INDEXED MPQ_TYPE_INDEXED
#   define PMPI_TYPE_INDEXED PMPQ_TYPE_INDEXED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_indexed__ mpq_type_indexed__
#   define pmpi_type_indexed__ pmpq_type_indexed__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_indexed mpq_type_indexed
#   define pmpi_type_indexed pmpq_type_indexed
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_indexed_ mpq_type_indexed_
#   endif
#   define pmpi_type_indexed_ pmpq_type_indexed_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_LB MPQ_TYPE_LB
#   define PMPI_TYPE_LB PMPQ_TYPE_LB
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_lb__ mpq_type_lb__
#   define pmpi_type_lb__ pmpq_type_lb__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_lb mpq_type_lb
#   define pmpi_type_lb pmpq_type_lb
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_lb_ mpq_type_lb_
#   endif
#   define pmpi_type_lb_ pmpq_type_lb_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_SIZE MPQ_TYPE_SIZE
#   define PMPI_TYPE_SIZE PMPQ_TYPE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_size__ mpq_type_size__
#   define pmpi_type_size__ pmpq_type_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_size mpq_type_size
#   define pmpi_type_size pmpq_type_size
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_size_ mpq_type_size_
#   endif
#   define pmpi_type_size_ pmpq_type_size_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_STRUCT MPQ_TYPE_STRUCT
#   define PMPI_TYPE_STRUCT PMPQ_TYPE_STRUCT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_struct__ mpq_type_struct__
#   define pmpi_type_struct__ pmpq_type_struct__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_struct mpq_type_struct
#   define pmpi_type_struct pmpq_type_struct
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_struct_ mpq_type_struct_
#   endif
#   define pmpi_type_struct_ pmpq_type_struct_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_UB MPQ_TYPE_UB
#   define PMPI_TYPE_UB PMPQ_TYPE_UB
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_ub__ mpq_type_ub__
#   define pmpi_type_ub__ pmpq_type_ub__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_ub mpq_type_ub
#   define pmpi_type_ub pmpq_type_ub
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_ub_ mpq_type_ub_
#   endif
#   define pmpi_type_ub_ pmpq_type_ub_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_VECTOR MPQ_TYPE_VECTOR
#   define PMPI_TYPE_VECTOR PMPQ_TYPE_VECTOR
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_vector__ mpq_type_vector__
#   define pmpi_type_vector__ pmpq_type_vector__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_vector mpq_type_vector
#   define pmpi_type_vector pmpq_type_vector
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_vector_ mpq_type_vector_
#   endif
#   define pmpi_type_vector_ pmpq_type_vector_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GET_ELEMENTS MPQ_GET_ELEMENTS
#   define PMPI_GET_ELEMENTS PMPQ_GET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_get_elements__ mpq_get_elements__
#   define pmpi_get_elements__ pmpq_get_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_get_elements mpq_get_elements
#   define pmpi_get_elements pmpq_get_elements
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_get_elements_ mpq_get_elements_
#   endif
#   define pmpi_get_elements_ pmpq_get_elements_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_PACK_SIZE MPQ_PACK_SIZE
#   define PMPI_PACK_SIZE PMPQ_PACK_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_pack_size__ mpq_pack_size__
#   define pmpi_pack_size__ pmpq_pack_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_pack_size mpq_pack_size
#   define pmpi_pack_size pmpq_pack_size
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_pack_size_ mpq_pack_size_
#   endif
#   define pmpi_pack_size_ pmpq_pack_size_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_PACK MPQ_PACK
#   define PMPI_PACK PMPQ_PACK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_pack__ mpq_pack__
#   define pmpi_pack__ pmpq_pack__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_pack mpq_pack
#   define pmpi_pack pmpq_pack
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_pack_ mpq_pack_
#   endif
#   define pmpi_pack_ pmpq_pack_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_UNPACK MPQ_UNPACK
#   define PMPI_UNPACK PMPQ_UNPACK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_unpack__ mpq_unpack__
#   define pmpi_unpack__ pmpq_unpack__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_unpack mpq_unpack
#   define pmpi_unpack pmpq_unpack
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_unpack_ mpq_unpack_
#   endif
#   define pmpi_unpack_ pmpq_unpack_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INITIALIZED MPQ_INITIALIZED
#   define PMPI_INITIALIZED PMPQ_INITIALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_initialized__ mpq_initialized__
#   define pmpi_initialized__ pmpq_initialized__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_initialized mpq_initialized
#   define pmpi_initialized pmpq_initialized
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_initialized_ mpq_initialized_
#   endif
#   define pmpi_initialized_ pmpq_initialized_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ABORT MPQ_ABORT
#   define PMPI_ABORT PMPQ_ABORT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_abort__ mpq_abort__
#   define pmpi_abort__ pmpq_abort__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_abort mpq_abort
#   define pmpi_abort pmpq_abort
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_abort_ mpq_abort_
#   endif
#   define pmpi_abort_ pmpq_abort_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INIT MPQ_INIT
#   define PMPI_INIT PMPQ_INIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_init__ mpq_init__
#   define pmpi_init__ pmpq_init__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_init mpq_init
#   define pmpi_init pmpq_init
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_init_ mpq_init_
#   endif
#   define pmpi_init_ pmpq_init_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_FINALIZE MPQ_FINALIZE
#   define PMPI_FINALIZE PMPQ_FINALIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_finalize__ mpq_finalize__
#   define pmpi_finalize__ pmpq_finalize__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_finalize mpq_finalize
#   define pmpi_finalize pmpq_finalize
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_finalize_ mpq_finalize_
#   endif
#   define pmpi_finalize_ pmpq_finalize_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERROR_STRING MPQ_ERROR_STRING
#   define PMPI_ERROR_STRING PMPQ_ERROR_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_error_string__ mpq_error_string__
#   define pmpi_error_string__ pmpq_error_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_error_string mpq_error_string
#   define pmpi_error_string pmpq_error_string
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_error_string_ mpq_error_string_
#   endif
#   define pmpi_error_string_ pmpq_error_string_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GET_PROCESSOR_NAME MPQ_GET_PROCESSOR_NAME
#   define PMPI_GET_PROCESSOR_NAME PMPQ_GET_PROCESSOR_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_get_processor_name__ mpq_get_processor_name__
#   define pmpi_get_processor_name__ pmpq_get_processor_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_get_processor_name mpq_get_processor_name
#   define pmpi_get_processor_name pmpq_get_processor_name
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_get_processor_name_ mpq_get_processor_name_
#   endif
#   define pmpi_get_processor_name_ pmpq_get_processor_name_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERRHANDLER_CREATE MPQ_ERRHANDLER_CREATE
#   define PMPI_ERRHANDLER_CREATE PMPQ_ERRHANDLER_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_errhandler_create__ mpq_errhandler_create__
#   define pmpi_errhandler_create__ pmpq_errhandler_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_errhandler_create mpq_errhandler_create
#   define pmpi_errhandler_create pmpq_errhandler_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_errhandler_create_ mpq_errhandler_create_
#   endif
#   define pmpi_errhandler_create_ pmpq_errhandler_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERRHANDLER_SET MPQ_ERRHANDLER_SET
#   define PMPI_ERRHANDLER_SET PMPQ_ERRHANDLER_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_errhandler_set__ mpq_errhandler_set__
#   define pmpi_errhandler_set__ pmpq_errhandler_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_errhandler_set mpq_errhandler_set
#   define pmpi_errhandler_set pmpq_errhandler_set
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_errhandler_set_ mpq_errhandler_set_
#   endif
#   define pmpi_errhandler_set_ pmpq_errhandler_set_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERRHANDLER_GET MPQ_ERRHANDLER_GET
#   define PMPI_ERRHANDLER_GET PMPQ_ERRHANDLER_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_errhandler_get__ mpq_errhandler_get__
#   define pmpi_errhandler_get__ pmpq_errhandler_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_errhandler_get mpq_errhandler_get
#   define pmpi_errhandler_get pmpq_errhandler_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_errhandler_get_ mpq_errhandler_get_
#   endif
#   define pmpi_errhandler_get_ pmpq_errhandler_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERRHANDLER_FREE MPQ_ERRHANDLER_FREE
#   define PMPI_ERRHANDLER_FREE PMPQ_ERRHANDLER_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_errhandler_free__ mpq_errhandler_free__
#   define pmpi_errhandler_free__ pmpq_errhandler_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_errhandler_free mpq_errhandler_free
#   define pmpi_errhandler_free pmpq_errhandler_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_errhandler_free_ mpq_errhandler_free_
#   endif
#   define pmpi_errhandler_free_ pmpq_errhandler_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ERROR_CLASS MPQ_ERROR_CLASS
#   define PMPI_ERROR_CLASS PMPQ_ERROR_CLASS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_error_class__ mpq_error_class__
#   define pmpi_error_class__ pmpq_error_class__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_error_class mpq_error_class
#   define pmpi_error_class pmpq_error_class
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_error_class_ mpq_error_class_
#   endif
#   define pmpi_error_class_ pmpq_error_class_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WTIME MPQ_WTIME
#   define PMPI_WTIME PMPQ_WTIME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_wtime__ mpq_wtime__
#   define pmpi_wtime__ pmpq_wtime__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_wtime mpq_wtime
#   define pmpi_wtime pmpq_wtime
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_wtime_ mpq_wtime_
#   endif
#   define pmpi_wtime_ pmpq_wtime_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_WTICK MPQ_WTICK
#   define PMPI_WTICK PMPQ_WTICK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_wtick__ mpq_wtick__
#   define pmpi_wtick__ pmpq_wtick__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_wtick mpq_wtick
#   define pmpi_wtick pmpq_wtick
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_wtick_ mpq_wtick_
#   endif
#   define pmpi_wtick_ pmpq_wtick_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GET_VERSION MPQ_GET_VERSION
#   define PMPI_GET_VERSION PMPQ_GET_VERSION
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_get_version__ mpq_get_version__
#   define pmpi_get_version__ pmpq_get_version__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_get_version mpq_get_version
#   define pmpi_get_version pmpq_get_version
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_get_version_ mpq_get_version_
#   endif
#   define pmpi_get_version_ pmpq_get_version_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_KEYVAL_FREE MPQ_KEYVAL_FREE
#   define PMPI_KEYVAL_FREE PMPQ_KEYVAL_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_keyval_free__ mpq_keyval_free__
#   define pmpi_keyval_free__ pmpq_keyval_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_keyval_free mpq_keyval_free
#   define pmpi_keyval_free pmpq_keyval_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_keyval_free_ mpq_keyval_free_
#   endif
#   define pmpi_keyval_free_ pmpq_keyval_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_KEYVAL_CREATE MPQ_KEYVAL_CREATE
#   define PMPI_KEYVAL_CREATE PMPQ_KEYVAL_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_keyval_create__ mpq_keyval_create__
#   define pmpi_keyval_create__ pmpq_keyval_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_keyval_create mpq_keyval_create
#   define pmpi_keyval_create pmpq_keyval_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_keyval_create_ mpq_keyval_create_
#   endif
#   define pmpi_keyval_create_ pmpq_keyval_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ATTR_GET MPQ_ATTR_GET
#   define PMPI_ATTR_GET PMPQ_ATTR_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_attr_get__ mpq_attr_get__
#   define pmpi_attr_get__ pmpq_attr_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_attr_get mpq_attr_get
#   define pmpi_attr_get pmpq_attr_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_attr_get_ mpq_attr_get_
#   endif
#   define pmpi_attr_get_ pmpq_attr_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ATTR_DELETE MPQ_ATTR_DELETE
#   define PMPI_ATTR_DELETE PMPQ_ATTR_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_attr_delete__ mpq_attr_delete__
#   define pmpi_attr_delete__ pmpq_attr_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_attr_delete mpq_attr_delete
#   define pmpi_attr_delete pmpq_attr_delete
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_attr_delete_ mpq_attr_delete_
#   endif
#   define pmpi_attr_delete_ pmpq_attr_delete_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ATTR_PUT MPQ_ATTR_PUT
#   define PMPI_ATTR_PUT PMPQ_ATTR_PUT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_attr_put__ mpq_attr_put__
#   define pmpi_attr_put__ pmpq_attr_put__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_attr_put mpq_attr_put
#   define pmpi_attr_put pmpq_attr_put
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_attr_put_ mpq_attr_put_
#   endif
#   define pmpi_attr_put_ pmpq_attr_put_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_EXCL MPQ_GROUP_EXCL
#   define PMPI_GROUP_EXCL PMPQ_GROUP_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_excl__ mpq_group_excl__
#   define pmpi_group_excl__ pmpq_group_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_excl mpq_group_excl
#   define pmpi_group_excl pmpq_group_excl
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_excl_ mpq_group_excl_
#   endif
#   define pmpi_group_excl_ pmpq_group_excl_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_DIFFERENCE MPQ_GROUP_DIFFERENCE
#   define PMPI_GROUP_DIFFERENCE PMPQ_GROUP_DIFFERENCE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_difference__ mpq_group_difference__
#   define pmpi_group_difference__ pmpq_group_difference__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_difference mpq_group_difference
#   define pmpi_group_difference pmpq_group_difference
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_difference_ mpq_group_difference_
#   endif
#   define pmpi_group_difference_ pmpq_group_difference_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_FREE MPQ_GROUP_FREE
#   define PMPI_GROUP_FREE PMPQ_GROUP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_free__ mpq_group_free__
#   define pmpi_group_free__ pmpq_group_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_free mpq_group_free
#   define pmpi_group_free pmpq_group_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_free_ mpq_group_free_
#   endif
#   define pmpi_group_free_ pmpq_group_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_INCL MPQ_GROUP_INCL
#   define PMPI_GROUP_INCL PMPQ_GROUP_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_incl__ mpq_group_incl__
#   define pmpi_group_incl__ pmpq_group_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_incl mpq_group_incl
#   define pmpi_group_incl pmpq_group_incl
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_incl_ mpq_group_incl_
#   endif
#   define pmpi_group_incl_ pmpq_group_incl_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_INTERSECTION MPQ_GROUP_INTERSECTION
#   define PMPI_GROUP_INTERSECTION PMPQ_GROUP_INTERSECTION
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_intersection__ mpq_group_intersection__
#   define pmpi_group_intersection__ pmpq_group_intersection__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_intersection mpq_group_intersection
#   define pmpi_group_intersection pmpq_group_intersection
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_intersection_ mpq_group_intersection_
#   endif
#   define pmpi_group_intersection_ pmpq_group_intersection_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_RANK MPQ_GROUP_RANK
#   define PMPI_GROUP_RANK PMPQ_GROUP_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_rank__ mpq_group_rank__
#   define pmpi_group_rank__ pmpq_group_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_rank mpq_group_rank
#   define pmpi_group_rank pmpq_group_rank
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_rank_ mpq_group_rank_
#   endif
#   define pmpi_group_rank_ pmpq_group_rank_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_SIZE MPQ_GROUP_SIZE
#   define PMPI_GROUP_SIZE PMPQ_GROUP_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_size__ mpq_group_size__
#   define pmpi_group_size__ pmpq_group_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_size mpq_group_size
#   define pmpi_group_size pmpq_group_size
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_size_ mpq_group_size_
#   endif
#   define pmpi_group_size_ pmpq_group_size_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_UNION MPQ_GROUP_UNION
#   define PMPI_GROUP_UNION PMPQ_GROUP_UNION
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_union__ mpq_group_union__
#   define pmpi_group_union__ pmpq_group_union__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_union mpq_group_union
#   define pmpi_group_union pmpq_group_union
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_union_ mpq_group_union_
#   endif
#   define pmpi_group_union_ pmpq_group_union_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_COMPARE MPQ_GROUP_COMPARE
#   define PMPI_GROUP_COMPARE PMPQ_GROUP_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_compare__ mpq_group_compare__
#   define pmpi_group_compare__ pmpq_group_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_compare mpq_group_compare
#   define pmpi_group_compare pmpq_group_compare
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_compare_ mpq_group_compare_
#   endif
#   define pmpi_group_compare_ pmpq_group_compare_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_DUP MPQ_COMM_DUP
#   define PMPI_COMM_DUP PMPQ_COMM_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_dup__ mpq_comm_dup__
#   define pmpi_comm_dup__ pmpq_comm_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_dup mpq_comm_dup
#   define pmpi_comm_dup pmpq_comm_dup
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_dup_ mpq_comm_dup_
#   endif
#   define pmpi_comm_dup_ pmpq_comm_dup_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_FREE MPQ_COMM_FREE
#   define PMPI_COMM_FREE PMPQ_COMM_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_free__ mpq_comm_free__
#   define pmpi_comm_free__ pmpq_comm_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_free mpq_comm_free
#   define pmpi_comm_free pmpq_comm_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_free_ mpq_comm_free_
#   endif
#   define pmpi_comm_free_ pmpq_comm_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_GROUP MPQ_COMM_GROUP
#   define PMPI_COMM_GROUP PMPQ_COMM_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_group__ mpq_comm_group__
#   define pmpi_comm_group__ pmpq_comm_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_group mpq_comm_group
#   define pmpi_comm_group pmpq_comm_group
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_group_ mpq_comm_group_
#   endif
#   define pmpi_comm_group_ pmpq_comm_group_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_CREATE MPQ_COMM_CREATE
#   define PMPI_COMM_CREATE PMPQ_COMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_create__ mpq_comm_create__
#   define pmpi_comm_create__ pmpq_comm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_create mpq_comm_create
#   define pmpi_comm_create pmpq_comm_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_create_ mpq_comm_create_
#   endif
#   define pmpi_comm_create_ pmpq_comm_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_SET_NAME MPQ_COMM_SET_NAME
#   define PMPI_COMM_SET_NAME PMPQ_COMM_SET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_set_name__ mpq_comm_set_name__
#   define pmpi_comm_set_name__ pmpq_comm_set_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_set_name mpq_comm_set_name
#   define pmpi_comm_set_name pmpq_comm_set_name
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_set_name_ mpq_comm_set_name_
#   endif
#   define pmpi_comm_set_name_ pmpq_comm_set_name_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_GET_NAME MPQ_COMM_GET_NAME
#   define PMPI_COMM_GET_NAME PMPQ_COMM_GET_NAME
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_get_name__ mpq_comm_get_name__
#   define pmpi_comm_get_name__ pmpq_comm_get_name__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_get_name mpq_comm_get_name
#   define pmpi_comm_get_name pmpq_comm_get_name
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_get_name_ mpq_comm_get_name_
#   endif
#   define pmpi_comm_get_name_ pmpq_comm_get_name_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_SIZE MPQ_COMM_SIZE
#   define PMPI_COMM_SIZE PMPQ_COMM_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_size__ mpq_comm_size__
#   define pmpi_comm_size__ pmpq_comm_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_size mpq_comm_size
#   define pmpi_comm_size pmpq_comm_size
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_size_ mpq_comm_size_
#   endif
#   define pmpi_comm_size_ pmpq_comm_size_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_SPLIT MPQ_COMM_SPLIT
#   define PMPI_COMM_SPLIT PMPQ_COMM_SPLIT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_split__ mpq_comm_split__
#   define pmpi_comm_split__ pmpq_comm_split__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_split mpq_comm_split
#   define pmpi_comm_split pmpq_comm_split
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_split_ mpq_comm_split_
#   endif
#   define pmpi_comm_split_ pmpq_comm_split_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_TRANSLATE_RANKS MPQ_GROUP_TRANSLATE_RANKS
#   define PMPI_GROUP_TRANSLATE_RANKS PMPQ_GROUP_TRANSLATE_RANKS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_translate_ranks__ mpq_group_translate_ranks__
#   define pmpi_group_translate_ranks__ pmpq_group_translate_ranks__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_translate_ranks mpq_group_translate_ranks
#   define pmpi_group_translate_ranks pmpq_group_translate_ranks
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_translate_ranks_ mpq_group_translate_ranks_
#   endif
#   define pmpi_group_translate_ranks_ pmpq_group_translate_ranks_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_TEST_INTER MPQ_COMM_TEST_INTER
#   define PMPI_COMM_TEST_INTER PMPQ_COMM_TEST_INTER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_test_inter__ mpq_comm_test_inter__
#   define pmpi_comm_test_inter__ pmpq_comm_test_inter__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_test_inter mpq_comm_test_inter
#   define pmpi_comm_test_inter pmpq_comm_test_inter
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_test_inter_ mpq_comm_test_inter_
#   endif
#   define pmpi_comm_test_inter_ pmpq_comm_test_inter_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_RANK MPQ_COMM_RANK
#   define PMPI_COMM_RANK PMPQ_COMM_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_rank__ mpq_comm_rank__
#   define pmpi_comm_rank__ pmpq_comm_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_rank mpq_comm_rank
#   define pmpi_comm_rank pmpq_comm_rank
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_rank_ mpq_comm_rank_
#   endif
#   define pmpi_comm_rank_ pmpq_comm_rank_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_COMPARE MPQ_COMM_COMPARE
#   define PMPI_COMM_COMPARE PMPQ_COMM_COMPARE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_compare__ mpq_comm_compare__
#   define pmpi_comm_compare__ pmpq_comm_compare__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_compare mpq_comm_compare
#   define pmpi_comm_compare pmpq_comm_compare
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_compare_ mpq_comm_compare_
#   endif
#   define pmpi_comm_compare_ pmpq_comm_compare_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_REMOTE_SIZE MPQ_COMM_REMOTE_SIZE
#   define PMPI_COMM_REMOTE_SIZE PMPQ_COMM_REMOTE_SIZE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_remote_size__ mpq_comm_remote_size__
#   define pmpi_comm_remote_size__ pmpq_comm_remote_size__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_remote_size mpq_comm_remote_size
#   define pmpi_comm_remote_size pmpq_comm_remote_size
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_remote_size_ mpq_comm_remote_size_
#   endif
#   define pmpi_comm_remote_size_ pmpq_comm_remote_size_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_COMM_REMOTE_GROUP MPQ_COMM_REMOTE_GROUP
#   define PMPI_COMM_REMOTE_GROUP PMPQ_COMM_REMOTE_GROUP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_comm_remote_group__ mpq_comm_remote_group__
#   define pmpi_comm_remote_group__ pmpq_comm_remote_group__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_comm_remote_group mpq_comm_remote_group
#   define pmpi_comm_remote_group pmpq_comm_remote_group
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_comm_remote_group_ mpq_comm_remote_group_
#   endif
#   define pmpi_comm_remote_group_ pmpq_comm_remote_group_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INTERCOMM_CREATE MPQ_INTERCOMM_CREATE
#   define PMPI_INTERCOMM_CREATE PMPQ_INTERCOMM_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_intercomm_create__ mpq_intercomm_create__
#   define pmpi_intercomm_create__ pmpq_intercomm_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_intercomm_create mpq_intercomm_create
#   define pmpi_intercomm_create pmpq_intercomm_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_intercomm_create_ mpq_intercomm_create_
#   endif
#   define pmpi_intercomm_create_ pmpq_intercomm_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INTERCOMM_MERGE MPQ_INTERCOMM_MERGE
#   define PMPI_INTERCOMM_MERGE PMPQ_INTERCOMM_MERGE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_intercomm_merge__ mpq_intercomm_merge__
#   define pmpi_intercomm_merge__ pmpq_intercomm_merge__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_intercomm_merge mpq_intercomm_merge
#   define pmpi_intercomm_merge pmpq_intercomm_merge
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_intercomm_merge_ mpq_intercomm_merge_
#   endif
#   define pmpi_intercomm_merge_ pmpq_intercomm_merge_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_NULL_COPY_FN MPQ_NULL_COPY_FN
#   define PMPI_NULL_COPY_FN PMPQ_NULL_COPY_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_null_copy_fn__ mpq_null_copy_fn__
#   define pmpi_null_copy_fn__ pmpq_null_copy_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_null_copy_fn mpq_null_copy_fn
#   define pmpi_null_copy_fn pmpq_null_copy_fn
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_null_copy_fn_ mpq_null_copy_fn_
#   endif
#   define pmpi_null_copy_fn_ pmpq_null_copy_fn_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_NULL_DELETE_FN MPQ_NULL_DELETE_FN
#   define PMPI_NULL_DELETE_FN PMPQ_NULL_DELETE_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_null_delete_fn__ mpq_null_delete_fn__
#   define pmpi_null_delete_fn__ pmpq_null_delete_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_null_delete_fn mpq_null_delete_fn
#   define pmpi_null_delete_fn pmpq_null_delete_fn
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_null_delete_fn_ mpq_null_delete_fn_
#   endif
#   define pmpi_null_delete_fn_ pmpq_null_delete_fn_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_DUP_FN MPQ_DUP_FN
#   define PMPI_DUP_FN PMPQ_DUP_FN
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_dup_fn__ mpq_dup_fn__
#   define pmpi_dup_fn__ pmpq_dup_fn__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_dup_fn mpq_dup_fn
#   define pmpi_dup_fn pmpq_dup_fn
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_dup_fn_ mpq_dup_fn_
#   endif
#   define pmpi_dup_fn_ pmpq_dup_fn_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BARRIER MPQ_BARRIER
#   define PMPI_BARRIER PMPQ_BARRIER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_barrier__ mpq_barrier__
#   define pmpi_barrier__ pmpq_barrier__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_barrier mpq_barrier
#   define pmpi_barrier pmpq_barrier
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_barrier_ mpq_barrier_
#   endif
#   define pmpi_barrier_ pmpq_barrier_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_BCAST MPQ_BCAST
#   define PMPI_BCAST PMPQ_BCAST
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_bcast__ mpq_bcast__
#   define pmpi_bcast__ pmpq_bcast__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_bcast mpq_bcast
#   define pmpi_bcast pmpq_bcast
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_bcast_ mpq_bcast_
#   endif
#   define pmpi_bcast_ pmpq_bcast_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GATHER MPQ_GATHER
#   define PMPI_GATHER PMPQ_GATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_gather__ mpq_gather__
#   define pmpi_gather__ pmpq_gather__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_gather mpq_gather
#   define pmpi_gather pmpq_gather
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_gather_ mpq_gather_
#   endif
#   define pmpi_gather_ pmpq_gather_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GATHERV MPQ_GATHERV
#   define PMPI_GATHERV PMPQ_GATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_gatherv__ mpq_gatherv__
#   define pmpi_gatherv__ pmpq_gatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_gatherv mpq_gatherv
#   define pmpi_gatherv pmpq_gatherv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_gatherv_ mpq_gatherv_
#   endif
#   define pmpi_gatherv_ pmpq_gatherv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SCATTER MPQ_SCATTER
#   define PMPI_SCATTER PMPQ_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_scatter__ mpq_scatter__
#   define pmpi_scatter__ pmpq_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_scatter mpq_scatter
#   define pmpi_scatter pmpq_scatter
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_scatter_ mpq_scatter_
#   endif
#   define pmpi_scatter_ pmpq_scatter_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SCATTERV MPQ_SCATTERV
#   define PMPI_SCATTERV PMPQ_SCATTERV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_scatterv__ mpq_scatterv__
#   define pmpi_scatterv__ pmpq_scatterv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_scatterv mpq_scatterv
#   define pmpi_scatterv pmpq_scatterv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_scatterv_ mpq_scatterv_
#   endif
#   define pmpi_scatterv_ pmpq_scatterv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ALLGATHER MPQ_ALLGATHER
#   define PMPI_ALLGATHER PMPQ_ALLGATHER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_allgather__ mpq_allgather__
#   define pmpi_allgather__ pmpq_allgather__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_allgather mpq_allgather
#   define pmpi_allgather pmpq_allgather
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_allgather_ mpq_allgather_
#   endif
#   define pmpi_allgather_ pmpq_allgather_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ALLGATHERV MPQ_ALLGATHERV
#   define PMPI_ALLGATHERV PMPQ_ALLGATHERV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_allgatherv__ mpq_allgatherv__
#   define pmpi_allgatherv__ pmpq_allgatherv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_allgatherv mpq_allgatherv
#   define pmpi_allgatherv pmpq_allgatherv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_allgatherv_ mpq_allgatherv_
#   endif
#   define pmpi_allgatherv_ pmpq_allgatherv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ALLTOALL MPQ_ALLTOALL
#   define PMPI_ALLTOALL PMPQ_ALLTOALL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_alltoall__ mpq_alltoall__
#   define pmpi_alltoall__ pmpq_alltoall__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_alltoall mpq_alltoall
#   define pmpi_alltoall pmpq_alltoall
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_alltoall_ mpq_alltoall_
#   endif
#   define pmpi_alltoall_ pmpq_alltoall_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ALLTOALLV MPQ_ALLTOALLV
#   define PMPI_ALLTOALLV PMPQ_ALLTOALLV
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_alltoallv__ mpq_alltoallv__
#   define pmpi_alltoallv__ pmpq_alltoallv__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_alltoallv mpq_alltoallv
#   define pmpi_alltoallv pmpq_alltoallv
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_alltoallv_ mpq_alltoallv_
#   endif
#   define pmpi_alltoallv_ pmpq_alltoallv_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_REDUCE MPQ_REDUCE
#   define PMPI_REDUCE PMPQ_REDUCE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_reduce__ mpq_reduce__
#   define pmpi_reduce__ pmpq_reduce__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_reduce mpq_reduce
#   define pmpi_reduce pmpq_reduce
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_reduce_ mpq_reduce_
#   endif
#   define pmpi_reduce_ pmpq_reduce_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_ALLREDUCE MPQ_ALLREDUCE
#   define PMPI_ALLREDUCE PMPQ_ALLREDUCE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_allreduce__ mpq_allreduce__
#   define pmpi_allreduce__ pmpq_allreduce__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_allreduce mpq_allreduce
#   define pmpi_allreduce pmpq_allreduce
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_allreduce_ mpq_allreduce_
#   endif
#   define pmpi_allreduce_ pmpq_allreduce_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_REDUCE_SCATTER MPQ_REDUCE_SCATTER
#   define PMPI_REDUCE_SCATTER PMPQ_REDUCE_SCATTER
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_reduce_scatter__ mpq_reduce_scatter__
#   define pmpi_reduce_scatter__ pmpq_reduce_scatter__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_reduce_scatter mpq_reduce_scatter
#   define pmpi_reduce_scatter pmpq_reduce_scatter
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_reduce_scatter_ mpq_reduce_scatter_
#   endif
#   define pmpi_reduce_scatter_ pmpq_reduce_scatter_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_SCAN MPQ_SCAN
#   define PMPI_SCAN PMPQ_SCAN
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_scan__ mpq_scan__
#   define pmpi_scan__ pmpq_scan__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_scan mpq_scan
#   define pmpi_scan pmpq_scan
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_scan_ mpq_scan_
#   endif
#   define pmpi_scan_ pmpq_scan_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_OP_CREATE MPQ_OP_CREATE
#   define PMPI_OP_CREATE PMPQ_OP_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_op_create__ mpq_op_create__
#   define pmpi_op_create__ pmpq_op_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_op_create mpq_op_create
#   define pmpi_op_create pmpq_op_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_op_create_ mpq_op_create_
#   endif
#   define pmpi_op_create_ pmpq_op_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_OP_FREE MPQ_OP_FREE
#   define PMPI_OP_FREE PMPQ_OP_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_op_free__ mpq_op_free__
#   define pmpi_op_free__ pmpq_op_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_op_free mpq_op_free
#   define pmpi_op_free pmpq_op_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_op_free_ mpq_op_free_
#   endif
#   define pmpi_op_free_ pmpq_op_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TOPO_TEST MPQ_TOPO_TEST
#   define PMPI_TOPO_TEST PMPQ_TOPO_TEST
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_topo_test__ mpq_topo_test__
#   define pmpi_topo_test__ pmpq_topo_test__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_topo_test mpq_topo_test
#   define pmpi_topo_test pmpq_topo_test
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_topo_test_ mpq_topo_test_
#   endif
#   define pmpi_topo_test_ pmpq_topo_test_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPHDIMS_GET MPQ_GRAPHDIMS_GET
#   define PMPI_GRAPHDIMS_GET PMPQ_GRAPHDIMS_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graphdims_get__ mpq_graphdims_get__
#   define pmpi_graphdims_get__ pmpq_graphdims_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graphdims_get mpq_graphdims_get
#   define pmpi_graphdims_get pmpq_graphdims_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graphdims_get_ mpq_graphdims_get_
#   endif
#   define pmpi_graphdims_get_ pmpq_graphdims_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPH_GET MPQ_GRAPH_GET
#   define PMPI_GRAPH_GET PMPQ_GRAPH_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graph_get__ mpq_graph_get__
#   define pmpi_graph_get__ pmpq_graph_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graph_get mpq_graph_get
#   define pmpi_graph_get pmpq_graph_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graph_get_ mpq_graph_get_
#   endif
#   define pmpi_graph_get_ pmpq_graph_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CARTDIM_GET MPQ_CARTDIM_GET
#   define PMPI_CARTDIM_GET PMPQ_CARTDIM_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cartdim_get__ mpq_cartdim_get__
#   define pmpi_cartdim_get__ pmpq_cartdim_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cartdim_get mpq_cartdim_get
#   define pmpi_cartdim_get pmpq_cartdim_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cartdim_get_ mpq_cartdim_get_
#   endif
#   define pmpi_cartdim_get_ pmpq_cartdim_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_GET MPQ_CART_GET
#   define PMPI_CART_GET PMPQ_CART_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_get__ mpq_cart_get__
#   define pmpi_cart_get__ pmpq_cart_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_get mpq_cart_get
#   define pmpi_cart_get pmpq_cart_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_get_ mpq_cart_get_
#   endif
#   define pmpi_cart_get_ pmpq_cart_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_DIMS_CREATE MPQ_DIMS_CREATE
#   define PMPI_DIMS_CREATE PMPQ_DIMS_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_dims_create__ mpq_dims_create__
#   define pmpi_dims_create__ pmpq_dims_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_dims_create mpq_dims_create
#   define pmpi_dims_create pmpq_dims_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_dims_create_ mpq_dims_create_
#   endif
#   define pmpi_dims_create_ pmpq_dims_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_MAP MPQ_CART_MAP
#   define PMPI_CART_MAP PMPQ_CART_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_map__ mpq_cart_map__
#   define pmpi_cart_map__ pmpq_cart_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_map mpq_cart_map
#   define pmpi_cart_map pmpq_cart_map
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_map_ mpq_cart_map_
#   endif
#   define pmpi_cart_map_ pmpq_cart_map_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPH_MAP MPQ_GRAPH_MAP
#   define PMPI_GRAPH_MAP PMPQ_GRAPH_MAP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graph_map__ mpq_graph_map__
#   define pmpi_graph_map__ pmpq_graph_map__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graph_map mpq_graph_map
#   define pmpi_graph_map pmpq_graph_map
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graph_map_ mpq_graph_map_
#   endif
#   define pmpi_graph_map_ pmpq_graph_map_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_CREATE MPQ_CART_CREATE
#   define PMPI_CART_CREATE PMPQ_CART_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_create__ mpq_cart_create__
#   define pmpi_cart_create__ pmpq_cart_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_create mpq_cart_create
#   define pmpi_cart_create pmpq_cart_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_create_ mpq_cart_create_
#   endif
#   define pmpi_cart_create_ pmpq_cart_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPH_CREATE MPQ_GRAPH_CREATE
#   define PMPI_GRAPH_CREATE PMPQ_GRAPH_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graph_create__ mpq_graph_create__
#   define pmpi_graph_create__ pmpq_graph_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graph_create mpq_graph_create
#   define pmpi_graph_create pmpq_graph_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graph_create_ mpq_graph_create_
#   endif
#   define pmpi_graph_create_ pmpq_graph_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_RANK MPQ_CART_RANK
#   define PMPI_CART_RANK PMPQ_CART_RANK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_rank__ mpq_cart_rank__
#   define pmpi_cart_rank__ pmpq_cart_rank__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_rank mpq_cart_rank
#   define pmpi_cart_rank pmpq_cart_rank
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_rank_ mpq_cart_rank_
#   endif
#   define pmpi_cart_rank_ pmpq_cart_rank_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_COORDS MPQ_CART_COORDS
#   define PMPI_CART_COORDS PMPQ_CART_COORDS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_coords__ mpq_cart_coords__
#   define pmpi_cart_coords__ pmpq_cart_coords__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_coords mpq_cart_coords
#   define pmpi_cart_coords pmpq_cart_coords
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_coords_ mpq_cart_coords_
#   endif
#   define pmpi_cart_coords_ pmpq_cart_coords_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPH_NEIGHBORS_COUNT MPQ_GRAPH_NEIGHBORS_COUNT
#   define PMPI_GRAPH_NEIGHBORS_COUNT PMPQ_GRAPH_NEIGHBORS_COUNT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graph_neighbors_count__ mpq_graph_neighbors_count__
#   define pmpi_graph_neighbors_count__ pmpq_graph_neighbors_count__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graph_neighbors_count mpq_graph_neighbors_count
#   define pmpi_graph_neighbors_count pmpq_graph_neighbors_count
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graph_neighbors_count_ mpq_graph_neighbors_count_
#   endif
#   define pmpi_graph_neighbors_count_ pmpq_graph_neighbors_count_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GRAPH_NEIGHBORS MPQ_GRAPH_NEIGHBORS
#   define PMPI_GRAPH_NEIGHBORS PMPQ_GRAPH_NEIGHBORS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_graph_neighbors__ mpq_graph_neighbors__
#   define pmpi_graph_neighbors__ pmpq_graph_neighbors__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_graph_neighbors mpq_graph_neighbors
#   define pmpi_graph_neighbors pmpq_graph_neighbors
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_graph_neighbors_ mpq_graph_neighbors_
#   endif
#   define pmpi_graph_neighbors_ pmpq_graph_neighbors_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_SHIFT MPQ_CART_SHIFT
#   define PMPI_CART_SHIFT PMPQ_CART_SHIFT
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_shift__ mpq_cart_shift__
#   define pmpi_cart_shift__ pmpq_cart_shift__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_shift mpq_cart_shift
#   define pmpi_cart_shift pmpq_cart_shift
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_shift_ mpq_cart_shift_
#   endif
#   define pmpi_cart_shift_ pmpq_cart_shift_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_CART_SUB MPQ_CART_SUB
#   define PMPI_CART_SUB PMPQ_CART_SUB
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_cart_sub__ mpq_cart_sub__
#   define pmpi_cart_sub__ pmpq_cart_sub__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_cart_sub mpq_cart_sub
#   define pmpi_cart_sub pmpq_cart_sub
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_cart_sub_ mpq_cart_sub_
#   endif
#   define pmpi_cart_sub_ pmpq_cart_sub_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_PCONTROL MPQ_PCONTROL
#   define PMPI_PCONTROL PMPQ_PCONTROL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_pcontrol__ mpq_pcontrol__
#   define pmpi_pcontrol__ pmpq_pcontrol__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_pcontrol mpq_pcontrol
#   define pmpi_pcontrol pmpq_pcontrol
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_pcontrol_ mpq_pcontrol_
#   endif
#   define pmpi_pcontrol_ pmpq_pcontrol_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_FINALIZED MPQ_FINALIZED
#   define PMPI_FINALIZED PMPQ_FINALIZED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_finalized__ mpq_finalized__
#   define pmpi_finalized__ pmpq_finalized__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_finalized mpq_finalized
#   define pmpi_finalized pmpq_finalized
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_finalized_ mpq_finalized_
#   endif
#   define pmpi_finalized_ pmpq_finalized_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_CREATE_INDEXED_BLOCK MPQ_TYPE_CREATE_INDEXED_BLOCK
#   define PMPI_TYPE_CREATE_INDEXED_BLOCK PMPQ_TYPE_CREATE_INDEXED_BLOCK
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_create_indexed_block__ mpq_type_create_indexed_block__
#   define pmpi_type_create_indexed_block__ pmpq_type_create_indexed_block__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_create_indexed_block mpq_type_create_indexed_block
#   define pmpi_type_create_indexed_block pmpq_type_create_indexed_block
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_create_indexed_block_ mpq_type_create_indexed_block_
#   endif
#   define pmpi_type_create_indexed_block_ pmpq_type_create_indexed_block_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_CREATE_SUBARRAY MPQ_TYPE_CREATE_SUBARRAY
#   define PMPI_TYPE_CREATE_SUBARRAY PMPQ_TYPE_CREATE_SUBARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_create_subarray__ mpq_type_create_subarray__
#   define pmpi_type_create_subarray__ pmpq_type_create_subarray__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_create_subarray mpq_type_create_subarray
#   define pmpi_type_create_subarray pmpq_type_create_subarray
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_create_subarray_ mpq_type_create_subarray_
#   endif
#   define pmpi_type_create_subarray_ pmpq_type_create_subarray_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_CREATE_DARRAY MPQ_TYPE_CREATE_DARRAY
#   define PMPI_TYPE_CREATE_DARRAY PMPQ_TYPE_CREATE_DARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_create_darray__ mpq_type_create_darray__
#   define pmpi_type_create_darray__ pmpq_type_create_darray__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_create_darray mpq_type_create_darray
#   define pmpi_type_create_darray pmpq_type_create_darray
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_create_darray_ mpq_type_create_darray_
#   endif
#   define pmpi_type_create_darray_ pmpq_type_create_darray_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_CREATE MPQ_INFO_CREATE
#   define PMPI_INFO_CREATE PMPQ_INFO_CREATE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_create__ mpq_info_create__
#   define pmpi_info_create__ pmpq_info_create__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_create mpq_info_create
#   define pmpi_info_create pmpq_info_create
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_create_ mpq_info_create_
#   endif
#   define pmpi_info_create_ pmpq_info_create_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_DUP MPQ_INFO_DUP
#   define PMPI_INFO_DUP PMPQ_INFO_DUP
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_dup__ mpq_info_dup__
#   define pmpi_info_dup__ pmpq_info_dup__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_dup mpq_info_dup
#   define pmpi_info_dup pmpq_info_dup
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_dup_ mpq_info_dup_
#   endif
#   define pmpi_info_dup_ pmpq_info_dup_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_GET MPQ_INFO_GET
#   define PMPI_INFO_GET PMPQ_INFO_GET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_get__ mpq_info_get__
#   define pmpi_info_get__ pmpq_info_get__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_get mpq_info_get
#   define pmpi_info_get pmpq_info_get
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_get_ mpq_info_get_
#   endif
#   define pmpi_info_get_ pmpq_info_get_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_GET_NTHKEY MPQ_INFO_GET_NTHKEY
#   define PMPI_INFO_GET_NTHKEY PMPQ_INFO_GET_NTHKEY
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_get_nthkey__ mpq_info_get_nthkey__
#   define pmpi_info_get_nthkey__ pmpq_info_get_nthkey__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_get_nthkey mpq_info_get_nthkey
#   define pmpi_info_get_nthkey pmpq_info_get_nthkey
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_get_nthkey_ mpq_info_get_nthkey_
#   endif
#   define pmpi_info_get_nthkey_ pmpq_info_get_nthkey_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_SET MPQ_INFO_SET
#   define PMPI_INFO_SET PMPQ_INFO_SET
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_set__ mpq_info_set__
#   define pmpi_info_set__ pmpq_info_set__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_set mpq_info_set
#   define pmpi_info_set pmpq_info_set
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_set_ mpq_info_set_
#   endif
#   define pmpi_info_set_ pmpq_info_set_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_DELETE MPQ_INFO_DELETE
#   define PMPI_INFO_DELETE PMPQ_INFO_DELETE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_delete__ mpq_info_delete__
#   define pmpi_info_delete__ pmpq_info_delete__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_delete mpq_info_delete
#   define pmpi_info_delete pmpq_info_delete
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_delete_ mpq_info_delete_
#   endif
#   define pmpi_info_delete_ pmpq_info_delete_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_FREE MPQ_INFO_FREE
#   define PMPI_INFO_FREE PMPQ_INFO_FREE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_free__ mpq_info_free__
#   define pmpi_info_free__ pmpq_info_free__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_free mpq_info_free
#   define pmpi_info_free pmpq_info_free
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_free_ mpq_info_free_
#   endif
#   define pmpi_info_free_ pmpq_info_free_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_GET_NKEYS MPQ_INFO_GET_NKEYS
#   define PMPI_INFO_GET_NKEYS PMPQ_INFO_GET_NKEYS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_get_nkeys__ mpq_info_get_nkeys__
#   define pmpi_info_get_nkeys__ pmpq_info_get_nkeys__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_get_nkeys mpq_info_get_nkeys
#   define pmpi_info_get_nkeys pmpq_info_get_nkeys
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_get_nkeys_ mpq_info_get_nkeys_
#   endif
#   define pmpi_info_get_nkeys_ pmpq_info_get_nkeys_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_INFO_GET_VALUELEN MPQ_INFO_GET_VALUELEN
#   define PMPI_INFO_GET_VALUELEN PMPQ_INFO_GET_VALUELEN
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_info_get_valuelen__ mpq_info_get_valuelen__
#   define pmpi_info_get_valuelen__ pmpq_info_get_valuelen__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_info_get_valuelen mpq_info_get_valuelen
#   define pmpi_info_get_valuelen pmpq_info_get_valuelen
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_info_get_valuelen_ mpq_info_get_valuelen_
#   endif
#   define pmpi_info_get_valuelen_ pmpq_info_get_valuelen_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_GET_CONTENTS MPQ_TYPE_GET_CONTENTS
#   define PMPI_TYPE_GET_CONTENTS PMPQ_TYPE_GET_CONTENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_get_contents__ mpq_type_get_contents__
#   define pmpi_type_get_contents__ pmpq_type_get_contents__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_get_contents mpq_type_get_contents
#   define pmpi_type_get_contents pmpq_type_get_contents
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_get_contents_ mpq_type_get_contents_
#   endif
#   define pmpi_type_get_contents_ pmpq_type_get_contents_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_TYPE_GET_ENVELOPE MPQ_TYPE_GET_ENVELOPE
#   define PMPI_TYPE_GET_ENVELOPE PMPQ_TYPE_GET_ENVELOPE
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_type_get_envelope__ mpq_type_get_envelope__
#   define pmpi_type_get_envelope__ pmpq_type_get_envelope__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_type_get_envelope mpq_type_get_envelope
#   define pmpi_type_get_envelope pmpq_type_get_envelope
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_type_get_envelope_ mpq_type_get_envelope_
#   endif
#   define pmpi_type_get_envelope_ pmpq_type_get_envelope_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_RANGE_INCL MPQ_GROUP_RANGE_INCL
#   define PMPI_GROUP_RANGE_INCL PMPQ_GROUP_RANGE_INCL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_range_incl__ mpq_group_range_incl__
#   define pmpi_group_range_incl__ pmpq_group_range_incl__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_range_incl mpq_group_range_incl
#   define pmpi_group_range_incl pmpq_group_range_incl
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_range_incl_ mpq_group_range_incl_
#   endif
#   define pmpi_group_range_incl_ pmpq_group_range_incl_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_GROUP_RANGE_EXCL MPQ_GROUP_RANGE_EXCL
#   define PMPI_GROUP_RANGE_EXCL PMPQ_GROUP_RANGE_EXCL
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_group_range_excl__ mpq_group_range_excl__
#   define pmpi_group_range_excl__ pmpq_group_range_excl__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_group_range_excl mpq_group_range_excl
#   define pmpi_group_range_excl pmpq_group_range_excl
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_group_range_excl_ mpq_group_range_excl_
#   endif
#   define pmpi_group_range_excl_ pmpq_group_range_excl_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_STATUS_SET_CANCELLED MPQ_STATUS_SET_CANCELLED
#   define PMPI_STATUS_SET_CANCELLED PMPQ_STATUS_SET_CANCELLED
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_status_set_cancelled__ mpq_status_set_cancelled__
#   define pmpi_status_set_cancelled__ pmpq_status_set_cancelled__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_status_set_cancelled mpq_status_set_cancelled
#   define pmpi_status_set_cancelled pmpq_status_set_cancelled
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_status_set_cancelled_ mpq_status_set_cancelled_
#   endif
#   define pmpi_status_set_cancelled_ pmpq_status_set_cancelled_
#endif

#if defined(F77_NAME_UPPER)
#   define MPI_STATUS_SET_ELEMENTS MPQ_STATUS_SET_ELEMENTS
#   define PMPI_STATUS_SET_ELEMENTS PMPQ_STATUS_SET_ELEMENTS
#elif defined(F77_NAME_LOWER_2USCORE)
#   define mpi_status_set_elements__ mpq_status_set_elements__
#   define pmpi_status_set_elements__ pmpq_status_set_elements__
#elif !defined(F77_NAME_LOWER_USCORE)
#   define mpi_status_set_elements mpq_status_set_elements
#   define pmpi_status_set_elements pmpq_status_set_elements
#else
#   if !defined(MPI_BUILD_PROFILING)
#       define mpi_status_set_elements_ mpq_status_set_elements_
#   endif
#   define pmpi_status_set_elements_ pmpq_status_set_elements_
#endif

#endif /* !defined(MPID_FORT_H) */
