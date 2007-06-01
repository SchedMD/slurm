/* Direct Channel structures */

struct sfc_open 
{
  int                dcd;       /* where to put the resulting dc descriptor */
};

#define NPORT     0
#define IP_ADDR   1

struct sfcaddr_in
{
  int                port;      /* socket-like port number          */
  unsigned long      inet_addr; /* Internet address or NPORT number */
  int                type;      /* given addr is NPORT OR IPADDR    */
};


struct sfc_bind
{
  int                dcd;       /* dc descriptor to be bound        */
  struct sfcaddr_in  *name;     /* addr structure to which the dcd  */
                                /*   should be bound                */
};


struct sfc_connect
{
  int                dcd;       /* dcd requesting the connection             */
  int                timeout;   /* num of seconds before timeout;            */
  struct sfcaddr_in  *name;     /* address of target dc connection port that */ 
                                /*   will form the other end of the          */
                                /*   communications line                     */
};


struct sfc_listen
{
  int                dcd;       /* the dc descriptor doing the listening */
  int                backlog;   /* max outstanding connection requests   */
};


struct sfc_accept
{
  int                dcd;       /* dcd that issued accept routine            */
  int                newdcd;    /* accepted dcd, now refers to new connection*/
  int                blockflag; /* BLOCKING, NOBLOCKING                      */
  struct sfcaddr_in  *address;  /* specifies result parameter that is filled */
  int                timeout;   /* timeout if blocked                        */
};

 
struct sfc_read
{ 
  int                dcd;       /* which dc descriptor to read from */
  caddr_t            buff;      /* where to put the read data       */
  int                nbytes;    /* number of bytes to read          */
  int                blockflag; /* BLOCKING, NONBLOCKING            */
  int                timeout;   /* timeout if blocked               */
};


struct sfc_write
{
  int                dcd;       /* which dc descriptor to write to  */
  caddr_t            buff;      /* where data to write is           */
  int                nbytes;    /* number of bytes to write         */
  int                type;      /* BLOCKING or NONBLOCKING          */
};


struct sfc_close
{
  int                dcd;       /* dcd to close */
};



/* Direct channel command equates */
#define SFC                (('n'<<8)|0x80)
#define SFC_OPEN           (SFC|0x01)   
#define SFC_LISTEN         (SFC|0x02) 
#define SFC_ACCEPT         (SFC|0x03) 
#define SFC_BIND           (SFC|0x04)
#define SFC_CONNECT        (SFC|0x05)  
#define SFC_READ           (SFC|0x07)
#define SFC_WRITE          (SFC|0x09)
#define SFC_CLOSE          (SFC|0x0A)
#define SFC_DEBUG          (SFC|0x0E)  /* not yet implemented */
#define SFC_CFG            (SFC|0x0F)  
#define SFC_ABNCLOSE       (SFC|0x11) 


/* read, write, and accept type equates */
#define BLOCKING           0 
#define NONBLOCKING        1 


/* Miscellaneous Equates */
#define MAXBACKLOG         10     /* max pending listen queue requests */
#define MAXDCDS            254 
#define DCGETPORT          0xFFFFFFFF  
#define DCMAXPORTS         2500 
#define DCMAXLEN           65500  /* 65500 bytes */
#define DCMAXTIMEOUT       10     /* ten seconds */


/* Direct Channel error codes */
#define NOERROR            00  /* successful */
#define EPORTINUSE         10  /* desired port is used by another dcd */
#define EMAXDCDS           12  /* all dcd's are being used */
#define ENOPORTSAVAIL      20  /* all ports are being used */
#define ENOTENABLED        30  /* direct channel is not configured */
#define EALRDYBOUND        40  /* given dcd is already bound to a port */
#define ENOTBOUND          60  /* given dcd is not bound */
#define ENOTCONNECTED      70  /* given dcd is not connected to anyone */
#define ENOCONNECTREQS     80  /* listen queue is empty */
#define E_IO               100 /* general failure */
#define EBOTTLENECK        101 /* receiver unable to accept xmits due to lack
                                  of resources, try again later */
#define ENOARP             102 /* unable to find connection address */ 
#define ETIMEOUT           108 /* request has timed out */ 
#define E_BUSY             109 /* in data read or write process */ 
#define EOUTOFMEM          120 /* not enough mem or unable to pin user buffer */
#define ENOTLISTENING      140 /* dcd has not gotton a listen call */
#define EALRDYCONNECT      150 /* dcd is already connected */
#define EALRDYACCEPTED     160 /* dcd has already accepted someone */
#define EMESSQUEFULL       170 /* receiver's listen q is full */ 
#define EALRDYLISTEN       180 /* dcd is already listening */
#define EINVALIDPAR        210 /* user structure variable is invalid */
#define ETARGETUNDEF       220 /* receiver port is undefined */
#define E_NODATA           250 /* no received data for dcd */ 
#define EOVERFLOWDC        251 /* no more xmit reqs can be accepted */  
#define EDMAFAILURE        252 /* unable to dma data */
#define ENODMAWINS         253 /* out of dma memory */


