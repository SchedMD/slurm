#if defined(CRAY) || defined(TITAN)  ||  defined(NCUBE)

#define p4send_ P4SEND
#define p4sendx_ P4SENDX
#define p4sendr_ P4SENDR
#define p4sendrx_ P4SENDRX
#define p4recv_ P4RECV
#define p4probe_ P4PROBE
#define p4ntotids_ P4NTOTIDS
#define p4nslaves_ P4NSLAVES
#define p4nclids_ P4NCLIDS
#define p4myid_ P4MYID
#define p4myclid_ P4MYCLID
#define p4globarr_ P4GLOBARR
#define p4getclmasts_ P4GETCLMASTS
#define p4getclids_ P4GETCLIDS
#define p4clock_ P4CLOCK
#define p4ustimer_ P4USTIMER
#define p4flush_ P4FLUSH
#define p4init_ P4INIT
#define p4crpg_ P4CRPG
#define p4cleanup_ P4CLEANUP
#define p4brdcst_ P4BRDCST
#define p4brdcstx_ P4BRDCSTX
#define p4error_ P4ERROR
#define p4softerrs_ P4SOFTERRS
#define p4version_ P4VERSION
#define p4avlbufs_ P4AVLBUFS
#define p4setavlbuf_ P4SETAVLBUF
#define p4globop_ P4GLOBOP
#define p4dblsumop_ P4DBLSUMOP
#define p4dblmultop_ P4DBLMULTOP
#define p4dblmaxop_ P4DBLMAXOP
#define p4dblminop_ P4DBLMINOP
#define p4dblabsmaxop_ P4DBLABSMAXOP
#define p4dblabsminop_ P4DBLABSMINOP
#define p4fltsumop_ P4FLTSUMOP
#define p4fltmultop_ P4FLTMULTOP
#define p4fltmaxop_ P4FLTMAXOP
#define p4fltminop_ P4FLTMINOP
#define p4fltabsmaxop_ P4FLTABSMAXOP
#define p4fltabsminop_ P4FLTABSMINOP
#define p4intsumop_ P4INTSUMOP
#define p4intmultop_ P4INTMULTOP
#define p4intmaxop_ P4INTMAXOP
#define p4intminop_ P4INTMINOP
#define p4intabsmaxop_ P4INTABSMAXOP
#define p4intabsminop_ P4INTABSMINOP

#define fslave_ FSLAVE
#define slstart_ SLSTART
#define args_ ARGS
#define numargc_ NUMARGC

#endif

#if defined(NEXT)  ||  defined(RS6000)
/* HP probably also goes here; eliminated temporarily since solaris looks
  like an hp for now  */

#define p4send_ p4send
#define p4sendx_ p4sendx
#define p4sendr_ p4sendr
#define p4sendrx_ p4sendrx
#define p4recv_ p4recv
#define p4probe_ p4probe
#define p4ntotids_ p4ntotids
#define p4nslaves_ p4nslaves
#define p4nclids_ p4nclids
#define p4myid_ p4myid
#define p4myclid_ p4myclid
#define p4globarr_ p4globarr
#define p4getclmasts_ p4getclmasts
#define p4getclids_ p4getclids
#define p4clock_ p4clock
#define p4ustimer_ p4ustimer
#define p4flush_ p4flush
#define p4init_ p4init
#define p4crpg_ p4crpg
#define p4cleanup_ p4cleanup
#define p4brdcst_ p4brdcst
#define p4brdcstx_ p4brdcstx
#define p4error_ p4error
#define p4softerrs_ p4softerrs
#define p4version_ p4version
#define p4avlbufs_ p4avlbufs
#define p4setavlbuf_ p4setavlbuf
#define p4globop_ p4globop
#define p4dblsumop_ p4dblsumop
#define p4dblmultop_ p4dblmultop
#define p4dblmaxop_ p4dblmaxop
#define p4dblminop_ p4dblminop
#define p4dblabsmaxop_ p4dblabsmaxop
#define p4dblabsminop_ p4dblabsminop
#define p4fltsumop_ p4fltsumop
#define p4fltmultop_ p4fltmultop
#define p4fltmaxop_ p4fltmaxop
#define p4fltminop_ p4fltminop
#define p4fltabsmaxop_ p4fltabsmaxop
#define p4fltabsminop_ p4fltabsminop
#define p4intsumop_ p4intsumop
#define p4intmultop_ p4intmultop
#define p4intmaxop_ p4intmaxop
#define p4intminop_ p4intminop
#define p4intabsmaxop_ p4intabsmaxop
#define p4intabsminop_ p4intabsminop

#define fslave_ fslave
#define slstart_ slstart
#define args_ args
#define numargc_ numargc

#endif
