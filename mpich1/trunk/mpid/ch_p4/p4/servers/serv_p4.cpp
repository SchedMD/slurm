# 1 "serv_p4.c" 
# 1 "../include/p4.h" 1



# 1 "/usr/include/ctype.h" 1










#ident	"@(#)ctype.h	1.19	95/01/28 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1










#ident	"@(#)feature_tests.h	1.7	94/12/06 SMI"

# 15 "/usr/include/sys/feature_tests.h" 










# 27 "/usr/include/sys/feature_tests.h" 


# 31 "/usr/include/sys/feature_tests.h" 



# 14 "/usr/include/ctype.h" 2

# 17 "/usr/include/ctype.h" 











# 43 "/usr/include/ctype.h" 
# 63 "/usr/include/ctype.h" 
# 77 "/usr/include/ctype.h" 
# 90 "/usr/include/ctype.h" 


extern unsigned char	_ctype[];























# 118 "/usr/include/ctype.h" 



# 5 "../include/p4.h" 2
# 1 "/usr/ucbinclude/stdio.h" 1



























 

#ident	"@(#)stdio.h	1.7	95/06/08 SMI"	







# 1 "/usr/include/sys/va_list.h" 1








#ident	"@(#)va_list.h	1.6	96/01/26 SMI"











# 23 "/usr/include/sys/va_list.h" 


# 41 "/usr/include/sys/va_list.h" 


# 45 "/usr/include/sys/va_list.h" 

typedef char *__va_list;




# 53 "/usr/include/sys/va_list.h" 



# 39 "/usr/ucbinclude/stdio.h" 2
				








typedef unsigned int 	size_t;


typedef long	fpos_t;





# 58 "/usr/ucbinclude/stdio.h" 
# 70 "/usr/ucbinclude/stdio.h" 


# 73 "/usr/ucbinclude/stdio.h" 
# 83 "/usr/ucbinclude/stdio.h" 






# 92 "/usr/ucbinclude/stdio.h" 
# 91 "/usr/ucbinclude/stdio.h" 


# 95 "/usr/ucbinclude/stdio.h" 


































# 133 "/usr/ucbinclude/stdio.h" 






typedef struct	
{
# 144 "/usr/ucbinclude/stdio.h" 

	int		_cnt;	
	unsigned char	*_ptr;	

	unsigned char	*_base;	
	unsigned char	_flag;	
	unsigned char	_file;	
} FILE;

# 155 "/usr/ucbinclude/stdio.h" 

extern FILE		_iob[20];

extern FILE		*_lastbuf;
extern unsigned char 	*_bufendtab[];

extern unsigned char	 _sibuf[], _sobuf[];


# 216 "/usr/ucbinclude/stdio.h" 
# 227 "/usr/ucbinclude/stdio.h" 
# 240 "/usr/ucbinclude/stdio.h" 
# 247 "/usr/ucbinclude/stdio.h" 

















extern FILE     *fopen(), *fdopen(), *freopen(), *popen();
extern long     ftell();
extern void     rewind(), setbuf();
extern char     *ctermid(), *cuserid(), *fgets(), *gets(), *sprintf(),
		*vsprintf();
extern int      fclose(), fflush(), fread(), fwrite(), fseek(), fgetc(),
                getw(), pclose(), printf(), fprintf(),
                vprintf(), vfprintf(), fputc(), putw(),
                puts(), fputs(), scanf(), fscanf(), sscanf(),
                setvbuf(), system(), ungetc();




# 6 "../include/p4.h" 2

# 9 "../include/p4.h" 





# 1 "/usr/include/rpc/rpc.h" 1













#ident	"@(#)rpc.h	1.11	93/02/04 SMI"



# 1 "/usr/include/rpc/types.h" 1







#ident	"@(#)types.h	1.18	94/03/08 SMI"






# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 





# 39 "/usr/ucbinclude/sys/types.h" 

# 43 "/usr/ucbinclude/sys/types.h" 


typedef	struct _physadr { int r[1]; } *physadr;
typedef	struct _label { int val[2]; } label_t;
# 50 "/usr/ucbinclude/sys/types.h" 






typedef unsigned char   uchar_t;
typedef unsigned short  ushort_t;
typedef unsigned int    uint_t;
typedef unsigned long   ulong_t;



typedef char *          addr_t; 

typedef char *          caddr_t;        
typedef long            daddr_t;        
typedef long            off_t;          
typedef short           cnt_t;          
typedef ulong_t 	paddr_t;        
typedef uchar_t 	use_t;          
typedef short           sysid_t;
typedef short           index_t;
typedef	long		swblk_t;
typedef short           lock_t;         
typedef enum boolean { B_FALSE, B_TRUE } boolean_t;
typedef ulong_t		l_dev_t;
















typedef	long long		longlong_t;
typedef	unsigned long long	u_longlong_t;
# 105 "/usr/ucbinclude/sys/types.h" 


typedef	longlong_t	offset_t;
typedef	longlong_t	diskaddr_t;








typedef union lloff {
	offset_t	_f;	
	struct {
		long _u;	
		off_t _l;	
	} _p;
} lloff_t;

typedef union lldaddr {
	diskaddr_t	_f;	
	struct {
		long _u;	
		daddr_t _l;	
	} _p;
} lldaddr_t;

typedef ulong_t k_fltset_t;	

typedef long            id_t;           
                                        
                                        
                                        
                                        





typedef ulong_t major_t;        
typedef ulong_t minor_t;        

typedef short	pri_t;












typedef ushort_t o_mode_t;              
typedef short   o_dev_t;                
typedef ushort_t o_uid_t;               
typedef o_uid_t o_gid_t;                
typedef short   o_nlink_t;              
typedef short   o_pid_t;                
typedef ushort_t o_ino_t;               
 



typedef int	key_t;			
typedef ulong_t mode_t;                 



typedef long    uid_t;                  


typedef uid_t   gid_t;                  
typedef ulong_t nlink_t;                
typedef ulong_t dev_t;			
typedef ulong_t ino_t;			
typedef long    pid_t;                  

# 189 "/usr/ucbinclude/sys/types.h" 




typedef int	ssize_t;	
				




typedef long            time_t;         




typedef long            clock_t; 




typedef unsigned char   unchar;
typedef unsigned int    uint;
typedef unsigned long   ulong;

# 225 "/usr/ucbinclude/sys/types.h" 























typedef	long	hostid_t;











typedef unsigned char	u_char;
typedef unsigned short	u_short;
typedef unsigned int	u_int;
typedef unsigned long	u_long;
typedef unsigned short	ushort;		
typedef struct _quad { long val[2]; } quad;	






# 280 "/usr/ucbinclude/sys/types.h" 





















# 1 "/usr/include/sys/select.h" 1










#ident	"@(#)select.h	1.10	92/07/14 SMI"	


# 1 "/usr/include/sys/time.h" 1





















#ident	"@(#)time.h	2.47	95/08/24 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 25 "/usr/include/sys/time.h" 2






# 33 "/usr/include/sys/time.h" 


# 36 "/usr/include/sys/time.h" 



struct timeval {
	long	tv_sec;		
	long	tv_usec;	
};

struct timezone {
	int	tz_minuteswest;	
	int	tz_dsttime;	
};




# 54 "/usr/include/sys/time.h" 







# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 62 "/usr/include/sys/time.h" 2


# 66 "/usr/include/sys/time.h" 


# 69 "/usr/include/sys/time.h" 







































struct	itimerval {
	struct	timeval it_interval;	
	struct	timeval it_value;	
};













# 126 "/usr/include/sys/time.h" 











# 139 "/usr/include/sys/time.h" 
















# 156 "/usr/include/sys/time.h" 

typedef struct  timespec {		
	time_t		tv_sec;		
	long		tv_nsec;	
} timespec_t;

typedef struct timespec timestruc_t;	

# 172 "/usr/include/sys/time.h" 







# 180 "/usr/include/sys/time.h" 







# 188 "/usr/include/sys/time.h" 

typedef struct itimerspec {		
	struct timespec	it_interval;	
	struct timespec	it_value;	
} itimerspec_t;





typedef	longlong_t	hrtime_t;

# 236 "/usr/include/sys/time.h" 


# 239 "/usr/include/sys/time.h" 

# 258 "/usr/include/sys/time.h" 
# 269 "/usr/include/sys/time.h" 

int adjtime();
int getitimer();
int setitimer();
int gettimeofday();
int settimeofday();

hrtime_t	gethrtime();
hrtime_t	gethrvtime();


# 1 "/usr/include/time.h" 1













#ident	"@(#)time.h	1.23	95/08/28 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 17 "/usr/include/time.h" 2

# 20 "/usr/include/time.h" 


# 24 "/usr/include/time.h" 


# 29 "/usr/include/time.h" 

# 33 "/usr/include/time.h" 

# 37 "/usr/include/time.h" 



typedef int	clockid_t;



typedef int	timer_t;




struct	tm {	
	int	tm_sec;
	int	tm_min;
	int	tm_hour;
	int	tm_mday;
	int	tm_mon;
	int	tm_year;
	int	tm_wday;
	int	tm_yday;
	int	tm_isdst;
};

# 72 "/usr/include/time.h" 
# 78 "/usr/include/time.h" 
# 84 "/usr/include/time.h" 
# 104 "/usr/include/time.h" 
# 116 "/usr/include/time.h" 
# 124 "/usr/include/time.h" 
# 140 "/usr/include/time.h" 


extern long clock();
extern double difftime();
extern time_t mktime();
extern time_t time();
extern size_t strftime();
extern struct tm *gmtime(), *localtime();
extern char *ctime(), *asctime(), *strptime();
extern int cftime(), ascftime();
extern void tzset();

# 153 "/usr/include/time.h" 
# 156 "/usr/include/time.h" 


extern long timezone, altzone;
extern int daylight;
extern char *tzname[2];

extern struct tm *getdate();
# 167 "/usr/include/time.h" 

extern int getdate_err;







# 177 "/usr/include/time.h" 
# 178 "/usr/include/time.h" 
# 180 "/usr/include/time.h" 
# 212 "/usr/include/time.h" 
# 249 "/usr/include/time.h" 


# 253 "/usr/include/time.h" 



# 281 "/usr/include/sys/time.h" 2





# 288 "/usr/include/sys/time.h" 



# 15 "/usr/include/sys/select.h" 2


# 19 "/usr/include/sys/select.h" 
















typedef	long	fd_mask;





typedef	struct fd_set {
	fd_mask	fds_bits[(((1024)+(( (sizeof (fd_mask) * 8))-1))/( (sizeof (fd_mask) * 8)))];
} fd_set;










# 56 "/usr/include/sys/select.h" 





# 63 "/usr/include/sys/select.h" 

extern int select();



# 70 "/usr/include/sys/select.h" 



# 302 "/usr/ucbinclude/sys/types.h" 2






# 1 "/usr/ucbinclude/sys/sysmacros.h" 1







#ident	"@(#)sysmacros.h	1.1	90/04/27 SMI"	





















 


















# 309 "/usr/ucbinclude/sys/types.h" 2


# 16 "/usr/include/rpc/types.h" 2

# 19 "/usr/include/rpc/types.h" 


typedef int bool_t;
typedef int enum_t;





typedef u_longlong_t ulonglong_t;











# 42 "/usr/include/rpc/types.h" 





# 66 "/usr/include/rpc/types.h" 


# 76 "/usr/include/rpc/types.h" 




# 82 "/usr/include/rpc/types.h" 

extern char __nsl_dom[];



# 89 "/usr/include/rpc/types.h" 


# 1 "/usr/include/sys/time.h" 1


















# 34 "/usr/include/sys/time.h" 
# 67 "/usr/include/sys/time.h" 
# 124 "/usr/include/sys/time.h" 
# 136 "/usr/include/sys/time.h" 
# 154 "/usr/include/sys/time.h" 
# 178 "/usr/include/sys/time.h" 
# 186 "/usr/include/sys/time.h" 
# 237 "/usr/include/sys/time.h" 
# 239 "/usr/include/sys/time.h" 
# 258 "/usr/include/sys/time.h" 
# 278 "/usr/include/sys/time.h" 
# 290 "/usr/include/sys/time.h" 

# 92 "/usr/include/rpc/types.h" 2


# 19 "/usr/include/rpc/rpc.h" 2


# 1 "/usr/include/tiuser.h" 1










#ident	"@(#)tiuser.h	1.12	95/09/10 SMI"	





# 1 "/usr/include/sys/tiuser.h" 1










#ident	"@(#)tiuser.h	1.13	94/12/23 SMI"	

# 15 "/usr/include/sys/tiuser.h" 

























































struct t_info {
	long addr;	
	long options;	
	long tsdu;	
	long etsdu;	
	long connect;	
	long discon;	
	long servtype;	
};












struct netbuf {
	unsigned int maxlen;
	unsigned int len;
	char *buf;
};





struct t_bind {
	struct netbuf	addr;
	unsigned	qlen;
};




struct t_optmgmt {
	struct netbuf	opt;
	long		flags;
};




struct t_discon {
	struct netbuf udata;		
	int reason;			
	int sequence;			
};




struct t_call {
	struct netbuf addr;		
	struct netbuf opt;		
	struct netbuf udata;		
	int sequence;			
};




struct t_unitdata {
	struct netbuf addr;		
	struct netbuf opt;		
	struct netbuf udata;		
};




struct t_uderr {
	struct netbuf addr;		
	struct netbuf opt;		
	long	error;			
};




































					

					
					
					
					
					
					
					
					
					





































extern char tiusr_statetbl[25][9];

















# 291 "/usr/include/sys/tiuser.h" 


# 295 "/usr/include/sys/tiuser.h" 



# 18 "/usr/include/tiuser.h" 2

# 21 "/usr/include/tiuser.h" 


# 24 "/usr/include/tiuser.h" 
# 27 "/usr/include/tiuser.h" 

extern int t_errno;


# 33 "/usr/include/tiuser.h" 



# 22 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/ucbinclude/fcntl.h" 1







#ident	"@(#)fcntl.h	1.1	90/04/27 SMI"	





















 




# 37 "/usr/ucbinclude/fcntl.h" 

































# 74 "/usr/ucbinclude/fcntl.h" 


# 78 "/usr/ucbinclude/fcntl.h" 






































 

 





 



 



# 153 "/usr/ucbinclude/fcntl.h" 


# 166 "/usr/ucbinclude/fcntl.h" 


typedef struct flock {
	short	l_type;
	short	l_whence;
	off_t	l_start;
	off_t	l_len;		
	long	l_sysid;
        pid_t	l_pid;
	long 	pad[4];		
} flock_t;















 


 


# 23 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/memory.h" 1










#ident	"@(#)memory.h	1.8	92/07/14 SMI"	

# 15 "/usr/include/memory.h" 



# 21 "/usr/include/memory.h" 


# 29 "/usr/include/memory.h" 

extern void
	*memccpy(),
	*memchr(),
	*memcpy(),
	*memset();
extern int memcmp();


# 40 "/usr/include/memory.h" 



# 24 "/usr/include/rpc/rpc.h" 2
# 29 "/usr/include/rpc/rpc.h" 


# 1 "/usr/include/rpc/xdr.h" 1













#ident	"@(#)xdr.h	1.26	95/02/06 SMI"



# 1 "/usr/include/sys/byteorder.h" 1










#ident	"@(#)byteorder.h	1.9	94/01/04 SMI"	

# 1 "/usr/include/sys/isa_defs.h" 1







#ident	"@(#)isa_defs.h	1.7	94/10/26 SMI"














































































































# 121 "/usr/include/sys/isa_defs.h" 










# 135 "/usr/include/sys/isa_defs.h" 
# 175 "/usr/include/sys/isa_defs.h" 
# 212 "/usr/include/sys/isa_defs.h" 
# 211 "/usr/include/sys/isa_defs.h" 





# 218 "/usr/include/sys/isa_defs.h" 

































# 253 "/usr/include/sys/isa_defs.h" 


# 257 "/usr/include/sys/isa_defs.h" 



# 14 "/usr/include/sys/byteorder.h" 2

# 17 "/usr/include/sys/byteorder.h" 


































# 56 "/usr/include/sys/byteorder.h" 


# 60 "/usr/include/sys/byteorder.h" 



# 19 "/usr/include/rpc/xdr.h" 2
# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 20 "/usr/include/rpc/xdr.h" 2

# 1 "/usr/ucbinclude/stdio.h" 1



























 

#ident	"@(#)stdio.h	1.7	95/06/08 SMI"	




# 56 "/usr/ucbinclude/stdio.h" 
# 58 "/usr/ucbinclude/stdio.h" 
# 71 "/usr/ucbinclude/stdio.h" 
# 73 "/usr/ucbinclude/stdio.h" 
# 88 "/usr/ucbinclude/stdio.h" 
# 120 "/usr/ucbinclude/stdio.h" 
# 128 "/usr/ucbinclude/stdio.h" 
# 152 "/usr/ucbinclude/stdio.h" 
# 163 "/usr/ucbinclude/stdio.h" 
# 216 "/usr/ucbinclude/stdio.h" 
# 227 "/usr/ucbinclude/stdio.h" 
# 240 "/usr/ucbinclude/stdio.h" 
# 277 "/usr/ucbinclude/stdio.h" 

# 22 "/usr/include/rpc/xdr.h" 2

# 25 "/usr/include/rpc/xdr.h" 


# 29 "/usr/include/rpc/xdr.h" 





































enum xdr_op {
	XDR_ENCODE = 0,
	XDR_DECODE = 1,
	XDR_FREE = 2
};














typedef struct XDR {
	enum xdr_op	x_op;	
	struct xdr_ops {
# 106 "/usr/include/rpc/xdr.h" 

		bool_t	(*x_getlong)();	
		bool_t	(*x_putlong)();	
		bool_t	(*x_getbytes)(); 
		bool_t	(*x_putbytes)(); 
		u_int	(*x_getpostn)(); 
		bool_t  (*x_setpostn)(); 
		long *	(*x_inline)(); 
		void	(*x_destroy)();	
		bool_t	(*x_control)();

	} *x_ops;
	caddr_t 	x_public; 
	caddr_t		x_private; 
	caddr_t 	x_base;	
	int		x_handy; 
} XDR;












































































# 201 "/usr/include/rpc/xdr.h" 

typedef	bool_t (*xdrproc_t)();



struct xdr_discrim {
	int	value;
	xdrproc_t proc;
};







































# 287 "/usr/include/rpc/xdr.h" 

extern bool_t	xdr_void();
extern bool_t	xdr_int();
extern bool_t	xdr_u_int();
extern bool_t	xdr_long();
extern bool_t	xdr_u_long();
extern bool_t	xdr_short();
extern bool_t	xdr_u_short();
extern bool_t	xdr_bool();
extern bool_t	xdr_enum();
extern bool_t	xdr_array();
extern bool_t	xdr_bytes();
extern bool_t	xdr_opaque();
extern bool_t	xdr_string();
extern bool_t	xdr_union();

extern bool_t   xdr_hyper();
extern bool_t   xdr_longlong_t();
extern bool_t   xdr_u_hyper();
extern bool_t   xdr_u_longlong_t();
extern bool_t	xdr_char();
extern bool_t	xdr_reference();
extern bool_t	xdr_pointer();
extern void	xdr_free();
extern bool_t	xdr_wrapstring();


extern bool_t	xdr_u_char();
extern bool_t	xdr_vector();
extern bool_t	xdr_float();
extern bool_t	xdr_double();
extern bool_t   xdr_quadruple();








struct netobj {
	u_int	n_len;
	char	*n_bytes;
};
typedef struct netobj netobj;

# 335 "/usr/include/rpc/xdr.h" 

extern bool_t   xdr_netobj();








struct xdr_bytesrec {
	bool_t xc_is_last_record;
	size_t xc_num_avail;
};

typedef struct xdr_bytesrec xdr_bytesrec;







# 361 "/usr/include/rpc/xdr.h" 







# 384 "/usr/include/rpc/xdr.h" 

extern void   xdrmem_create();
extern void   xdrstdio_create();
extern void   xdrrec_create();
extern bool_t xdrrec_endofrecord();
extern bool_t xdrrec_skiprecord();
extern bool_t xdrrec_eof();
extern u_int xdrrec_readbytes();

# 407 "/usr/include/rpc/xdr.h" 


# 411 "/usr/include/rpc/xdr.h" 



# 32 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/auth.h" 1















#ident	"@(#)auth.h	1.27	94/08/09 SMI"

# 1 "/usr/include/rpc/xdr.h" 1










# 413 "/usr/include/rpc/xdr.h" 

# 19 "/usr/include/rpc/auth.h" 2
# 1 "/usr/include/sys/cred.h" 1

































#ident	"@(#)cred.h	1.18	94/12/04 SMI"	

# 38 "/usr/include/sys/cred.h" 


# 42 "/usr/include/sys/cred.h" 








typedef struct cred {
	ulong_t	cr_ref;			
	uid_t	cr_uid;			
	gid_t	cr_gid;			
	uid_t	cr_ruid;		
	gid_t	cr_rgid;		
	uid_t	cr_suid;		
	gid_t	cr_sgid;		
	ulong_t	cr_ngroups;		
	gid_t	cr_groups[1];		
} cred_t;

# 90 "/usr/include/sys/cred.h" 


# 94 "/usr/include/sys/cred.h" 



# 20 "/usr/include/rpc/auth.h" 2

# 23 "/usr/include/rpc/auth.h" 








enum auth_stat {
	AUTH_OK = 0,
	


	AUTH_BADCRED = 1,		
	AUTH_REJECTEDCRED = 2,		
	AUTH_BADVERF = 3,		
	AUTH_REJECTEDVERF = 4,		
	AUTH_TOOWEAK = 5,		
	


	AUTH_INVALIDRESP = 6,		
	AUTH_FAILED = 7,			
	


	AUTH_KERB_GENERIC = 8,		
	AUTH_TIMEEXPIRE = 9,		
	AUTH_TKT_FILE = 10,		
	AUTH_DECODE = 11,		
	AUTH_NET_ADDR = 12		
};
typedef enum auth_stat AUTH_STAT;







union des_block {
	struct  {
		u_long high;
		u_long low;
	} key;
	char c[8];
};
typedef union des_block des_block;

# 74 "/usr/include/rpc/auth.h" 

extern bool_t xdr_des_block();






struct opaque_auth {
	enum_t	oa_flavor;		
	caddr_t	oa_base;		
	u_int	oa_length;		
};





typedef struct __auth {
	struct	opaque_auth	ah_cred;
	struct	opaque_auth	ah_verf;
	union	des_block	ah_key;
	struct auth_ops {
# 116 "/usr/include/rpc/auth.h" 

		void	(*ah_nextverf)();
		int	(*ah_marshal)();	
		int	(*ah_validate)();	
		int	(*ah_refresh)();	
		void	(*ah_destroy)();	

	} *ah_ops;
	caddr_t ah_private;
} AUTH;
















# 147 "/usr/include/rpc/auth.h" 













# 165 "/usr/include/rpc/auth.h" 













extern struct opaque_auth _null_auth;















# 196 "/usr/include/rpc/auth.h" 

# 202 "/usr/include/rpc/auth.h" 

extern AUTH *authsys_create();
extern AUTH *authsys_create_default();	
extern AUTH *authnone_create();	
















# 224 "/usr/include/rpc/auth.h" 

# 228 "/usr/include/rpc/auth.h" 

extern AUTH *authdes_seccreate();








# 244 "/usr/include/rpc/auth.h" 

# 253 "/usr/include/rpc/auth.h" 

extern int getnetname();
extern int host2netname();
extern int user2netname();
extern int netname2host();








# 270 "/usr/include/rpc/auth.h" 


# 280 "/usr/include/rpc/auth.h" 


extern int key_decryptsession();
extern int key_encryptsession();
extern int key_gendes();
extern int key_setsecret();
extern int key_secretkey_is_set();














# 303 "/usr/include/rpc/auth.h" 

# 307 "/usr/include/rpc/auth.h" 

extern AUTH *authkerb_seccreate();














# 326 "/usr/include/rpc/auth.h" 

extern int authkerb_getucred();


# 339 "/usr/include/rpc/auth.h" 


# 345 "/usr/include/rpc/auth.h" 










# 357 "/usr/include/rpc/auth.h" 


# 361 "/usr/include/rpc/auth.h" 



# 33 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/clnt.h" 1












#ident	"@(#)clnt.h	1.36	95/02/06 SMI"



# 1 "/usr/include/rpc/rpc_com.h" 1













#ident	"@(#)rpc_com.h	1.18	95/01/09 SMI"

# 18 "/usr/include/rpc/rpc_com.h" 





















# 46 "/usr/include/rpc/rpc_com.h" 

extern u_int __rpc_get_t_size();
extern u_int __rpc_get_a_size();
extern int __rpc_dtbsize();
extern struct netconfig *__rpcfd_to_nconf();
extern int __rpc_matchserv();
extern  int __rpc_get_default_domain();




# 59 "/usr/include/rpc/rpc_com.h" 

bool_t rpc_control();

















# 80 "/usr/include/rpc/rpc_com.h" 



# 18 "/usr/include/rpc/clnt.h" 2





# 1 "/usr/include/sys/netconfig.h" 1










#ident	"@(#)netconfig.h	1.13	95/02/24 SMI"	

# 15 "/usr/include/sys/netconfig.h" 





struct  netconfig {
	char		*nc_netid;	
	unsigned long	nc_semantics;	
	unsigned long	nc_flag;	
	char		*nc_protofmly;	
	char		*nc_proto;	
	char		*nc_device;	
	unsigned long	nc_nlookups;	
	char		**nc_lookups;	
	unsigned long	nc_unused[8];
};

typedef struct {
	struct netconfig **nc_head;
	struct netconfig **nc_curr;
} NCONF_HANDLE;























































# 102 "/usr/include/sys/netconfig.h" 


extern void		*setnetconfig();
extern int		endnetconfig();
extern struct netconfig	*getnetconfig();
extern struct netconfig	*getnetconfigent();
extern void		freenetconfigent();
extern void		*setnetpath();
extern int		endnetpath();
extern struct netconfig *getnetpath();



# 117 "/usr/include/sys/netconfig.h" 



# 24 "/usr/include/rpc/clnt.h" 2
# 26 "/usr/include/rpc/clnt.h" 


# 30 "/usr/include/rpc/clnt.h" 


enum clnt_stat {
	RPC_SUCCESS = 0,			
	


	RPC_CANTENCODEARGS = 1,		
	RPC_CANTDECODERES = 2,		
	RPC_CANTSEND = 3,			
	RPC_CANTRECV = 4,
	
	RPC_TIMEDOUT = 5,			
	RPC_INTR = 18,			
	RPC_UDERROR = 23,			
	


	RPC_VERSMISMATCH = 6,		
	RPC_AUTHERROR = 7,		
	RPC_PROGUNAVAIL = 8,		
	RPC_PROGVERSMISMATCH = 9,		
	RPC_PROCUNAVAIL = 10,		
	RPC_CANTDECODEARGS = 11,		
	RPC_SYSTEMERROR = 12,		

	


	RPC_UNKNOWNHOST = 13,		
	RPC_UNKNOWNPROTO = 17,		
	RPC_UNKNOWNADDR = 19,		
	RPC_NOBROADCAST = 21,		

	


	RPC_RPCBFAILURE = 14,		

	RPC_PROGNOTREGISTERED = 15,	
	RPC_N2AXLATEFAILURE = 22,
	
	


	RPC_TLIERROR = 20,
	


	RPC_FAILED = 16,
	


	RPC_INPROGRESS = 24,
	RPC_STALERACHANDLE = 25,
	RPC_CANTCONNECT = 26,		
	RPC_XPRTFAILED = 27,		
	RPC_CANTCREATESTREAM = 28	
};





struct rpc_err {
	enum clnt_stat re_status;
	union {
		struct {
			int RE_errno;	
			int RE_t_errno;	
		} RE_err;
		enum auth_stat RE_why;	
		struct {
			u_long low;	
			u_long high;	
		} RE_vers;
		struct {		
			long s1;
			long s2;
		} RE_lb;		
	} ru;





};





struct rpc_timers {
	u_short		rt_srtt;	
	u_short		rt_deviate;	
	u_long		rt_rtxcur;	
};







typedef struct __client {
	AUTH	*cl_auth;			
	struct clnt_ops {
# 157 "/usr/include/rpc/clnt.h" 

		enum clnt_stat	(*cl_call)();	
		void		(*cl_abort)();	
		void		(*cl_geterr)();	
		bool_t		(*cl_freeres)(); 
		void		(*cl_destroy)(); 
		bool_t		(*cl_control)(); 
		int		(*cl_settimers)(); 

	} *cl_ops;
	caddr_t			cl_private;	

	char			*cl_netid;	
	char			*cl_tp;		
# 173 "/usr/include/rpc/clnt.h" 

} CLIENT;





















struct knetconfig {
	unsigned long	knc_semantics;	
	char		*knc_protofmly;	
	char		*knc_proto;	
	dev_t		knc_rdev;	
	unsigned long	knc_unused[8];
};

# 275 "/usr/include/rpc/clnt.h" 















































































































































# 428 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_create();






# 447 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_create_timed();






# 466 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_create_vers();







# 484 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_tp_create();






# 502 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_tp_create_timed();






# 523 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_tli_create();





# 541 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_vc_create();





# 559 "/usr/include/rpc/clnt.h" 

extern CLIENT * clnt_dg_create();









# 572 "/usr/include/rpc/clnt.h" 

extern CLIENT *clnt_raw_create();






# 583 "/usr/include/rpc/clnt.h" 

void clnt_pcreateerror();
char *clnt_spcreateerror();





# 593 "/usr/include/rpc/clnt.h" 

void clnt_perrno();





# 605 "/usr/include/rpc/clnt.h" 

void clnt_perror();
char *clnt_sperror();





struct rpc_createerr {
	enum clnt_stat cf_stat;
	struct rpc_err cf_error; 
};

# 621 "/usr/include/rpc/clnt.h" 

extern struct rpc_createerr rpc_createerr;













# 639 "/usr/include/rpc/clnt.h" 

extern enum clnt_stat rpc_call();














































typedef bool_t(*resultproc_t)(
# 691 "/usr/include/rpc/clnt.h" 

);
# 700 "/usr/include/rpc/clnt.h" 

extern enum clnt_stat rpc_broadcast();
extern enum clnt_stat rpc_broadcast_exp();






# 711 "/usr/include/rpc/clnt.h" 

char *clnt_sperrno();	


# 717 "/usr/include/rpc/clnt.h" 


# 721 "/usr/include/rpc/clnt.h" 


# 726 "/usr/include/rpc/clnt.h" 



# 34 "/usr/include/rpc/rpc.h" 2

# 1 "/usr/include/rpc/rpc_msg.h" 1







#ident	"@(#)rpc_msg.h	1.13	94/10/19 SMI"



# 1 "/usr/include/rpc/clnt.h" 1









# 728 "/usr/include/rpc/clnt.h" 

# 13 "/usr/include/rpc/rpc_msg.h" 2





# 20 "/usr/include/rpc/rpc_msg.h" 











enum msg_type {
	CALL = 0,
	REPLY = 1
};

enum reply_stat {
	MSG_ACCEPTED = 0,
	MSG_DENIED = 1
};

enum accept_stat {
	SUCCESS = 0,
	PROG_UNAVAIL = 1,
	PROG_MISMATCH = 2,
	PROC_UNAVAIL = 3,
	GARBAGE_ARGS = 4,
	SYSTEM_ERR = 5
};

enum reject_stat {
	RPC_MISMATCH = 0,
	AUTH_ERROR = 1
};










struct accepted_reply {
	struct opaque_auth	ar_verf;
	enum accept_stat	ar_stat;
	union {
		struct {
			u_long	low;
			u_long	high;
		} AR_versions;
		struct {
			caddr_t	where;
			xdrproc_t proc;
		} AR_results;
		
	} ru;


};




struct rejected_reply {
	enum reject_stat rj_stat;
	union {
		struct {
			u_long low;
			u_long high;
		} RJ_versions;
		enum auth_stat RJ_why;  
	} ru;


};




struct reply_body {
	enum reply_stat rp_stat;
	union {
		struct accepted_reply RP_ar;
		struct rejected_reply RP_dr;
	} ru;


};




struct call_body {
	u_long cb_rpcvers;	
	u_long cb_prog;
	u_long cb_vers;
	u_long cb_proc;
	struct opaque_auth cb_cred;
	struct opaque_auth cb_verf; 
};




struct rpc_msg {
	u_long			rm_xid;
	enum msg_type		rm_direction;
	union {
		struct call_body RM_cmb;
		struct reply_body RM_rmb;
	} ru;


};










# 148 "/usr/include/rpc/rpc_msg.h" 

extern bool_t	xdr_callmsg();









# 161 "/usr/include/rpc/rpc_msg.h" 

extern bool_t	xdr_callhdr();









# 174 "/usr/include/rpc/rpc_msg.h" 

extern bool_t	xdr_replymsg();



# 191 "/usr/include/rpc/rpc_msg.h" 







# 200 "/usr/include/rpc/rpc_msg.h" 

extern void	__seterr_reply();



# 211 "/usr/include/rpc/rpc_msg.h" 


# 215 "/usr/include/rpc/rpc_msg.h" 



# 36 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/auth_sys.h" 1












#ident	"@(#)auth_sys.h	1.14	95/01/26 SMI"








# 24 "/usr/include/rpc/auth_sys.h" 











struct authsys_parms {
	u_long	 aup_time;
	char	*aup_machname;
	uid_t	 aup_uid;
	gid_t	 aup_gid;
	u_int	 aup_len;
	gid_t	*aup_gids;
};



# 48 "/usr/include/rpc/auth_sys.h" 

extern bool_t xdr_authsys_parms();











struct short_hand_verf {
	struct opaque_auth new_cred;
};

# 75 "/usr/include/rpc/auth_sys.h" 


# 79 "/usr/include/rpc/auth_sys.h" 



# 37 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/auth_des.h" 1







#ident	"@(#)auth_des.h	1.15	95/10/20 SMI"






# 17 "/usr/include/rpc/auth_des.h" 


# 21 "/usr/include/rpc/auth_des.h" 






enum authdes_namekind {
	ADN_FULLNAME,
	ADN_NICKNAME
};





struct authdes_fullname {
	char *name;	
	des_block key;	
	u_long window;	
};





struct authdes_cred {
	enum authdes_namekind adc_namekind;
	struct authdes_fullname adc_fullname;
	u_long adc_nickname;
};




struct authdes_verf {
	union {
		struct timeval adv_ctime;	
		des_block adv_xtime;		
	} adv_time_u;
	u_long adv_int_u;
};



































# 100 "/usr/include/rpc/auth_des.h" 

extern int	authdes_getucred();



# 108 "/usr/include/rpc/auth_des.h" 

extern int	getpublickey();
extern int	getsecretkey();



# 138 "/usr/include/rpc/auth_des.h" 


# 142 "/usr/include/rpc/auth_des.h" 



# 38 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/auth_kerb.h" 1









#ident	"@(#)auth_kerb.h	1.10	95/04/04 SMI"

# 1 "/usr/include/kerberos/krb.h" 1
















#ident	"@(#)krb.h	1.8	94/02/16 SMI"

# 1 "/usr/include/kerberos/mit-copyright.h" 1
























#ident	"@(#)mit-copyright.h	1.4	93/02/04 SMI"


# 20 "/usr/include/kerberos/krb.h" 2
# 1 "/usr/include/kerberos/des.h" 1
















#ident	"@(#)des.h	1.5	93/05/27 SMI"

# 1 "/usr/include/kerberos/mit-copyright.h" 1





















# 27 "/usr/include/kerberos/mit-copyright.h" 

# 20 "/usr/include/kerberos/des.h" 2

# 23 "/usr/include/kerberos/des.h" 


typedef unsigned char des_cblock[8];	

typedef struct des_ks_struct { des_cblock _; } des_key_schedule[16];




















typedef struct des_ks_struct bit_64;




# 55 "/usr/include/kerberos/des.h" 



# 21 "/usr/include/kerberos/krb.h" 2

# 24 "/usr/include/kerberos/krb.h" 





extern char *krb_err_txt[256];


# 36 "/usr/include/kerberos/krb.h" 






# 45 "/usr/include/kerberos/krb.h" 
















# 67 "/usr/include/kerberos/krb.h" 








char		*krb_get_default_realm();

# 81 "/usr/include/kerberos/krb.h" 


















					





struct ktext {
	int	length;				
	unsigned char dat[1250];	
	unsigned long mbz;			
						
};

typedef struct ktext *KTEXT;
typedef struct ktext KTEXT_ST;












# 128 "/usr/include/kerberos/krb.h" 












struct auth_dat {
	unsigned char k_flags;		
	char	pname[40];	
	char	pinst[40];		
	char	prealm[40];	
	unsigned long checksum;		
	des_cblock	session;		
	int	life;			
	unsigned long time_sec;		
	unsigned long address;		
	KTEXT_ST	reply;		
};

typedef struct auth_dat AUTH_DAT;



struct credentials {
	char	service[40];	
	char	instance[40];	
	char	realm[40];	
	des_cblock	session;		
	int	lifetime;		
	int	kvno;			
	KTEXT_ST ticket_st;		
	long	issue_date;		
	char	pname[40];	
	char	pinst[40];		
};

typedef struct credentials CREDENTIALS;



struct msg_dat {
	unsigned char *app_data;	
	unsigned long app_length;	
	unsigned long hash;		
	int	swap;			
	long	time_sec;		
	unsigned char time_5ms;		
};

typedef struct msg_dat MSG_DAT;



# 189 "/usr/include/kerberos/krb.h" 




































































































































































char *tkt_string();


# 376 "/usr/include/kerberos/krb.h" 







						



# 389 "/usr/include/kerberos/krb.h" 


# 393 "/usr/include/kerberos/krb.h" 



# 13 "/usr/include/rpc/auth_kerb.h" 2
# 1 "/usr/include/sys/socket.h" 1










#ident	"@(#)socket.h	1.15	95/02/24 SMI"	

























# 1 "/usr/include/sys/netconfig.h" 1







# 90 "/usr/include/sys/netconfig.h" 
# 119 "/usr/include/sys/netconfig.h" 

# 38 "/usr/include/sys/socket.h" 2


# 42 "/usr/include/sys/socket.h" 






# 53 "/usr/include/sys/socket.h" 














































struct	linger {
	int	l_onoff;		
	int	l_linger;		
};







































struct sockaddr {
	u_short	sa_family;		
	char	sa_data[14];		
};





struct sockproto {
	u_short	sp_family;		
	u_short	sp_protocol;		
};







































struct msghdr {
	caddr_t	msg_name;		
	int	msg_namelen;		
	struct	iovec *msg_iov;		
	int	msg_iovlen;		
	caddr_t	msg_accrights;		
	int	msg_accrightslen;
};














struct opthdr {
	long	level;		
	long	name;		
	long	len;		
};








struct optdefault {
	int	optname;	
	char	*val;		
	int	len;		
};





struct opproc {
	int	level;		
	int	(*func)();	
};




struct socksysreq {
	int	args[7];
};






struct socknewproto {
	int	family;	
	int	type;	
	int	proto;	
	dev_t	dev;	
	int	flags;	
};




# 269 "/usr/include/sys/socket.h" 
























# 311 "/usr/include/sys/socket.h" 

extern int accept();
extern int bind();
extern int connect();
extern int getpeername();
extern int getsockname();
extern int getsockopt();
extern int listen();
extern int recv();
extern int recvfrom();
extern int send();
extern int sendto();
extern int setsockopt();
extern int socket();
extern int recvmsg();
extern int sendmsg();
extern int shutdown();
extern int socketpair();



# 334 "/usr/include/sys/socket.h" 



# 14 "/usr/include/rpc/auth_kerb.h" 2
# 1 "/usr/include/netinet/in.h" 1




















#ident	"@(#)in.h	1.4	93/07/06 SMI"


# 26 "/usr/include/netinet/in.h" 


# 1 "/usr/include/sys/stream.h" 1










#ident	"@(#)stream.h	1.56	94/09/28 SMI"	




# 1 "/usr/include/sys/vnode.h" 1

































#ident	"@(#)vnode.h	1.53	95/08/29 SMI"	

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 37 "/usr/include/sys/vnode.h" 2
# 1 "/usr/include/sys/t_lock.h" 1













#ident	"@(#)t_lock.h	1.42	94/11/02 SMI"


# 1 "/usr/include/sys/machlock.h" 1







#ident	"@(#)machlock.h	1.14	94/10/20 SMI"

# 12 "/usr/include/sys/machlock.h" 




# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 17 "/usr/include/sys/machlock.h" 2

# 27 "/usr/include/sys/machlock.h" 

extern void	lock_set();		
extern int	lock_try();		
extern int	ulock_try();		
extern void	lock_clear();		
extern void	ulock_clear();		

extern int	lock_set_spl();		
extern void	lock_clear_splx();	







typedef	lock_t	disp_lock_t;		


















extern	int	hres_lock;
extern	int	clock_res;



















# 85 "/usr/include/sys/machlock.h" 



# 18 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/dki_lkinfo.h" 1







#ident	"@(#)dki_lkinfo.h	1.8	93/05/03 SMI"

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 11 "/usr/include/sys/dki_lkinfo.h" 2
# 1 "/usr/include/sys/dl.h" 1










#ident	"@(#)dl.h	1.13	93/08/18 SMI"	

# 1 "/usr/include/sys/isa_defs.h" 1




# 130 "/usr/include/sys/isa_defs.h" 
# 135 "/usr/include/sys/isa_defs.h" 
# 215 "/usr/include/sys/isa_defs.h" 
# 259 "/usr/include/sys/isa_defs.h" 

# 14 "/usr/include/sys/dl.h" 2

# 17 "/usr/include/sys/dl.h" 


typedef	struct dl {
# 23 "/usr/include/sys/dl.h" 

	long	dl_hop;
	ulong_t	dl_lop;

} dl_t;

# 37 "/usr/include/sys/dl.h" 

extern dl_t	ladd();
extern dl_t	lsub();
extern dl_t	lmul();
extern dl_t	ldivide();
extern dl_t	lshiftl();
extern dl_t	llog10();
extern dl_t	lexp10();


extern dl_t	lzero;
extern dl_t	lone;
extern dl_t	lten;

# 53 "/usr/include/sys/dl.h" 



# 12 "/usr/include/sys/dki_lkinfo.h" 2

# 15 "/usr/include/sys/dki_lkinfo.h" 






typedef struct lkinfo {
	char	*lk_name;	
	int	lk_flags;	
	long	lk_pad[2];	
} lkinfo_t;

typedef struct _lkstat_t {
	lkinfo_t	*ls_infop;	
	ulong_t		ls_wrcnt;	
	ulong_t		ls_rdcnt;	
	ulong_t		ls_solordcnt;	
	ulong_t		ls_fail;	
	
	union {
		dl_t lsu_time;		
		struct _lkstat_t *lsu_next; 
	} un;

	dl_t		ls_wtime;	
	dl_t		ls_htime;	
} lkstat_t;

typedef struct lkstat_sum {
	lkstat_t	*sp;
	struct lkstat_sum *next;
} lkstat_sum_t;









typedef struct lksblk {
	struct lksblk *lsb_prev, *lsb_next;
	int lsb_nfree;				
	lkstat_t *lsb_free;			
	lkstat_t lsb_bufs[91];		
} lksblk_t;

# 69 "/usr/include/sys/dki_lkinfo.h" 


# 73 "/usr/include/sys/dki_lkinfo.h" 



# 19 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/sleepq.h" 1







#ident	"@(#)sleepq.h	1.17	94/07/29 SMI"

# 1 "/usr/include/sys/machlock.h" 1




# 87 "/usr/include/sys/machlock.h" 

# 11 "/usr/include/sys/sleepq.h" 2

# 14 "/usr/include/sys/sleepq.h" 








typedef struct sleepq {
	struct _kthread * sq_first;
} sleepq_t;




typedef struct _sleepq_head {
	sleepq_t	sq_queue;
	disp_lock_t	sq_lock;
} sleepq_head_t;

# 85 "/usr/include/sys/sleepq.h" 


# 89 "/usr/include/sys/sleepq.h" 




# 20 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/turnstile.h" 1







#ident	"@(#)turnstile.h	1.27	94/10/27 SMI"

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 11 "/usr/include/sys/turnstile.h" 2
# 1 "/usr/ucbinclude/sys/param.h" 1







#ident	"@(#)param.h	1.3	95/11/05 SMI"	





















 












# 1 "/usr/include/limits.h" 1










#ident	"@(#)limits.h	1.29	96/01/11 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 14 "/usr/include/limits.h" 2
# 1 "/usr/include/sys/isa_defs.h" 1




# 130 "/usr/include/sys/isa_defs.h" 
# 135 "/usr/include/sys/isa_defs.h" 
# 215 "/usr/include/sys/isa_defs.h" 
# 259 "/usr/include/sys/isa_defs.h" 

# 15 "/usr/include/limits.h" 2

# 18 "/usr/include/limits.h" 















# 38 "/usr/include/limits.h" 












# 51 "/usr/include/limits.h" 




























# 81 "/usr/include/limits.h" 






























































# 145 "/usr/include/limits.h" 






				












						

						






# 174 "/usr/include/limits.h" 







































extern long _sysconf(int);	



















# 237 "/usr/include/limits.h" 



# 242 "/usr/include/limits.h" 



# 44 "/usr/ucbinclude/sys/param.h" 2
# 1 "/usr/ucbinclude/unistd.h" 1







#ident	"@(#)unistd.h	1.4	92/12/15 SMI"	





















 



  
# 1 "/usr/ucbinclude/sys/fcntl.h" 1







#ident	"@(#)fcntl.h	1.1	90/04/27 SMI"	





















 

# 69 "/usr/ucbinclude/sys/fcntl.h" 
# 75 "/usr/ucbinclude/sys/fcntl.h" 
# 131 "/usr/ucbinclude/sys/fcntl.h" 
# 154 "/usr/ucbinclude/sys/fcntl.h" 
# 197 "/usr/ucbinclude/sys/fcntl.h" 

# 36 "/usr/ucbinclude/unistd.h" 2












































































# 45 "/usr/ucbinclude/sys/param.h" 2

# 48 "/usr/ucbinclude/sys/param.h" 


# 52 "/usr/ucbinclude/sys/param.h" 





















				
















































































# 156 "/usr/ucbinclude/sys/param.h" 


# 160 "/usr/ucbinclude/sys/param.h" 

























































# 1 "/usr/ucbinclude/sys/signal.h" 1







#ident	"@(#)signal.h	1.10	94/05/26 SMI" 





















 









































# 74 "/usr/ucbinclude/sys/signal.h" 







































# 124 "/usr/ucbinclude/sys/signal.h" 
# 131 "/usr/ucbinclude/sys/signal.h" 





















typedef struct {		
	unsigned long	__sigbits[4];
} sigset_t;

typedef	struct {
	unsigned long	__sigbits[2];
} k_sigset_t;

struct sigaction {
	int sa_flags;
	void (*sa_handler)();
	sigset_t sa_mask;
	int sa_resv[2];
};






			




























struct sigaltstack {
	char	*ss_sp;
	int	ss_size;
	int	ss_flags;
};

typedef struct sigaltstack stack_t;




# 214 "/usr/ucbinclude/sys/signal.h" 







# 223 "/usr/ucbinclude/sys/signal.h" 


typedef int	sig_atomic_t;

# 238 "/usr/ucbinclude/sys/signal.h" 
# 251 "/usr/ucbinclude/sys/signal.h" 
# 265 "/usr/ucbinclude/sys/signal.h" 


extern char *_sys_siglist[];
extern int _sys_nsig;

extern	void(*signal())();
extern  void(*sigset())();



# 277 "/usr/ucbinclude/sys/signal.h" 











struct  sigstack {
        char    *ss_sp;                 
        int     ss_onstack;             
};




struct  sigvec {
        void    (*sv_handler)();        
        int     sv_mask;                
        int     sv_flags;               
};







struct  sigcontext {
	int	sc_onstack;		
	int	sc_mask;		
# 317 "/usr/ucbinclude/sys/signal.h" 

# 324 "/usr/ucbinclude/sys/signal.h" 

# 329 "/usr/ucbinclude/sys/signal.h" 



	int	sc_sp;			
	int	sc_pc;			
	int	sc_npc;			
	int	sc_psr;			
	int	sc_g1;			
	int	sc_o0;
	int	sc_wbcnt;		
	char	*sc_spbuf[31];	
	int	sc_wbuf[31][16];	

# 348 "/usr/ucbinclude/sys/signal.h" 

# 355 "/usr/ucbinclude/sys/signal.h" 

};




# 365 "/usr/ucbinclude/sys/signal.h" 

# 384 "/usr/ucbinclude/sys/signal.h" 








# 394 "/usr/ucbinclude/sys/signal.h" 

# 397 "/usr/ucbinclude/sys/signal.h" 


# 402 "/usr/ucbinclude/sys/signal.h" 





# 418 "/usr/ucbinclude/sys/signal.h" 

# 434 "/usr/ucbinclude/sys/signal.h" 
















# 1 "/usr/include/vm/faultcode.h" 1

































#ident	"@(#)faultcode.h	1.15	92/07/14 SMI"	

# 38 "/usr/include/vm/faultcode.h" 






















typedef	int	faultcode_t;	


# 65 "/usr/include/vm/faultcode.h" 



# 451 "/usr/ucbinclude/sys/signal.h" 2



# 456 "/usr/ucbinclude/sys/signal.h" 



























# 485 "/usr/ucbinclude/sys/signal.h" 





# 218 "/usr/ucbinclude/sys/param.h" 2

 
# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 221 "/usr/ucbinclude/sys/param.h" 2








 









 









 









# 12 "/usr/include/sys/turnstile.h" 2
# 1 "/usr/include/sys/pirec.h" 1







#ident	"@(#)pirec.h	1.11	93/12/20 SMI"

# 12 "/usr/include/sys/pirec.h" 




















typedef struct pirec
{
	struct pirec	*pi_forw;	
	struct pirec	*pi_back;	
	struct _kthread *pi_benef;	
	uint_t		pi_epri_hi;	
} pirec_t;

# 66 "/usr/include/sys/pirec.h" 


# 70 "/usr/include/sys/pirec.h" 



# 13 "/usr/include/sys/turnstile.h" 2
# 1 "/usr/include/sys/sleepq.h" 1




# 92 "/usr/include/sys/sleepq.h" 

# 14 "/usr/include/sys/turnstile.h" 2

# 17 "/usr/include/sys/turnstile.h" 









typedef enum {
	QOBJ_UND	= -1,	
	QOBJ_DEF	= 0,	
	QOBJ_READER	= 0,	
	QOBJ_WRITER	= 1,	
	QOBJ_CV		= 0,	
	QOBJ_MUTEX	= 0,	
	QOBJ_SEMA	= 0	
} qobj_t;





typedef struct turnstile	turnstile_t;

typedef ushort_t		turnstile_id_t;

struct turnstile {
	union tstile_un {
		



		turnstile_t	*ts_forw;

		




		pirec_t		ts_prioinv;
	} tsun;

	



	sleepq_t	ts_sleepq[2];
	





	turnstile_id_t	ts_id;
	uchar_t		ts_flags;
	disp_lock_t	ts_wlock;	

	




	void		*ts_sobj_priv_data;	
};

# 181 "/usr/include/sys/turnstile.h" 


# 185 "/usr/include/sys/turnstile.h" 



# 21 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/mutex.h" 1







#ident	"@(#)mutex.h	1.14	94/07/29 SMI"


# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 12 "/usr/include/sys/mutex.h" 2
# 1 "/usr/include/sys/dki_lkinfo.h" 1




# 75 "/usr/include/sys/dki_lkinfo.h" 

# 13 "/usr/include/sys/mutex.h" 2


# 17 "/usr/include/sys/mutex.h" 




























typedef enum {
	MUTEX_ADAPTIVE = 0,	
	MUTEX_SPIN,		
	MUTEX_ADAPTIVE_STAT,	
	MUTEX_SPIN_STAT,	
	MUTEX_DRIVER_NOSTAT = 4, 
	MUTEX_DRIVER_STAT = 5,	
	MUTEX_ADAPTIVE_DEF	
} kmutex_type_t;

# 59 "/usr/include/sys/mutex.h" 















typedef struct mutex {
	void	*_opaque[2];
} kmutex_t;


# 100 "/usr/include/sys/mutex.h" 




# 106 "/usr/include/sys/mutex.h" 



# 22 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/rwlock.h" 1















#ident	"@(#)rwlock.h	1.3	94/07/29 SMI"

# 20 "/usr/include/sys/rwlock.h" 




typedef enum {
	RW_SLEEP,			
	RW_SLEEP_STAT,			
	RW_DRIVER_NOSTAT = 2,		
	RW_DRIVER_STAT = 3,		
	RW_DEFAULT			
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;

# 39 "/usr/include/sys/rwlock.h" 




typedef struct _krwlock {
	void	*_opaque[3];
} krwlock_t;


# 70 "/usr/include/sys/rwlock.h" 




# 76 "/usr/include/sys/rwlock.h" 



# 23 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/semaphore.h" 1
















#ident	"@(#)semaphore.h	1.4	94/07/29 SMI"


# 22 "/usr/include/sys/semaphore.h" 




# 28 "/usr/include/sys/semaphore.h" 







typedef enum {
	SEMA_DEFAULT,
	SEMA_DRIVER
} ksema_type_t;

typedef struct _ksema {
	void	* _opaque[2];	
} ksema_t;

# 58 "/usr/include/sys/semaphore.h" 



# 63 "/usr/include/sys/semaphore.h" 



# 24 "/usr/include/sys/t_lock.h" 2
# 1 "/usr/include/sys/condvar.h" 1















#ident	"@(#)condvar.h	1.6	94/07/29 SMI"


# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 20 "/usr/include/sys/condvar.h" 2
# 22 "/usr/include/sys/condvar.h" 



# 27 "/usr/include/sys/condvar.h" 








typedef struct _kcondvar {
	ushort_t	_opaque;
} kcondvar_t;

typedef	enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;


# 63 "/usr/include/sys/condvar.h" 




# 69 "/usr/include/sys/condvar.h" 



# 25 "/usr/include/sys/t_lock.h" 2


# 29 "/usr/include/sys/t_lock.h" 





















# 89 "/usr/include/sys/t_lock.h" 




# 95 "/usr/include/sys/t_lock.h" 



# 38 "/usr/include/sys/vnode.h" 2
# 1 "/usr/include/sys/time.h" 1


















# 34 "/usr/include/sys/time.h" 
# 67 "/usr/include/sys/time.h" 
# 124 "/usr/include/sys/time.h" 
# 136 "/usr/include/sys/time.h" 
# 154 "/usr/include/sys/time.h" 
# 178 "/usr/include/sys/time.h" 
# 186 "/usr/include/sys/time.h" 
# 237 "/usr/include/sys/time.h" 
# 239 "/usr/include/sys/time.h" 
# 258 "/usr/include/sys/time.h" 
# 278 "/usr/include/sys/time.h" 
# 290 "/usr/include/sys/time.h" 

# 39 "/usr/include/sys/vnode.h" 2
# 1 "/usr/include/sys/cred.h" 1






























# 96 "/usr/include/sys/cred.h" 

# 40 "/usr/include/sys/vnode.h" 2
# 1 "/usr/include/sys/uio.h" 1










#ident	"@(#)uio.h	1.21	94/04/22 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 14 "/usr/include/sys/uio.h" 2

# 17 "/usr/include/sys/uio.h" 


# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 20 "/usr/include/sys/uio.h" 2








typedef struct iovec {
	caddr_t	iov_base;
	int	iov_len;
} iovec_t;




typedef enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_USERISPACE } uio_seg_t;

typedef struct uio {
	iovec_t	*uio_iov;	
	int	uio_iovcnt;	
	lloff_t	_uio_offset;	
	uio_seg_t uio_segflg;	
	short	uio_fmode;	
	lldaddr_t _uio_limit;	
	int	uio_resid;	
} uio_t;










typedef enum uio_rw { UIO_READ, UIO_WRITE } uio_rw_t;

# 83 "/usr/include/sys/uio.h" 



# 89 "/usr/include/sys/uio.h" 

extern ssize_t readv();
extern ssize_t writev();





# 99 "/usr/include/sys/uio.h" 



# 41 "/usr/include/sys/vnode.h" 2
# 1 "/usr/include/vm/seg_enum.h" 1

































#ident	"@(#)seg_enum.h	1.1	93/04/03 SMI"

# 38 "/usr/include/vm/seg_enum.h" 












enum fault_type {
	F_INVAL,		
	F_PROT,			
	F_SOFTLOCK,		
	F_SOFTUNLOCK		
};




enum seg_rw {
	S_OTHER,		
	S_READ,			
	S_WRITE,		
	S_EXEC,			
	S_CREATE		
};

# 70 "/usr/include/vm/seg_enum.h" 



# 42 "/usr/include/sys/vnode.h" 2
# 44 "/usr/include/sys/vnode.h" 


# 48 "/usr/include/sys/vnode.h" 












typedef enum vtype {
	VNON	= 0,
	VREG	= 1,
	VDIR	= 2,
	VBLK	= 3,
	VCHR	= 4,
	VLNK	= 5,
	VFIFO	= 6,
	VDOOR	= 7,
	VBAD	= 8
} vtype_t;









typedef struct vnode {
	kmutex_t	v_lock;			
	u_short		v_flag;			
	u_long		v_count;		
	struct vfs	*v_vfsmountedhere;	
	struct vnodeops	*v_op;			
	struct vfs	*v_vfsp;		
	struct stdata	*v_stream;		
	struct page	*v_pages;		
	enum vtype	v_type;			
	dev_t		v_rdev;			
	caddr_t		v_data;			
	struct filock	*v_filocks;		
	kcondvar_t	v_cv;			
} vnode_t;































typedef struct vattr {
	long		va_mask;	
	vtype_t		va_type;	
	mode_t		va_mode;	
	uid_t		va_uid;		
	gid_t		va_gid;		
	dev_t		va_fsid;	
	ino_t		va_nodeid;	
	nlink_t		va_nlink;	
	u_long		va_size0;	
	u_long		va_size;	
	timestruc_t	va_atime;	
	timestruc_t	va_mtime;	
	timestruc_t	va_ctime;	
	dev_t		va_rdev;	
	u_long		va_blksize;	
	u_long		va_nblocks;	
	u_long		va_vcode;	
} vattr_t;



























































enum rm		{ RMFILE, RMDIRECTORY };	
enum symfollow	{ NO_FOLLOW, FOLLOW };		
enum vcexcl	{ NONEXCL, EXCL };		
enum create	{ CRCREAT, CRMKNOD, CRMKDIR, CRCORE }; 

typedef enum rm		rm_t;
typedef enum symfollow	symfollow_t;
typedef enum vcexcl	vcexcl_t;
typedef enum create	create_t;





typedef struct vsecattr {
	u_long		vsa_mask;	
	int		vsa_aclcnt;	
	void		*vsa_aclentp;	
	int		vsa_dfaclcnt;	
	void		*vsa_dfaclentp;	
} vsecattr_t;










struct pathname;
struct fid;
struct flock;
struct page;
struct seg;
struct as;
struct pollhead;












typedef struct vnodeops {
	int	(*vop_open)(struct vnode **, int, struct cred *);
	int	(*vop_close)(struct vnode *, int, int, offset_t, struct cred *);
	int	(*vop_read)(struct vnode *, struct uio *, int, struct cred *);
	int	(*vop_write)(struct vnode *, struct uio *, int, struct cred *);
	int	(*vop_ioctl)(struct vnode *, int, int, int, struct cred *,
			int *);
	int	(*vop_setfl)(struct vnode *, int, int, struct cred *);
	int	(*vop_getattr)(struct vnode *, struct vattr *, int,
			struct cred *);
	int	(*vop_setattr)(struct vnode *, struct vattr *, int,
			struct cred *);
	int	(*vop_access)(struct vnode *, int, int, struct cred *);
	int	(*vop_lookup)(struct vnode *, char *, struct vnode **,
			struct pathname *, int, struct vnode *, struct cred *);
	int	(*vop_create)(struct vnode *, char *, struct vattr *,
			vcexcl_t, int, struct vnode **, struct cred *);
	int	(*vop_remove)(struct vnode *, char *, struct cred *);
	int	(*vop_link)(struct vnode *, struct vnode *, char *,
			struct cred *);
	int	(*vop_rename)(struct vnode *, char *, struct vnode *, char *,
			struct cred *);
	int	(*vop_mkdir)(struct vnode *, char *, struct vattr *,
			struct vnode **, struct cred *);
	int	(*vop_rmdir)(struct vnode *, char *, struct vnode *,
			struct cred *);
	int	(*vop_readdir)(struct vnode *, struct uio *, struct cred *,
			int *);
	int	(*vop_symlink)(struct vnode *, char *, struct vattr *, char *,
			struct cred *);
	int	(*vop_readlink)(struct vnode *, struct uio *, struct cred *);
	int	(*vop_fsync)(struct vnode *, int, struct cred *);
	void	(*vop_inactive)(struct vnode *, struct cred *);
	int	(*vop_fid)(struct vnode *, struct fid *);
	void	(*vop_rwlock)(struct vnode *, int);
	void	(*vop_rwunlock)(struct vnode *, int);
	int	(*vop_seek)(struct vnode *, offset_t, offset_t *);
	int	(*vop_cmp)(struct vnode *, struct vnode *);
	int	(*vop_frlock)(struct vnode *, int, struct flock *, int,
			offset_t, struct cred *);
	int	(*vop_space)(struct vnode *, int, struct flock *, int,
			offset_t, struct cred *);
	int	(*vop_realvp)(struct vnode *, struct vnode **);
	int	(*vop_getpage)(struct vnode *, offset_t, u_int, u_int *,
			struct page **, u_int, struct seg *, caddr_t,
			enum seg_rw, struct cred *);
	int	(*vop_putpage)(struct vnode *, offset_t, u_int, int,
			struct cred *);
	int	(*vop_map)(struct vnode *, offset_t, struct as *, caddr_t *,
			u_int, u_char, u_char, u_int, struct cred *);
	int	(*vop_addmap)(struct vnode *, offset_t, struct as *, caddr_t,
			u_int, u_char, u_char, u_int, struct cred *);
	int	(*vop_delmap)(struct vnode *, offset_t, struct as *, caddr_t,
			u_int, u_int, u_int, u_int, struct cred *);
	int	(*vop_poll)(struct vnode *, short, int, short *,
			struct pollhead **);
	int	(*vop_dump)(struct vnode *, caddr_t, int, int);
	int	(*vop_pathconf)(struct vnode *, int, u_long *, struct cred *);
	int	(*vop_pageio)(struct vnode *, struct page *, u_int, u_int, int,
			struct cred *);
	int	(*vop_dumpctl)(struct vnode *, int);
	void	(*vop_dispose)(struct vnode *, struct page *, int, int,
			struct cred *);
	int	(*vop_setsecattr)(struct vnode *, vsecattr_t *, int,
			struct cred *);
	int	(*vop_getsecattr)(struct vnode *, vsecattr_t *, int,
			struct cred *);
} vnodeops_t;











































































# 415 "/usr/include/sys/vnode.h" 














































# 476 "/usr/include/sys/vnode.h" 















# 493 "/usr/include/sys/vnode.h" 



# 17 "/usr/include/sys/stream.h" 2
# 1 "/usr/include/sys/poll.h" 1










#ident	"@(#)poll.h	1.19	94/08/31 SMI"	

# 15 "/usr/include/sys/poll.h" 






typedef struct pollfd {
	int fd;				
	short events;			
	short revents;			
} pollfd_t;






















# 96 "/usr/include/sys/poll.h" 
# 125 "/usr/include/sys/poll.h" 


# 129 "/usr/include/sys/poll.h" 


# 133 "/usr/include/sys/poll.h" 



# 18 "/usr/include/sys/stream.h" 2
# 1 "/usr/include/sys/strmdep.h" 1










#ident	"@(#)strmdep.h	1.8	92/07/14 SMI"	

# 15 "/usr/include/sys/strmdep.h" 

























# 42 "/usr/include/sys/strmdep.h" 



# 19 "/usr/include/sys/stream.h" 2
# 1 "/usr/include/sys/cred.h" 1






























# 96 "/usr/include/sys/cred.h" 

# 20 "/usr/include/sys/stream.h" 2
# 1 "/usr/include/sys/t_lock.h" 1










# 49 "/usr/include/sys/t_lock.h" 
# 97 "/usr/include/sys/t_lock.h" 

# 21 "/usr/include/sys/stream.h" 2

# 24 "/usr/include/sys/stream.h" 


































typedef struct	queue {
	struct	qinit	*q_qinfo;	
	struct	msgb	*q_first;	
	struct	msgb	*q_last;	
	struct	queue	*q_next;	
	struct	queue	*q_link;	
	void		*q_ptr;		
	ulong		q_count;	
	ulong		q_flag;		
	long		q_minpsz;	
					
	long		q_maxpsz;	
					
	ulong		q_hiwat;	
	ulong		q_lowat;	
	struct qband	*q_bandp;	
	kmutex_t	q_lock;		
	struct stdata 	*q_stream;	
	struct	syncq	*q_syncq;	
	unsigned char	q_nband;	
	kcondvar_t	q_wait;		
	kcondvar_t	q_sync;		
	struct	queue	*q_nfsrv;	
	struct	queue	*q_nbsrv;	
	ushort		q_draining;	
	short		q_struiot;	
} queue_t;





















					











typedef struct qband {
	struct qband	*qb_next;	
	ulong		qb_count;	
	struct msgb	*qb_first;	
	struct msgb	*qb_last;	
	ulong		qb_hiwat;	
	ulong		qb_lowat;	
	ulong		qb_flag;	
} qband_t;
















typedef enum qfields {
	QHIWAT	= 0,		
	QLOWAT	= 1,		
	QMAXPSZ	= 2,		
	QMINPSZ	= 3,		
	QCOUNT	= 4,		
	QFIRST	= 5,		
	QLAST	= 6,		
	QFLAG	= 7,		
	QSTRUIOT = 8,		
	QBAD	= 9
} qfields_t;




struct module_info {
	ushort	mi_idnum;		
	char 	*mi_idname;		
	long	mi_minpsz;		
	long	mi_maxpsz;		
	ulong	mi_hiwat;		
	ulong 	mi_lowat;		
};




struct	qinit {
	int	(*qi_putp)();		
	int	(*qi_srvp)();		
	int	(*qi_qopen)();		
	int	(*qi_qclose)();		
	int	(*qi_qadmin)();		
	struct module_info *qi_minfo;	
	struct module_stat *qi_mstat;	
	int	(*qi_rwp)();		
	int	(*qi_infop)();		
	int	qi_struiot;		
};













struct streamtab {
	struct qinit *st_rdinit;
	struct qinit *st_wrinit;
	struct qinit *st_muxrinit;
	struct qinit *st_muxwinit;
};





struct linkblk {
	queue_t *l_qtop;	
				
	queue_t *l_qbot;	
	int	l_index;	
};




typedef struct free_rtn {
	void (*free_func)();
	char *free_arg;
	struct	free_rtn	*free_next;
	int free_flags;
} frtn_t;














typedef struct datab {
	struct free_rtn	*db_frtnp;
	unsigned char	*db_base;
	unsigned char	*db_lim;
	unsigned char	db_ref;
	unsigned char	db_type;
	unsigned char	db_refmin;
	unsigned char	db_struioflag;
	void		*db_cache;	
	unsigned char	*db_struiobase;
	unsigned char	*db_struiolim;
	unsigned char	*db_struioptr;
	union {
		unsigned char data[8];
		


	} db_struioun;
} dblk_t;




typedef struct	msgb {
	struct	msgb	*b_next;
	struct  msgb	*b_prev;
	struct	msgb	*b_cont;
	unsigned char	*b_rptr;
	unsigned char	*b_wptr;
	struct datab 	*b_datap;
	unsigned char	b_band;
	unsigned short	b_flag;
	queue_t		*b_queue;	
} mblk_t;














					

























# 313 "/usr/include/sys/stream.h" 



































struct iocblk {
	int 	ioc_cmd;		
	cred_t	*ioc_cr;		
	uint	ioc_id;			
	uint	ioc_count;		
	int	ioc_error;		
	int	ioc_rval;		
	long	ioc_filler[4];		
};








struct copyreq {
	int	cq_cmd;			
	cred_t	*cq_cr;			
	uint	cq_id;			
	caddr_t	cq_addr;		
	uint	cq_size;		
	int	cq_flag;		
	mblk_t *cq_private;		
	long	cq_filler[4];		
};







					

					
					





struct copyresp {
	int	cp_cmd;			
	cred_t	*cp_cr;			
	uint	cp_id;			
	caddr_t	cp_rval;		
					
	uint	cp_pad1;
	int	cp_pad2;
	mblk_t *cp_private;		
	long	cp_filler[4];
};









struct stroptions {
	ulong	so_flags;		
	short	so_readopt;		
	ushort	so_wroff;		
	long	so_minpsz;		
	long	so_maxpsz;		
	ulong	so_hiwat;		
	ulong	so_lowat;		
	unsigned char so_band;		
	ushort	so_erropt;		
};






























struct str_evmsg {
	long		 sv_event;	
	vnode_t		*sv_vp;		
	long		 sv_eid;	
	long		 sv_evpri;	
	long		 sv_flags;	
	uid_t		 sv_uid;	
	pid_t		 sv_pid;	
	hostid_t	 sv_hostid;	
	long		 sv_pad[4];	
};










typedef struct struiod {
	mblk_t		*d_mp;		
	uio_t		d_uio;		
	iovec_t d_iov[16];	
} struiod_t;




typedef struct infod {
	unsigned char	d_cmd;		
	unsigned char	d_res;		
	int		d_bytes;	
	int		d_count;	
	uio_t		*d_uiop;	
} infod_t;















# 506 "/usr/include/sys/stream.h" 












































































# 584 "/usr/include/sys/stream.h" 































# 684 "/usr/include/sys/stream.h" 





extern int nstrpush;			

# 693 "/usr/include/sys/stream.h" 



# 29 "/usr/include/netinet/in.h" 2
# 1 "/usr/include/sys/byteorder.h" 1







# 43 "/usr/include/sys/byteorder.h" 
# 62 "/usr/include/sys/byteorder.h" 

# 30 "/usr/include/netinet/in.h" 2




















































































struct in_addr {
	union {
		struct { u_char s_b1, s_b2, s_b3, s_b4; } S_un_b;
		struct { u_short s_w1, s_w2; } S_un_w;
		u_long S_addr;
	} S_un;






};




















































struct sockaddr_in {
	short	sin_family;
	u_short	sin_port;
	struct	in_addr sin_addr;
	char	sin_zero[8];
};
























struct ip_mreq {
	struct in_addr	imr_multiaddr;	
	struct in_addr	imr_interface;	
};





# 228 "/usr/include/netinet/in.h" 


# 232 "/usr/include/netinet/in.h" 



# 15 "/usr/include/rpc/auth_kerb.h" 2

# 19 "/usr/include/rpc/auth_kerb.h" 



# 24 "/usr/include/rpc/auth_kerb.h" 





enum authkerb_namekind {
	AKN_FULLNAME,
	AKN_NICKNAME
};



struct authkerb_fullname {
	KTEXT_ST ticket;
	u_long window;		
};




struct authkerb_clnt_cred {
	
	unsigned char k_flags;	
	char    pname[40]; 
	char    pinst[40];	
	char    prealm[40]; 
	unsigned long checksum;	
	des_cblock session;	
	int	life;		
	unsigned long time_sec;	
	unsigned long address;	
	
	
	unsigned long expiry;	
	u_long nickname;	
	u_long window;		
};

typedef struct authkerb_clnt_cred authkerb_clnt_cred;




struct authkerb_cred {
	enum authkerb_namekind akc_namekind;
	struct authkerb_fullname akc_fullname;
	u_long akc_nickname;
};




struct authkerb_verf {
	union {
		struct timeval akv_ctime;	
		des_block akv_xtime;		
	} akv_time_u;
	u_long akv_int_u;
};


























# 123 "/usr/include/rpc/auth_kerb.h" 


# 127 "/usr/include/rpc/auth_kerb.h" 



# 39 "/usr/include/rpc/rpc.h" 2

# 1 "/usr/include/rpc/svc.h" 1












#ident	"@(#)svc.h	1.35	95/01/13 SMI"

# 1 "/usr/include/rpc/rpc_com.h" 1










# 82 "/usr/include/rpc/rpc_com.h" 

# 16 "/usr/include/rpc/svc.h" 2
# 1 "/usr/include/rpc/rpc_msg.h" 1




# 217 "/usr/include/rpc/rpc_msg.h" 

# 17 "/usr/include/rpc/svc.h" 2
# 1 "/usr/include/sys/tihdr.h" 1










#ident	"@(#)tihdr.h	1.8	92/07/14 SMI"	

# 15 "/usr/include/sys/tihdr.h" 





















































































































struct T_conn_req {
	long	PRIM_type;	
	long	DEST_length;	
	long	DEST_offset;	
	long	OPT_length;	
	long	OPT_offset;	
};



struct T_conn_res {
	long    PRIM_type;	
	queue_t *QUEUE_ptr;	
	long    OPT_length;	
	long	OPT_offset;	
	long    SEQ_number;	
};



struct T_discon_req {
	long    PRIM_type;	
	long    SEQ_number;	
};



struct T_data_req {
	long	PRIM_type;	
	long	MORE_flag;	
};



struct T_exdata_req {
	long	PRIM_type;	
	long	MORE_flag;	
};



struct T_info_req {
	long	PRIM_type;	
};



struct T_bind_req {
	long	PRIM_type;		
	long	ADDR_length;		
	long	ADDR_offset;		
	unsigned long CONIND_number;	
};



struct T_unbind_req {
	long	PRIM_type;	
};



struct T_unitdata_req {
	long	PRIM_type;	
	long	DEST_length;	
	long	DEST_offset;	
	long	OPT_length;	
	long	OPT_offset;	
};



struct T_optmgmt_req {
	long	PRIM_type;	
	long	OPT_length;	
	long	OPT_offset;	
	long    MGMT_flags;	
};



struct T_ordrel_req {
	long	PRIM_type;	
};



struct T_conn_ind {
	long	PRIM_type;	
	long	SRC_length;	
	long	SRC_offset;	
	long	OPT_length;	
	long    OPT_offset;	
	long    SEQ_number;	
};



struct T_conn_con {
	long	PRIM_type;	
	long	RES_length;	
	long	RES_offset;	
	long	OPT_length;	
	long    OPT_offset;	
};



struct T_discon_ind {
	long	PRIM_type;	
	long	DISCON_reason;	
	long    SEQ_number;	
};



struct T_data_ind {
	long 	PRIM_type;	
	long	MORE_flag;	
};



struct T_exdata_ind {
	long	PRIM_type;	
	long	MORE_flag;	
};



struct T_info_ack {
	long	PRIM_type;	
	long	TSDU_size;	
	long	ETSDU_size;	
	long	CDATA_size;	
	long	DDATA_size;	
	long	ADDR_size;	
	long	OPT_size;	
	long    TIDU_size;	
	long    SERV_type;	
	long    CURRENT_state;  
	long    PROVIDER_flag;  
};



struct T_bind_ack {
	long		PRIM_type;	
	long		ADDR_length;	
	long		ADDR_offset;	
	unsigned long	CONIND_number;	
};



struct T_error_ack {
	long 	PRIM_type;	
	long	ERROR_prim;	
	long	TLI_error;	
	long	UNIX_error;	
};



struct T_ok_ack {
	long 	PRIM_type;	
	long	CORRECT_prim;	
};



struct T_unitdata_ind {
	long	PRIM_type;	
	long	SRC_length;	
	long	SRC_offset;	
	long	OPT_length;	
	long	OPT_offset;	
};



struct T_uderror_ind {
	long	PRIM_type;	
	long	DEST_length;	
	long	DEST_offset;	
	long	OPT_length;	
	long	OPT_offset;	
	long	ERROR_type;	
};



struct T_optmgmt_ack {
	long	PRIM_type;	
	long	OPT_length;	
	long	OPT_offset;	
	long    MGMT_flags;	
};



struct T_ordrel_ind {
	long	PRIM_type;	
};




union T_primitives {
	long			type;		
	struct T_conn_req	conn_req;	
	struct T_conn_res	conn_res;	
	struct T_discon_req	discon_req;	
	struct T_data_req	data_req;	
	struct T_exdata_req	exdata_req;	
	struct T_info_req	info_req;	
	struct T_bind_req	bind_req;	
	struct T_unbind_req	unbind_req;	
	struct T_unitdata_req	unitdata_req;	
	struct T_optmgmt_req	optmgmt_req;	
	struct T_ordrel_req	ordrel_req;	
	struct T_conn_ind	conn_ind;	
	struct T_conn_con	conn_con;	
	struct T_discon_ind	discon_ind;	
	struct T_data_ind	data_ind;	
	struct T_exdata_ind	exdata_ind;	
	struct T_info_ack	info_ack;	
	struct T_bind_ack	bind_ack;	
	struct T_error_ack	error_ack;	
	struct T_ok_ack		ok_ack;		
	struct T_unitdata_ind	unitdata_ind;	
	struct T_uderror_ind	uderror_ind;	
	struct T_optmgmt_ack	optmgmt_ack;	
	struct T_ordrel_ind	ordrel_ind;	
};


# 371 "/usr/include/sys/tihdr.h" 



# 18 "/usr/include/rpc/svc.h" 2

# 21 "/usr/include/rpc/svc.h" 
























# 47 "/usr/include/rpc/svc.h" 







# 56 "/usr/include/rpc/svc.h" 


enum xprt_stat {
	XPRT_DIED,
	XPRT_MOREREQS,
	XPRT_IDLE
};




struct svc_req {
	u_long		rq_prog;	
	u_long		rq_vers;	
	u_long		rq_proc;	
	struct opaque_auth rq_cred;	
	caddr_t		rq_clntcred;	
	struct __svcxprt *rq_xprt;	
};

# 97 "/usr/include/rpc/svc.h" 


struct xp_ops {
# 137 "/usr/include/rpc/svc.h" 

		bool_t	(*xp_recv)(); 
		enum xprt_stat (*xp_stat)(); 
		bool_t	(*xp_getargs)(); 
		bool_t	(*xp_reply)(); 
		bool_t	(*xp_freeargs)(); 
		void	(*xp_destroy)(); 
# 151 "/usr/include/rpc/svc.h" 

		bool_t	(*xp_control)(); 


};




























typedef struct __svcxprt {
# 228 "/usr/include/rpc/svc.h" 

	int		xp_fd;

	u_short		xp_port;
	





	struct	xp_ops	*xp_ops;
	int		xp_addrlen;	 
	char		*xp_tp;		 
	char		*xp_netid;	 
	struct netbuf	xp_ltaddr;	 
	struct netbuf	xp_rtaddr;	 
	char		xp_raddr[16];	 
	struct opaque_auth xp_verf;	 
	caddr_t		xp_p1;		 
	caddr_t		xp_p2;		 
	caddr_t		xp_p3;		 
	int		xp_type;	

} SVCXPRT;

# 265 "/usr/include/rpc/svc.h" 






# 273 "/usr/include/rpc/svc.h" 


















# 296 "/usr/include/rpc/svc.h" 










































# 344 "/usr/include/rpc/svc.h" 





# 369 "/usr/include/rpc/svc.h" 



# 377 "/usr/include/rpc/svc.h" 

extern bool_t	rpc_reg();














# 398 "/usr/include/rpc/svc.h" 

extern bool_t	svc_reg();











# 413 "/usr/include/rpc/svc.h" 

extern void	svc_unreg();










# 427 "/usr/include/rpc/svc.h" 

extern void	xprt_register();










# 441 "/usr/include/rpc/svc.h" 

extern void	xprt_unregister();





























# 481 "/usr/include/rpc/svc.h" 

extern bool_t	svc_sendreply();
extern void	svcerr_decode();
extern void	svcerr_weakauth();
extern void	svcerr_noproc();
extern void	svcerr_progvers();
extern void	svcerr_auth();
extern void	svcerr_noprog();
extern void	svcerr_systemerr();


















extern fd_set svc_fdset;







# 523 "/usr/include/rpc/svc.h" 


extern void	rpctest_service();
extern void	svc_getreqset();











# 547 "/usr/include/rpc/svc.h" 

extern int svc_create();








# 566 "/usr/include/rpc/svc.h" 

extern SVCXPRT	*svc_tp_create();





# 583 "/usr/include/rpc/svc.h" 

extern SVCXPRT *svc_tli_create();






# 605 "/usr/include/rpc/svc.h" 

extern SVCXPRT	*svc_vc_create();
extern SVCXPRT	*svc_dg_create();






# 621 "/usr/include/rpc/svc.h" 

extern SVCXPRT *svc_fd_create();





# 630 "/usr/include/rpc/svc.h" 

extern SVCXPRT *svc_raw_create();





# 639 "/usr/include/rpc/svc.h" 

int svc_dg_enablecache();


# 646 "/usr/include/rpc/svc.h" 


# 667 "/usr/include/rpc/svc.h" 














# 683 "/usr/include/rpc/svc.h" 

void svc_done();




# 691 "/usr/include/rpc/svc.h" 



# 41 "/usr/include/rpc/rpc.h" 2
# 1 "/usr/include/rpc/svc_auth.h" 1







#ident	"@(#)svc_auth.h	1.10	93/11/12 SMI"







# 1 "/usr/include/rpc/svc.h" 1









# 693 "/usr/include/rpc/svc.h" 

# 17 "/usr/include/rpc/svc_auth.h" 2

# 20 "/usr/include/rpc/svc_auth.h" 





# 27 "/usr/include/rpc/svc_auth.h" 

extern enum auth_stat __authenticate();


# 33 "/usr/include/rpc/svc_auth.h" 



# 42 "/usr/include/rpc/rpc.h" 2


# 1 "/usr/include/rpc/rpcb_clnt.h" 1


























#ident	"@(#)rpcb_clnt.h	1.11	93/07/16 SMI"


# 1 "/usr/include/rpc/types.h" 1




# 93 "/usr/include/rpc/types.h" 

# 31 "/usr/include/rpc/rpcb_clnt.h" 2
# 1 "/usr/include/rpc/rpcb_prot.h" 1








# 1 "/usr/include/rpc/rpc.h" 1










# 47 "/usr/include/rpc/rpc.h" 

# 10 "/usr/include/rpc/rpcb_prot.h" 2





#ident	"@(#)rpcb_prot.x	1.6	95/01/13 SMI"


















































































struct rpcb {
	u_long r_prog;
	u_long r_vers;
	char *r_netid;
	char *r_addr;
	char *r_owner;
};
typedef struct rpcb rpcb;

typedef rpcb RPCB;













struct rp__list {
	rpcb rpcb_map;
	struct rp__list *rpcb_next;
};
typedef struct rp__list rp__list;

typedef rp__list *rpcblist_ptr;

typedef struct rp__list rpcblist;
typedef struct rp__list RPCBLIST;


struct rpcblist {
	RPCB rpcb_map;
	struct rpcblist *rpcb_next;
};


# 141 "/usr/include/rpc/rpcb_prot.h" 

# 144 "/usr/include/rpc/rpcb_prot.h" 

bool_t xdr_rpcblist();

# 149 "/usr/include/rpc/rpcb_prot.h" 







struct rpcb_rmtcallargs {
	u_long prog;
	u_long vers;
	u_long proc;
	struct {
		u_int args_len;
		char *args_val;
	} args;
};
typedef struct rpcb_rmtcallargs rpcb_rmtcallargs;












struct r_rpcb_rmtcallargs {
	u_long prog;
	u_long vers;
	u_long proc;
	struct {
		u_int args_len;
		char *args_val;
	} args;
	xdrproc_t	xdr_args;	
};






struct rpcb_rmtcallres {
	char *addr;
	struct {
		u_int results_len;
		char *results_val;
	} results;
};
typedef struct rpcb_rmtcallres rpcb_rmtcallres;




struct r_rpcb_rmtcallres {
	char *addr;
	struct {
		u_int results_len;
		char *results_val;
	} results;
	xdrproc_t	xdr_res;	
};








struct rpcb_entry {
	char *r_maddr;
	char *r_nc_netid;
	u_long r_nc_semantics;
	char *r_nc_protofmly;
	char *r_nc_proto;
};
typedef struct rpcb_entry rpcb_entry;





struct rpcb_entry_list {
	rpcb_entry rpcb_entry_map;
	struct rpcb_entry_list *rpcb_entry_next;
};
typedef struct rpcb_entry_list rpcb_entry_list;

typedef rpcb_entry_list *rpcb_entry_list_ptr;
















struct rpcbs_addrlist {
	u_long prog;
	u_long vers;
	int success;
	int failure;
	char *netid;
	struct rpcbs_addrlist *next;
};
typedef struct rpcbs_addrlist rpcbs_addrlist;



struct rpcbs_rmtcalllist {
	u_long prog;
	u_long vers;
	u_long proc;
	int success;
	int failure;
	int indirect;
	char *netid;
	struct rpcbs_rmtcalllist *next;
};
typedef struct rpcbs_rmtcalllist rpcbs_rmtcalllist;

typedef int rpcbs_proc[13];

typedef rpcbs_addrlist *rpcbs_addrlist_ptr;

typedef rpcbs_rmtcalllist *rpcbs_rmtcalllist_ptr;

struct rpcb_stat {
	rpcbs_proc info;
	int setinfo;
	int unsetinfo;
	rpcbs_addrlist_ptr addrinfo;
	rpcbs_rmtcalllist_ptr rmtinfo;
};
typedef struct rpcb_stat rpcb_stat;






typedef rpcb_stat rpcb_stat_byvers[3];







# 313 "/usr/include/rpc/rpcb_prot.h" 
# 316 "/usr/include/rpc/rpcb_prot.h" 

bool_t xdr_netbuf();






# 463 "/usr/include/rpc/rpcb_prot.h" 





extern  bool_t * rpcbproc_set_3();

extern  bool_t * rpcbproc_unset_3();

extern  char ** rpcbproc_getaddr_3();

extern  rpcblist_ptr * rpcbproc_dump_3();

extern  rpcb_rmtcallres * rpcbproc_callit_3();

extern  u_int * rpcbproc_gettime_3();

extern  struct netbuf * rpcbproc_uaddr2taddr_3();

extern  char ** rpcbproc_taddr2uaddr_3();
extern int rpcbprog_3_freeresult();

extern  bool_t * rpcbproc_set_4();
extern  bool_t * rpcbproc_unset_4();
extern  char ** rpcbproc_getaddr_4();
extern  rpcblist_ptr * rpcbproc_dump_4();

extern  rpcb_rmtcallres * rpcbproc_bcast_4();
extern  u_int * rpcbproc_gettime_4();
extern  struct netbuf * rpcbproc_uaddr2taddr_4();
extern  char ** rpcbproc_taddr2uaddr_4();

extern  char ** rpcbproc_getversaddr_4();

extern  rpcb_rmtcallres * rpcbproc_indirect_4();

extern  rpcb_entry_list_ptr * rpcbproc_getaddrlist_4();

extern  rpcb_stat * rpcbproc_getstat_4();
extern int rpcbprog_4_freeresult();


extern bool_t xdr_rpcb();
extern bool_t xdr_rp__list();
extern bool_t xdr_rpcblist_ptr();
extern bool_t xdr_rpcb_rmtcallargs();
extern bool_t xdr_rpcb_rmtcallres();
extern bool_t xdr_rpcb_entry();
extern bool_t xdr_rpcb_entry_list();
extern bool_t xdr_rpcb_entry_list_ptr();
extern bool_t xdr_rpcbs_addrlist();
extern bool_t xdr_rpcbs_rmtcalllist();
extern bool_t xdr_rpcbs_proc();
extern bool_t xdr_rpcbs_addrlist_ptr();
extern bool_t xdr_rpcbs_rmtcalllist_ptr();
extern bool_t xdr_rpcb_stat();
extern bool_t xdr_rpcb_stat_byvers();


# 32 "/usr/include/rpc/rpcb_clnt.h" 2

# 35 "/usr/include/rpc/rpcb_clnt.h" 


# 52 "/usr/include/rpc/rpcb_clnt.h" 

extern bool_t		rpcb_set();
extern bool_t		rpcb_unset();
extern rpcblist	*rpcb_getmaps();
extern enum clnt_stat	rpcb_rmtcall();
extern bool_t		rpcb_getaddr();
extern bool_t		rpcb_gettime();
extern char		*rpcb_taddr2uaddr();
extern struct netbuf	*rpcb_uaddr2taddr();


# 65 "/usr/include/rpc/rpcb_clnt.h" 



# 45 "/usr/include/rpc/rpc.h" 2



# 15 "../include/p4.h" 2




# 1 "/usr/include/netinet/in.h" 1

















# 234 "/usr/include/netinet/in.h" 

# 20 "../include/p4.h" 2

# 23 "../include/p4.h" 


# 1 "/usr/ucbinclude/signal.h" 1







#ident	"@(#)signal.h	1.1	90/04/27 SMI"	





















 

# 34 "/usr/ucbinclude/signal.h" 

# 26 "../include/p4.h" 2
# 1 "/usr/include/errno.h" 1










#ident	"@(#)errno.h	1.13	95/09/10 SMI"	





# 1 "/usr/include/sys/errno.h" 1










#ident	"@(#)errno.h	1.15	95/01/22 SMI"	






















# 36 "/usr/include/sys/errno.h" 











































































































	










				


	



				



















# 183 "/usr/include/sys/errno.h" 



# 18 "/usr/include/errno.h" 2

# 21 "/usr/include/errno.h" 


# 24 "/usr/include/errno.h" 
# 27 "/usr/include/errno.h" 

extern int errno;


# 33 "/usr/include/errno.h" 



# 27 "../include/p4.h" 2
# 1 "/usr/include/sys/time.h" 1


















# 34 "/usr/include/sys/time.h" 
# 67 "/usr/include/sys/time.h" 
# 124 "/usr/include/sys/time.h" 
# 136 "/usr/include/sys/time.h" 
# 154 "/usr/include/sys/time.h" 
# 178 "/usr/include/sys/time.h" 
# 186 "/usr/include/sys/time.h" 
# 237 "/usr/include/sys/time.h" 
# 239 "/usr/include/sys/time.h" 
# 258 "/usr/include/sys/time.h" 
# 278 "/usr/include/sys/time.h" 
# 290 "/usr/include/sys/time.h" 

# 28 "../include/p4.h" 2
# 1 "/usr/include/pwd.h" 1










#ident	"@(#)pwd.h	1.17	95/08/28 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 14 "/usr/include/pwd.h" 2

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 16 "/usr/include/pwd.h" 2

# 18 "/usr/include/pwd.h" 

# 1 "/usr/ucbinclude/stdio.h" 1



























 

#ident	"@(#)stdio.h	1.7	95/06/08 SMI"	




# 56 "/usr/ucbinclude/stdio.h" 
# 58 "/usr/ucbinclude/stdio.h" 
# 71 "/usr/ucbinclude/stdio.h" 
# 73 "/usr/ucbinclude/stdio.h" 
# 88 "/usr/ucbinclude/stdio.h" 
# 120 "/usr/ucbinclude/stdio.h" 
# 128 "/usr/ucbinclude/stdio.h" 
# 152 "/usr/ucbinclude/stdio.h" 
# 163 "/usr/ucbinclude/stdio.h" 
# 216 "/usr/ucbinclude/stdio.h" 
# 227 "/usr/ucbinclude/stdio.h" 
# 240 "/usr/ucbinclude/stdio.h" 
# 277 "/usr/ucbinclude/stdio.h" 

# 20 "/usr/include/pwd.h" 2


# 24 "/usr/include/pwd.h" 


struct passwd {
	char	*pw_name;
	char	*pw_passwd;
	uid_t	pw_uid;
	gid_t	pw_gid;
	char	*pw_age;
	char	*pw_comment;
	char	*pw_gecos;
	char	*pw_dir;
	char	*pw_shell;
};

# 39 "/usr/include/pwd.h" 

struct comment {
	char	*c_dept;
	char	*c_name;
	char	*c_acct;
	char	*c_bin;
};


# 52 "/usr/include/pwd.h" 
# 65 "/usr/include/pwd.h" 


extern struct passwd *getpwuid();		
extern struct passwd *getpwnam();		

# 71 "/usr/include/pwd.h" 

extern struct passwd *getpwent_r();
extern struct passwd *fgetpwent_r();

extern void setpwent();
extern void endpwent();
extern struct passwd *getpwent();		
extern struct passwd *fgetpwent();		
extern int putpwent();








# 90 "/usr/include/pwd.h" 


# 93 "/usr/include/pwd.h" 
# 129 "/usr/include/pwd.h" 


# 164 "/usr/include/pwd.h" 


extern struct passwd *getpwuid_r();
extern struct passwd *getpwnam_r();







# 177 "/usr/include/pwd.h" 



# 29 "../include/p4.h" 2
# 1 "/usr/ucbinclude/fcntl.h" 1







#ident	"@(#)fcntl.h	1.1	90/04/27 SMI"	





















 

# 69 "/usr/ucbinclude/fcntl.h" 
# 75 "/usr/ucbinclude/fcntl.h" 
# 131 "/usr/ucbinclude/fcntl.h" 
# 154 "/usr/ucbinclude/fcntl.h" 
# 197 "/usr/ucbinclude/fcntl.h" 

# 30 "../include/p4.h" 2

# 1 "../include/p4_config.h" 1










# 32 "../include/p4.h" 2
# 1 "../include/p4_MD.h" 1















# 21 "../include/p4_MD.h" 


# 27 "../include/p4_MD.h" 


# 33 "../include/p4_MD.h" 


# 39 "../include/p4_MD.h" 


# 45 "../include/p4_MD.h" 


# 48 "../include/p4_MD.h" 
# 53 "../include/p4_MD.h" 



# 59 "../include/p4_MD.h" 


# 65 "../include/p4_MD.h" 


# 69 "../include/p4_MD.h" 


# 73 "../include/p4_MD.h" 


# 79 "../include/p4_MD.h" 


# 84 "../include/p4_MD.h" 


# 88 "../include/p4_MD.h" 



# 93 "../include/p4_MD.h" 


# 97 "../include/p4_MD.h" 


# 101 "../include/p4_MD.h" 


# 105 "../include/p4_MD.h" 


# 109 "../include/p4_MD.h" 



# 116 "../include/p4_MD.h" 
# 120 "../include/p4_MD.h" 


# 126 "../include/p4_MD.h" 
# 130 "../include/p4_MD.h" 


# 137 "../include/p4_MD.h" 





# 144 "../include/p4_MD.h" 


# 148 "../include/p4_MD.h" 





# 159 "../include/p4_MD.h" 






































# 215 "../include/p4_MD.h" 


# 232 "../include/p4_MD.h" 
















# 250 "../include/p4_MD.h" 







# 271 "../include/p4_MD.h" 




# 289 "../include/p4_MD.h" 






# 312 "../include/p4_MD.h" 










# 324 "../include/p4_MD.h" 
# 336 "../include/p4_MD.h" 
# 349 "../include/p4_MD.h" 



# 372 "../include/p4_MD.h" 


# 377 "../include/p4_MD.h" 





# 391 "../include/p4_MD.h" 




# 1 "/usr/include/sys/mman.h" 1

































#ident	"@(#)mman.h	1.19	95/11/10 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 37 "/usr/include/sys/mman.h" 2

# 40 "/usr/include/sys/mman.h" 















# 56 "/usr/include/sys/mman.h" 
# 65 "/usr/include/sys/mman.h" 
























# 91 "/usr/include/sys/mman.h" 



# 99 "/usr/include/sys/mman.h" 

















# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 117 "/usr/include/sys/mman.h" 2





# 122 "/usr/include/sys/mman.h" 
# 149 "/usr/include/sys/mman.h" 

extern caddr_t mmap();
extern int munmap();
extern int mprotect();
extern int mincore();
extern int memcntl();
extern int msync();
extern int madvise();
extern int mlock();
extern int mlockall();
extern int munlock();
extern int munlockall();
































# 195 "/usr/include/sys/mman.h" 



# 396 "../include/p4_MD.h" 2
# 1 "/usr/include/sys/systeminfo.h" 1










#ident	"@(#)systeminfo.h	1.14	93/06/11 SMI"	

# 15 "/usr/include/sys/systeminfo.h" 


# 24 "/usr/include/sys/systeminfo.h" 


































					





					











# 78 "/usr/include/sys/systeminfo.h" 

int sysinfo();



# 85 "/usr/include/sys/systeminfo.h" 



# 397 "../include/p4_MD.h" 2
# 1 "/usr/include/sys/processor.h" 1












#ident	"@(#)processor.h	1.4	94/11/11 SMI"

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 16 "/usr/include/sys/processor.h" 2
# 1 "/usr/include/sys/procset.h" 1










#ident	"@(#)procset.h	1.15	93/05/05 SMI"	

# 15 "/usr/include/sys/procset.h" 


















typedef enum idtype {
	P_PID,		
	P_PPID,		
	P_PGID,		
			
	P_SID,		
	P_CID,		
	P_UID,		
	P_GID,		
	P_ALL,		
	P_LWPID		
} idtype_t;







typedef enum idop {
	POP_DIFF,	
			
			
	POP_AND,	
			
			
	POP_OR,		
			
			
	POP_XOR		
			
			
} idop_t;







typedef struct procset {
	idop_t		p_op;	
				
				
				

	idtype_t	p_lidtype;
				
				
	id_t		p_lid;	

	idtype_t	p_ridtype;
				
				
	id_t		p_rid;	
} procset_t;













# 106 "/usr/include/sys/procset.h" 
# 127 "/usr/include/sys/procset.h" 


# 131 "/usr/include/sys/procset.h" 



# 17 "/usr/include/sys/processor.h" 2

# 20 "/usr/include/sys/processor.h" 









typedef	int	processorid_t;




















typedef struct {
	int	pi_state;			
	char	pi_processor_type[16];	
	char	pi_fputypes[32];	
	int	pi_clock;			
} processor_info_t;












# 76 "/usr/include/sys/processor.h" 


extern int	p_online();
extern int	processor_info();
extern int	processor_bind();




# 87 "/usr/include/sys/processor.h" 



# 398 "../include/p4_MD.h" 2
# 1 "/usr/include/sys/procset.h" 1







# 106 "/usr/include/sys/procset.h" 
# 133 "/usr/include/sys/procset.h" 

# 399 "../include/p4_MD.h" 2
# 1 "/usr/include/synch.h" 1










#ident	"@(#)synch.h	1.31	95/08/24 SMI"







# 1 "/usr/include/sys/machlock.h" 1




# 87 "/usr/include/sys/machlock.h" 

# 20 "/usr/include/synch.h" 2
# 1 "/usr/include/sys/time.h" 1


















# 34 "/usr/include/sys/time.h" 
# 67 "/usr/include/sys/time.h" 
# 124 "/usr/include/sys/time.h" 
# 136 "/usr/include/sys/time.h" 
# 154 "/usr/include/sys/time.h" 
# 178 "/usr/include/sys/time.h" 
# 186 "/usr/include/sys/time.h" 
# 237 "/usr/include/sys/time.h" 
# 239 "/usr/include/sys/time.h" 
# 258 "/usr/include/sys/time.h" 
# 278 "/usr/include/sys/time.h" 
# 290 "/usr/include/sys/time.h" 

# 21 "/usr/include/synch.h" 2
# 1 "/usr/include/sys/synch.h" 1







#ident	"@(#)synch.h	1.21	93/04/13 SMI"

# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 11 "/usr/include/sys/synch.h" 2

# 14 "/usr/include/sys/synch.h" 




typedef unsigned char	uint8_t;



typedef unsigned long	uint32_t;



typedef u_longlong_t	uint64_t;






typedef struct _lwp_mutex {
	struct _mutex_flags {
		uint8_t		flag[4];
		uint32_t 	type;
	} flags;
	union _mutex_lock_un {
		struct _mutex_lock {
			uint8_t	pad[8];
		} lock64;
		uint64_t owner64;
	} lock;
	uint64_t data;
} lwp_mutex_t;







typedef struct _lwp_cond {
	struct _lwp_cond_flags {
		uint8_t		flag[4];
		uint32_t 	type;
	} flags;
	uint64_t data;
} lwp_cond_t;






typedef struct _lwp_sema {
	uint32_t	count;		
	uint32_t	type;
	uint8_t		flags[8];	
	uint64_t	data;		
} lwp_sema_t;








# 82 "/usr/include/sys/synch.h" 



# 22 "/usr/include/synch.h" 2


# 26 "/usr/include/synch.h" 






typedef struct _sema {
	
	uint32_t	count;		
	uint32_t	type;
	uint64_t	pad1[3];	
	uint64_t 	pad2[2];	
} sema_t;












typedef lwp_mutex_t mutex_t;
typedef lwp_cond_t cond_t;




typedef struct _rwlock {
	uint32_t	readers;	
	uint32_t	type;
	uint64_t	pad1[3];	
	uint64_t	pad2[2];	
	uint64_t	pad3[2];	
} rwlock_t;

# 100 "/usr/include/synch.h" 


int	_lwp_mutex_lock();
int	_lwp_mutex_unlock();
int	_lwp_mutex_trylock();
int	_lwp_cond_wait();
int	_lwp_cond_timedwait();
int	_lwp_cond_signal();
int	_lwp_cond_broadcast();
int	_lwp_sema_init();
int	_lwp_sema_wait();
int	_lwp_sema_post();
int	cond_init();
int	cond_destroy();
int	cond_wait();
int	cond_timedwait();
int	cond_signal();
int	cond_broadcast();
int	mutex_init();
int	mutex_destroy();
int	mutex_lock();
int	mutex_trylock();
int	mutex_unlock();
int	rwlock_init();
int	rwlock_destroy();
int	rw_rdlock();
int	rw_wrlock();
int	rw_unlock();
int	rw_tryrdlock();
int	rw_trywrlock();
int	sema_init();
int	sema_destroy();
int	sema_wait();
int	sema_post();
int	sema_trywait();













































# 187 "/usr/include/synch.h" 


int _sema_held();
int _rw_read_held();
int _rw_write_held();
int _mutex_held();





# 200 "/usr/include/synch.h" 



# 400 "../include/p4_MD.h" 2



# 407 "../include/p4_MD.h" 




# 431 "../include/p4_MD.h" 







	 typedef int MD_lock_t;







# 458 "../include/p4_MD.h" 



# 480 "../include/p4_MD.h" 





# 1 "/usr/ucbinclude/unistd.h" 1







#ident	"@(#)unistd.h	1.4	92/12/15 SMI"	





















 

# 111 "/usr/ucbinclude/unistd.h" 

# 486 "../include/p4_MD.h" 2











# 498 "../include/p4_MD.h" 
# 510 "../include/p4_MD.h" 
# 529 "../include/p4_MD.h" 
# 537 "../include/p4_MD.h" 
# 549 "../include/p4_MD.h" 




# 559 "../include/p4_MD.h" 
# 583 "../include/p4_MD.h" 





# 618 "../include/p4_MD.h" 



# 637 "../include/p4_MD.h" 





# 668 "../include/p4_MD.h" 


# 674 "../include/p4_MD.h" 




typedef unsigned long p4_usc_time_t;









# 1 "/usr/include/sys/systeminfo.h" 1







# 74 "/usr/include/sys/systeminfo.h" 
# 75 "/usr/include/sys/systeminfo.h" 
# 87 "/usr/include/sys/systeminfo.h" 

# 689 "../include/p4_MD.h" 2






# 705 "../include/p4_MD.h" 









# 33 "../include/p4.h" 2
# 1 "../include/p4_mon.h" 1

typedef MD_lock_t p4_lock_t;





struct p4_mon_queue;  
struct p4_monitor {
    p4_lock_t mon_lock;
    struct p4_mon_queue *qs;
};

typedef struct p4_monitor p4_monitor_t;


struct p4_mon_queue {
    int count;
    p4_lock_t delay_lock;
};


struct p4_getsub_monitor {
    struct p4_monitor m;
    int sub;
};

typedef struct p4_getsub_monitor p4_getsub_monitor_t;



struct p4_barrier_monitor {
    struct p4_monitor m;
};

typedef struct p4_barrier_monitor p4_barrier_monitor_t;

struct p4_askfor_monitor {
    struct p4_monitor m;
    int pgdone;
    int pbdone;
};

typedef struct p4_askfor_monitor p4_askfor_monitor_t;

# 34 "../include/p4.h" 2
# 1 "../include/p4_sr.h" 1
struct p4_queued_msg;  
struct p4_msg_queue {
    p4_monitor_t m;
    p4_lock_t ack_lock;
    struct p4_queued_msg *first_msg, *last_msg;
};



















# 27 "../include/p4_sr.h" 
# 29 "../include/p4_sr.h" 

# 32 "../include/p4_sr.h" 









































# 35 "../include/p4.h" 2



struct p4_procgroup_entry {
    char host_name[64];
    int numslaves_in_group;
    char slave_full_pathname[256];
    char username[10];
};


struct p4_procgroup {
    struct p4_procgroup_entry entries[256];
    int num_entries;
};


# 1 "../include/p4_funcs.h" 1

# 4 "../include/p4_funcs.h" 



# 9 "../include/p4_funcs.h" 





double p4_usclock ();
void init_usclock ();
char *p4_shmalloc ();
void p4_set_avail_buff ();
int p4_am_i_cluster_master ();
int p4_askfor 
();
int p4_askfor_init ();
void p4_barrier ();
int p4_barrier_init ();
int p4_broadcastx ();
int MD_clock ();
int p4_create ();
int p4_create_procgroup ();
int p4_startup ();
# 30 "../include/p4_funcs.h" 
# 34 "../include/p4_funcs.h" 

# 36 "../include/p4_funcs.h" 
# 40 "../include/p4_funcs.h" 

void p4_dprintf ();

void p4_dprintfl ();



void p4_error ();
void p4_set_hard_errors ();
void p4_global_barrier ();
void p4_get_cluster_masters ();
void p4_get_cluster_ids ();
int p4_get_my_cluster_id ();
int p4_get_my_id ();
int p4_get_my_id_from_proc ();
int p4_getsub_init ();
void p4_getsubs ();
int p4_global_op 
();
int p4_initenv ();
void p4_post_init ();
void p4_int_absmax_op ();
void p4_int_absmin_op ();
void p4_int_max_op ();
void p4_int_min_op ();
void p4_int_mult_op ();
void p4_int_sum_op ();
void p4_dbl_absmax_op ();
void p4_dbl_absmin_op ();
void p4_dbl_max_op ();
void p4_dbl_min_op ();
void p4_dbl_mult_op ();
void p4_dbl_sum_op ();
void p4_flt_sum_op ();
void p4_flt_absmax_op ();
void p4_flt_absmin_op ();
void p4_flt_max_op ();
void p4_flt_min_op ();
void p4_flt_mult_op ();
void p4_flt_sum_op ();
void p4_mcontinue ();
void p4_mdelay ();
void p4_menter ();
int p4_messages_available ();
int p4_any_messages_available ();
void p4_mexit ();
int p4_moninit ();
void p4_msg_free ();
char *p4_msg_alloc ();
int p4_num_cluster_ids ();
int p4_num_total_ids ();
int p4_num_total_slaves ();
void p4_probend ();
void p4_progend ();
int p4_recv ();
int p4_get_dbg_level ();
void p4_set_dbg_level ();
void p4_shfree ();
int p4_soft_errors ();
void p4_update 
();
char *p4_version ();
char *p4_machine_type ();
int p4_wait_for_end ();
int p4_proc_info ();
void p4_print_avail_buffs ();
struct p4_procgroup *p4_alloc_procgroup ();

int send_message ();

# 112 "../include/p4_funcs.h" 


# 53 "../include/p4.h" 2

# 56 "../include/p4.h" 


# 1 "../include/alog.h" 1
# 1 "/usr/ucbinclude/stdio.h" 1



























 

#ident	"@(#)stdio.h	1.7	95/06/08 SMI"	




# 56 "/usr/ucbinclude/stdio.h" 
# 58 "/usr/ucbinclude/stdio.h" 
# 71 "/usr/ucbinclude/stdio.h" 
# 73 "/usr/ucbinclude/stdio.h" 
# 88 "/usr/ucbinclude/stdio.h" 
# 120 "/usr/ucbinclude/stdio.h" 
# 128 "/usr/ucbinclude/stdio.h" 
# 152 "/usr/ucbinclude/stdio.h" 
# 163 "/usr/ucbinclude/stdio.h" 
# 216 "/usr/ucbinclude/stdio.h" 
# 227 "/usr/ucbinclude/stdio.h" 
# 240 "/usr/ucbinclude/stdio.h" 
# 277 "/usr/ucbinclude/stdio.h" 

# 2 "../include/alog.h" 2
# 1 "../include/usc.h" 1
















# 19 "../include/usc.h" 


# 23 "../include/usc.h" 


# 27 "../include/usc.h" 


# 31 "../include/usc.h" 


# 35 "../include/usc.h" 


# 39 "../include/usc.h" 


# 43 "../include/usc.h" 


# 47 "../include/usc.h" 






typedef unsigned long usc_time_t;


# 58 "../include/usc.h" 









# 71 "../include/usc.h" 



# 79 "../include/usc.h" 


extern usc_time_t usc_MD_rollover_val;





# 92 "../include/usc.h" 


# 99 "../include/usc.h" 


# 106 "../include/usc.h" 


# 113 "../include/usc.h" 


# 120 "../include/usc.h" 


# 125 "../include/usc.h" 


# 133 "../include/usc.h" 





# 143 "../include/usc.h" 











# 154 "../include/usc.h" 
# 160 "../include/usc.h" 



void usc_init ();
usc_time_t usc_MD_clock ();


# 3 "../include/alog.h" 2
















 

struct trace_buf;  
struct head_trace_buf {
        int             next_entry;
        int             max_size;
        unsigned long   prev_time;
        unsigned long   ind_time;
        int             trace_flag;
        struct trace_buf *xx_list;
        struct trace_buf *cbuf;
        FILE            *file_t;
};

struct trace_buf {
        struct trace_buf *next_buf;
        struct trace_table {
                int     id;
                int     task_id;
                int     event;
                int     data_int;
                char    data_string[12+1];
                unsigned long     tind;
                unsigned long     tstamp;
        } ALOG_table[100];
};

extern int xx_alog_status;
extern int xx_alog_setup_called;
extern int xx_alog_output_called;
extern char xx_alog_outdir[];
extern struct head_trace_buf *xx_buf_head;





# 56 "../include/alog.h" 
# 62 "../include/alog.h" 


void xx_write (), 
     xx_dump (), 
     xx_dump_aux (),
     xx_user (), 
     xx_user1 (), 
     xx_alog_setup ();
int  xx_getbuf ();


# 136 "../include/alog.h" 















# 154 "../include/alog.h" 




# 168 "../include/alog.h" 















# 59 "../include/p4.h" 2

# 1 "../include/usc.h" 1













# 44 "../include/usc.h" 
# 55 "../include/usc.h" 
# 66 "../include/usc.h" 
# 73 "../include/usc.h" 
# 86 "../include/usc.h" 
# 93 "../include/usc.h" 
# 100 "../include/usc.h" 
# 107 "../include/usc.h" 
# 114 "../include/usc.h" 
# 121 "../include/usc.h" 
# 126 "../include/usc.h" 
# 154 "../include/usc.h" 
# 166 "../include/usc.h" 

# 61 "../include/p4.h" 2




# 2 "serv_p4.c" 2
# 1 "../include/p4_sys.h" 1



# 1 "/usr/ucbinclude/strings.h" 1







#ident	"@(#)strings.h	1.1	90/04/27 SMI"	





















 









char	*strcat();
char	*strncat();
int	strcmp();
int	strncmp();
int	strcasecmp();
int	strncasecmp();
char	*strcpy();
char	*strncpy();
int	strlen();
char	*index();
char	*rindex();


# 5 "../include/p4_sys.h" 2
# 5 "../include/p4_sys.h" 
# 16 "../include/p4_sys.h" 


# 1 "../include/p4_patchlevel.h" 1

# 19 "../include/p4_sys.h" 2
# 1 "../include/p4_sock_util.h" 1

# 1 "/usr/include/sys/socket.h" 1







# 266 "/usr/include/sys/socket.h" 
# 336 "/usr/include/sys/socket.h" 

# 3 "../include/p4_sock_util.h" 2

# 1 "/usr/include/netdb.h" 1







































#ident	"@(#)netdb.h	1.14	94/10/04 SMI"	

# 44 "/usr/include/netdb.h" 








struct	hostent {
	char	*h_name;	
	char	**h_aliases;	
	int	h_addrtype;	
	int	h_length;	
	char	**h_addr_list;	

};





struct	netent {
	char		*n_name;	
	char		**n_aliases;	
	int		n_addrtype;	
	unsigned long	n_net;		
};

struct	servent {
	char	*s_name;	
	char	**s_aliases;	
	int	s_port;		
	char	*s_proto;	
};

struct	protoent {
	char	*p_name;	
	char	**p_aliases;	
	int	p_proto;	
};

# 143 "/usr/include/netdb.h" 

struct hostent	*gethostbyname_r();
struct hostent	*gethostbyaddr_r();
struct hostent	*gethostent_r();
struct servent	*getservbyname_r();
struct servent	*getservbyport_r();
struct servent	*getservent_r();
struct netent	*getnetbyname_r();
struct netent	*getnetbyaddr_r();
struct netent	*getnetent_r();
struct protoent	*getprotobyname_r();
struct protoent	*getprotobynumber_r();
struct protoent	*getprotoent_r();
int		 getnetgrent_r();
int		 innetgr();


struct hostent	*gethostbyname();
struct hostent	*gethostbyaddr();
struct hostent	*gethostent();
struct netent	*getnetbyname();
struct netent	*getnetbyaddr();
struct netent	*getnetent();
struct servent	*getservbyname();
struct servent	*getservbyport();
struct servent	*getservent();
struct protoent	*getprotobyname();
struct protoent	*getprotobynumber();
struct protoent	*getprotoent();
int		 getnetgrent();

int sethostent();
int endhostent();
int setnetent();
int endnetent();
int setservent();
int endservent();
int setprotoent();
int endprotoent();
int setnetgrent();
int endnetgrent();
int rcmd();
int rexec();
int rresvport();
int ruserok();







extern  int h_errno;












# 210 "/usr/include/netdb.h" 



# 5 "../include/p4_sock_util.h" 2

# 9 "../include/p4_sock_util.h" 



# 1 "/usr/include/netinet/tcp.h" 1









#ident	"@(#)tcp.h	1.5	93/08/18 SMI"


# 1 "/usr/include/sys/isa_defs.h" 1




# 130 "/usr/include/sys/isa_defs.h" 
# 135 "/usr/include/sys/isa_defs.h" 
# 215 "/usr/include/sys/isa_defs.h" 
# 259 "/usr/include/sys/isa_defs.h" 

# 14 "/usr/include/netinet/tcp.h" 2

# 17 "/usr/include/netinet/tcp.h" 


typedef	u_long	tcp_seq;




struct tcphdr {
	u_short	th_sport;		
	u_short	th_dport;		
	tcp_seq	th_seq;			
	tcp_seq	th_ack;			
# 32 "/usr/include/netinet/tcp.h" 

	u_int	th_off:4,		
		th_x2:4;		

	u_char	th_flags;






	u_short	th_win;			
	u_short	th_sum;			
	u_short	th_urp;			
};










# 59 "/usr/include/netinet/tcp.h" 
















# 77 "/usr/include/netinet/tcp.h" 



# 13 "../include/p4_sock_util.h" 2



















struct net_message_t 
{
    int type:32;
    int port:32;
    int success:32;
    char pgm[256];
    char host[128];
    char am_slave[32];
    char message[512];
};



# 48 "../include/p4_sock_util.h" 



# 54 "../include/p4_sock_util.h" 





# 20 "../include/p4_sys.h" 2

# 24 "../include/p4_sys.h" 


extern int errno;










# 1 "../include/p4_defs.h" 1








# 11 "../include/p4_defs.h" 





struct proc_info {
    int port;
    int switch_port;
    int unix_id;
    int slave_idx;
    int group_id;
    int am_rm;
    char host_name[64];

    struct sockaddr_in sockaddr;

    char machine_type[16];
};



struct p4_avail_buff {
    int size;			
    struct p4_msg *buff;
};

struct p4_global_data {
# 42 "../include/p4_defs.h" 

    struct proc_info proctable[256];
    int listener_pid;
    int listener_port;
    int local_communication_only;
    int local_slave_count;
    int n_forked_pids;
    char my_host_name[64];
    struct p4_avail_buff avail_buffs[8];
    p4_lock_t avail_buffs_lock;
    struct p4_queued_msg *avail_quel;
    p4_lock_t avail_quel_lock;
    struct p4_msg_queue shmem_msg_queues[1];
    int num_in_proctable;
    int num_installed;
    p4_lock_t slave_lock;
    int dest_id[256];
    int listener_fd;
    int max_connections;
    int cube_msgs_out;    
    unsigned long reference_time;  
    int hi_cluster_id;
    int low_cluster_id;
    void *cluster_shmem;
    p4_barrier_monitor_t cluster_barrier;
    char application_id[16];
};
extern struct p4_global_data *p4_global;

struct connection {
    int type;
    int port;
    int switch_port;
    int same_data_rep;
};

struct local_data {		
    int listener_fd;
    int my_id;
    int local_commtype;		
    struct p4_msg_queue *queued_messages;
    int am_bm;
    struct connection *conntab;	
    struct p4_procgroup *procgroup;
    int soft_errors;            
    char *xdr_buff;
    XDR xdr_enc;
    XDR xdr_dec;
};
extern struct local_data *p4_local;

struct listener_data {
    int listening_fd;
    int slave_fd;
};
extern struct listener_data *listener_info;








struct p4_msg {
    struct p4_msg *link;
    int orig_len;
    int type;                
    int to;
    int from;
    int ack_req;
    int len;
    int msg_id;		        
    int data_type;		
    int pad;
    char *msg;	
};

struct p4_net_msg_hdr {
    int msg_type:32;
    int to:32;
    int from:32;
    int ack_req:32;
    int msg_len:32;
    int msg_id:32;		
    int data_type:32;		
    int imm_from:32;            
                     
};

struct net_initial_handshake {
   int pid:32;
   int rm_num:32;
   
};

struct p4_queued_msg {
    struct p4_msg *qmsg;
    struct p4_queued_msg *next;
};









struct slave_listener_msg {
    int type:32;
    int from:32;
    int to:32;
    int to_pid:32;
    int lport:32;
    int pad:32;
};















struct bm_rm_msg {
    int type:32;

    
    int numslaves:32;
    int numinproctab:32;
    int memsize:32;
    int rm_num:32;
    int debug_level:32;
    int logging_flag:32;

    
    int port:32;

    
    int slave_idx:32;
    int slave_pid:32;
    int am_rm:32;

    
    int unix_id:32;
    int group_id:32;
    int switch_port:32;
    
    char host_name[64];

    
    char pgm[256];
    char version[8];
    char outfile[256];
    char application_id[16];
    char machine_type[16];
};






struct p4_brdcst_info_struct {




  int initialized;             
  int up;                      
  int left_cluster;            
  int right_cluster;           
  int left_slave;              
  int right_slave;             
};
extern struct p4_brdcst_info_struct p4_brdcst_info;





extern int p4_hard_errors;

# 38 "../include/p4_sys.h" 2
# 1 "../include/p4_sys_funcs.h" 1
# 1 "../include/p4_sys_funcs.h" 
# 7 "../include/p4_sys_funcs.h" 



# 1 "/usr/include/stdlib.h" 1










#ident	"@(#)stdlib.h	1.27	95/08/28 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 14 "/usr/include/stdlib.h" 2

# 16 "/usr/include/stdlib.h" 
# 18 "/usr/include/stdlib.h" 


# 22 "/usr/include/stdlib.h" 


typedef	struct {
	int	quot;
	int	rem;
} div_t;

typedef struct {
	long	quot;
	long	rem;
} ldiv_t;


typedef struct {
	long long	quot;
	long long	rem;
} lldiv_t;


# 44 "/usr/include/stdlib.h" 


# 49 "/usr/include/stdlib.h" 


# 53 "/usr/include/stdlib.h" 








typedef long wchar_t;


# 78 "/usr/include/stdlib.h" 
# 111 "/usr/include/stdlib.h" 
# 129 "/usr/include/stdlib.h" 
# 135 "/usr/include/stdlib.h" 
# 166 "/usr/include/stdlib.h" 
# 179 "/usr/include/stdlib.h" 


extern unsigned char	_ctype[];



extern double atof();
extern int atoi();
extern long int atol();
extern double strtod();
extern long int strtol();
extern unsigned long strtoul();

extern int rand();
extern void srand();
# 195 "/usr/include/stdlib.h" 
# 197 "/usr/include/stdlib.h" 


extern void *calloc();
extern void free();
extern void *malloc();
extern void *realloc();

extern void abort();
extern int atexit();
extern void exit();
extern char *getenv();
extern int system();

extern void *bsearch();
extern void qsort();

extern int abs();
extern div_t div();
extern long int labs();
extern ldiv_t ldiv();

extern int mbtowc();
extern int mblen();
extern int wctomb();

extern size_t mbstowcs();
extern size_t wcstombs();

extern long a64l();
extern int dup2();
extern char *ecvt();
extern char *fcvt();
extern char *qecvt();
extern char *qfcvt();
extern char *qgcvt();
extern char *getcwd();
extern char *getlogin();
extern int getopt();
extern int getsubopt();
extern char *optarg;
extern int optind, opterr, optopt;
extern char *getpass();
extern int getpw();
extern char *gcvt();
extern int isatty();
extern char *l64a();
extern void *memalign();
extern char *mktemp();
extern int putenv();
extern char *realpath();
extern void setkey();
extern void swab();
extern char *ttyname();
extern int ttyslot();
extern void *valloc();
extern char *ptsname();
extern int  grantpt();
extern int  unlockpt();

extern double drand48();
extern double erand48();
extern long jrand48();
extern void lcong48();
extern long lrand48();
extern long mrand48();
extern long nrand48();
extern unsigned short *seed48();
extern void srand48();


extern long long atoll();
extern long long llabs();
extern lldiv_t lldiv();
extern char *lltostr();
extern long long strtoll();
extern unsigned long long strtoull();
extern char *ulltostr();




# 280 "/usr/include/stdlib.h" 



# 11 "../include/p4_sys_funcs.h" 2


# 1 "/usr/ucbinclude/unistd.h" 1







#ident	"@(#)unistd.h	1.4	92/12/15 SMI"	





















 

# 111 "/usr/ucbinclude/unistd.h" 

# 14 "../include/p4_sys_funcs.h" 2


char *xx_malloc ();
char *MD_shmalloc ();
char *print_conn_type ();
char *xx_shmalloc ();
int MD_clock ();
void get_qualified_hostname ();
void MD_set_reference_time ();
void MD_initenv ();
void MD_initmem ();
void MD_malloc_hint ();
void MD_shfree ();

int bm_start ()	;

void request_connection ();
int create_bm_processes ();
void net_slave_info 
()	;
int create_remote_processes ();
void create_rm_processes ()	;
void dump_conntab ();
void dump_global ()	;
void dump_listener ()	;
void dump_local ()	;
void dump_procgroup ()	;
void dump_tmsg ();
int establish_connection ();

void put_execer_port ();
int get_execer_port ();
int fork_p4 ();
void free_p4_msg ();
void free_quel ();
void get_inet_addr ();
void get_inet_addr_str ();

void dump_sockaddr ();
void dump_sockinfo ();
# 55 "../include/p4_sys_funcs.h" 

void get_pipe ()	;
int getswport ();
void handle_connection_interrupt ();
int shmem_msgs_available ();
int socket_msgs_available ();
int MD_tcmp_msgs_available ();
int MD_i860_msgs_available ();
int MD_CM5_msgs_available ();
int MD_NCUBE_msgs_available ();
int MD_euih_msgs_available ();
int MD_i860_send ();
int MD_CM5_send ();
int MD_NCUBE_send ();
int MD_eui_send ();
int MD_euih_send ();
struct p4_msg *MD_i860_recv ();
struct p4_msg *MD_CM5_recv ();
struct p4_msg *MD_NCUBE_recv ();
struct p4_msg *MD_eui_recv ();
struct p4_msg *MD_euih_recv ();
int in_same_cluster ();
void p4_cluster_shmem_sync ();

void init_avail_buffs ();
void free_avail_buffs ();
void initialize_msg_queue ();
int install_in_proctable ();

void listener ();
struct listener_data *alloc_listener_info ();
struct local_data *alloc_local_bm ();
struct local_data *alloc_local_listener ();
struct local_data *alloc_local_rm ();
struct local_data *alloc_local_slave ();
int myhost ();
int net_accept ()	;

int net_conn_to_listener ()	;

int net_create_slave ();
int net_recv ()	;
int net_send ()	;
void net_setup_anon_listener ()	;
void net_setup_listener ()	;

void net_set_sockbuf_size ();
void get_sock_info_by_hostname ();
int num_in_mon_queue ();
void alloc_global ();
struct p4_msg *alloc_p4_msg ();
struct p4_msg *get_tmsg ();
struct p4_msg *recv_message ();
struct p4_msg *search_p4_queue ();
struct p4_msg *shmem_recv ();
struct p4_msg *socket_recv ();
struct p4_msg *socket_recv_on_fd ();
struct p4_queued_msg *alloc_quel ();
void free_avail_quels ();
void process_args ()	;
int process_connect_request ()	;

int process_slave_message ()	;
struct p4_procgroup *alloc_procgroup ();
struct p4_procgroup *read_procgroup ()	;
void procgroup_to_proctable ();
void queue_p4_message ();

void receive_proc_table ()	;

int rm_start ();
void send_ack ()	;
void sync_with_remotes ();
void send_proc_table ();
void setup_conntab ();
int shmem_send ();
void shutdown_p4_socks ();
# 136 "../include/p4_sys_funcs.h" 

int sock_msg_avail_on_fd ();
int socket_send ();
int socket_close_conn ();
int start_slave 
();
int subtree_broadcast_p4 ();
void trap_sig_errs ();
void wait_for_ack ()	;
int xdr_recv ();
int xdr_send ();
int data_representation ();
int same_data_representation ();
void xx_init_shmalloc ();
void xx_shfree ();
void zap_p4_processes ();
struct p4_msg *MD_tcmp_recv ();
int MD_tcmp_send ();
struct hostent *gethostbyname_p4 ();
char *getpw_ss ();

# 163 "../include/p4_sys_funcs.h" 


# 246 "../include/p4_sys_funcs.h" 

# 39 "../include/p4_sys.h" 2

# 1 "../include/p4_macros.h" 1


# 7 "../include/p4_macros.h" 









# 18 "../include/p4_macros.h" 


# 22 "../include/p4_macros.h" 


# 30 "../include/p4_macros.h" 
























# 41 "../include/p4_sys.h" 2

# 1 "../include/p4_globals.h" 1
# 3 "../include/p4_globals.h" 



    


extern char procgroup_file[256];
extern char bm_outfile[100];
extern char rm_outfile_head[100];
extern char whoami_p4[100];
extern int  p4_debug_level, p4_remote_debug_level;
extern int  logging_flag;
extern int  execer_mynodenum;
extern char execer_id[132];
extern char execer_myhost[100];
extern int  execer_mynumprocs;
extern char execer_masthost[100];
extern char execer_jobname[100];
extern int  execer_mastport;
extern int  execer_numtotnodes;
extern struct p4_procgroup *execer_pg;
extern int  execer_starting_remotes;



extern char local_domain[100];
extern int  globmemsize;
extern int  sserver_port;
extern int  hand_start_remotes;

# 37 "../include/p4_globals.h" 


# 42 "../include/p4_globals.h" 


# 47 "../include/p4_globals.h" 


# 53 "../include/p4_globals.h" 

# 43 "../include/p4_sys.h" 2


# 47 "../include/p4_sys.h" 






















# 3 "serv_p4.c" 2















# 1 "/usr/include/sys/stat.h" 1










#ident	"@(#)stat.h	1.26	95/08/14 SMI"	

# 1 "/usr/include/sys/feature_tests.h" 1







# 24 "/usr/include/sys/feature_tests.h" 
# 33 "/usr/include/sys/feature_tests.h" 

# 14 "/usr/include/sys/stat.h" 2

# 1 "/usr/include/sys/time.h" 1


















# 34 "/usr/include/sys/time.h" 
# 67 "/usr/include/sys/time.h" 
# 124 "/usr/include/sys/time.h" 
# 136 "/usr/include/sys/time.h" 
# 154 "/usr/include/sys/time.h" 
# 178 "/usr/include/sys/time.h" 
# 186 "/usr/include/sys/time.h" 
# 237 "/usr/include/sys/time.h" 
# 239 "/usr/include/sys/time.h" 
# 258 "/usr/include/sys/time.h" 
# 278 "/usr/include/sys/time.h" 
# 290 "/usr/include/sys/time.h" 

# 16 "/usr/include/sys/stat.h" 2
# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 17 "/usr/include/sys/stat.h" 2

# 20 "/usr/include/sys/stat.h" 








# 70 "/usr/include/sys/stat.h" 


	

struct	stat {
	dev_t	st_dev;
	long	st_pad1[3];	
	ino_t	st_ino;
	mode_t	st_mode;
	nlink_t st_nlink;
	uid_t 	st_uid;
	gid_t 	st_gid;
	dev_t	st_rdev;
	long	st_pad2[2];
	off_t	st_size;
	long	st_pad3;	
	timestruc_t st_atim;
	timestruc_t st_mtim;
	timestruc_t st_ctim;
	long	st_blksize;
	long	st_blocks;
	char	st_fstype[16];
	long	st_pad4[8];	
};
# 95 "/usr/include/sys/stat.h" 




# 103 "/usr/include/sys/stat.h" 





























































# 167 "/usr/include/sys/stat.h" 
# 172 "/usr/include/sys/stat.h" 
# 183 "/usr/include/sys/stat.h" 


extern int fstat(), stat();

extern int mknod(), lstat(), fchmod();


extern int chmod();
extern int mkdir();
extern int mkfifo();
extern mode_t umask();




# 200 "/usr/include/sys/stat.h" 



# 19 "serv_p4.c" 2
# 1 "/usr/ucbinclude/sys/file.h" 1







#ident	"@(#)file.h	1.2	91/12/04 SMI"	





















 




# 37 "/usr/ucbinclude/sys/file.h" 








typedef struct file
{
	struct file  *f_next;		
	struct file  *f_prev;		
	ushort	f_flag;
	cnt_t	f_count;		
	struct vnode *f_vnode;		
	off_t	f_offset;		
	struct	cred *f_cred;		
	struct	aioreq *f_aiof;		
	struct	aioreq *f_aiob;		

	struct	file *f_slnk;		

} file_t;


# 64 "/usr/ucbinclude/sys/file.h" 




# 90 "/usr/ucbinclude/sys/file.h" 









































extern unsigned int filecnt;






extern void setf(), setpof();
extern char getpof();
extern int fassign();

extern off_t lseek();


# 20 "serv_p4.c" 2
# 1 "/usr/ucbinclude/sys/ioctl.h" 1










#ident	"@(#)ioctl.h	1.2	90/08/19 SMI"	





















 


































# 1 "/usr/ucbinclude/sys/types.h" 1







#ident	"@(#)types.h	1.10	93/07/21 SMI"	





















 

# 91 "/usr/ucbinclude/sys/types.h" 
# 206 "/usr/ucbinclude/sys/types.h" 
# 212 "/usr/ucbinclude/sys/types.h" 
# 310 "/usr/ucbinclude/sys/types.h" 

# 69 "/usr/ucbinclude/sys/ioctl.h" 2
# 1 "/usr/ucbinclude/sys/ttychars.h" 1







#ident	"@(#)ttychars.h	1.1	90/04/27 SMI"	





















 














struct ttychars {
	char	tc_erase;	
	char	tc_kill;	
	char	tc_intrc;	
	char	tc_quitc;	
	char	tc_startc;	
	char	tc_stopc;	
	char	tc_eofc;	
	char	tc_brkc;	
	char	tc_suspc;	
	char	tc_dsuspc;	
	char	tc_rprntc;	
	char	tc_flushc;	
	char	tc_werasc;	
	char	tc_lnextc;	
};


























# 70 "/usr/ucbinclude/sys/ioctl.h" 2
# 1 "/usr/include/sys/ttydev.h" 1










#ident	"@(#)ttydev.h	1.7	92/07/14 SMI"	

# 15 "/usr/include/sys/ttydev.h" 




























# 45 "/usr/include/sys/ttydev.h" 



# 71 "/usr/ucbinclude/sys/ioctl.h" 2
# 1 "/usr/include/sys/ttold.h" 1










#ident	"@(#)ttold.h	1.11	93/12/15 SMI"	

# 15 "/usr/include/sys/ttold.h" 






struct tchars {
	char	t_intrc;	
	char	t_quitc;	
	char	t_startc;	
	char	t_stopc;	
	char	t_eofc;		
	char	t_brkc;		
};


struct tc {
	char	t_intrc;	
	char	t_quitc;	
	char	t_startc;	
	char	t_stopc;	
	char	t_eofc;		
	char	t_brkc;		
};








struct	sgttyb {
	char	sg_ispeed;		
	char	sg_ospeed;		
	char	sg_erase;		
	char	sg_kill;		
	int	sg_flags;		
};


struct ltchars {
	char	t_suspc;	
	char	t_dsuspc;	
	char	t_rprntc;	
	char	t_flushc;	
	char	t_werasc;	
	char	t_lnextc;	
};

































































struct winsize {
	unsigned short ws_row;		
	unsigned short ws_col;		
	unsigned short ws_xpixel;	
	unsigned short ws_ypixel;	
};







# 163 "/usr/include/sys/ttold.h" 













































						


						





























					

					










# 256 "/usr/include/sys/ttold.h" 



# 72 "/usr/ucbinclude/sys/ioctl.h" 2






















































# 128 "/usr/ucbinclude/sys/ioctl.h" 







# 1 "/usr/include/sys/filio.h" 1










#ident	"@(#)filio.h	1.17	93/07/12 SMI"	




























# 1 "/usr/include/sys/ioccom.h" 1










#ident	"@(#)ioccom.h	1.10	92/07/14 SMI"	
























# 38 "/usr/include/sys/ioccom.h" 







































struct bsd_compat_ioctltab {
	int	cmd;	
	int	flag;	
	unsigned int	size;	
};

# 85 "/usr/include/sys/ioccom.h" 



# 41 "/usr/include/sys/filio.h" 2

# 44 "/usr/include/sys/filio.h" 



























# 73 "/usr/include/sys/filio.h" 



# 136 "/usr/ucbinclude/sys/ioctl.h" 2
# 1 "/usr/include/sys/sockio.h" 1































#ident	"@(#)sockio.h	1.11	93/10/26 SMI"	





# 1 "/usr/include/sys/ioccom.h" 1







# 87 "/usr/include/sys/ioccom.h" 

# 39 "/usr/include/sys/sockio.h" 2

# 42 "/usr/include/sys/sockio.h" 


























	


















							


















							

							

							

							

							



							





# 126 "/usr/include/sys/sockio.h" 



# 137 "/usr/ucbinclude/sys/ioctl.h" 2


# 21 "serv_p4.c" 2
# 23 "serv_p4.c" 


# 25 "serv_p4.c" 
# 31 "serv_p4.c" 


extern char *inet_ntoa ();  

# 44 "serv_p4.c" 


# 48 "serv_p4.c" 


# 62 "serv_p4.c" 


extern char *optarg;














extern char *crypt();
# 83 "serv_p4.c" 

extern int errno;

char tmpbuf[1024];
char *fromhost;

char logfile[1024];
FILE *logfile_fp;
int   logfile_fd;



int daemon_mode;
int daemon_port;
int daemon_pid;     
int stdfd_closed = 0;
int debug = 0;

char *this_username;
int this_uid;

void doit                    ();
void execute                 
();
int getline                  ();
void failure                 ();
void notice                  ();
int net_accept               ();
void net_setup_listener      ();
void net_setup_anon_listener ();
void error_check             ();
char *timestamp              ();

char *save_string            ();
static int connect_to_listener ();
void reaper ();
int main ();















int stdin_fd	= 0;
FILE *stdin_fp	= (&_iob[0]);
int stdout_fd	= 1;
FILE *stdout_fp	= (&_iob[1]);
int stderr_fd	= 2;
FILE *stderr_fp	= (&_iob[2]);

void reaper(sigval)
int sigval;
{
    int i;
    wait(&i);
}

int main(argc, argv)
int argc;
char **argv;
{
    int c;
    struct sockaddr_in name;
    int namelen;
    int pid;

    daemon_pid = getpid();

    if (getuid() == 0)
    {
	strcpy(logfile, "/usr/adm/serv_p4.log");
	daemon_port = 753;
    }
    else
    {
	sprintf(logfile, "P4Server.Log.%d", getpid());
	daemon_port = 0;
	debug = 1;
    }

    namelen = sizeof(name);
    if (getpeername(0, (struct sockaddr *) &name, &namelen) < 0)
	daemon_mode = 1;
    else
	daemon_mode = 0;
    
    while ((c = getopt(argc, argv, "Ddop:l:")) != (-1))
    {
	switch (c)
	{
	case 'D':
	    debug++;
	    break;
	    
	case 'd':
	    daemon_mode++;
	    break;
	    
	case 'o':
	    

	    daemon_mode++;
	    close(0);
	    close(1);
	    close(2);
	    stdfd_closed = 1;
	    pid = fork();
	    if (pid < 0) {
		
		exit(1);
	    }
	    else if (pid > 0) exit(0);
	    
	    daemon_pid = getpid();
	    break;
	case 'p':
	    daemon_port = atoi(optarg);
	    break;

	case 'l':
	    strcpy(logfile, optarg);
	    break;

	case '?':
	default:
	    fprintf((&_iob[2]), "\
Usage: %s [-d] [-D] [-p port] [-l logfile] [-o]\n",argv[0]);
	    exit(1);
	}
    }

    if ((logfile_fp = fopen(logfile, "a")) == 0)
    {
	if (getuid() != 0)
	{
	    fprintf( stdout_fp, "Cannot open logfile, disabling logging\n");
	    logfile_fp = fopen("/dev/null", "w");
	    
	}
	else
	{
	    fprintf( (&_iob[2]), "Cannot open logfile %s: %s\n",
		    logfile, strerror(errno));
	    exit(1);
	}
    }
    else {
	if (!stdfd_closed)
	    fprintf( stdout_fp, "Logging to %s\n", logfile);
    }
    logfile_fd = ( logfile_fp )->_file;

    setbuf(logfile_fp, 0);
    
    fprintf( logfile_fp, "%s pid=%d starting at %s, logfile fd is %d\n",
	    argv[0], getpid(), timestamp(), logfile_fd );
	     
    fflush( logfile_fp );

    if (stdfd_closed) {
	
	dup2( logfile_fd, 1 );
	dup2( logfile_fd, 2 );
    }

    if (daemon_mode)
    {
	int lfd, 
	    fd,  
            pid; 

	signal(18, reaper);

	if (daemon_port == 0)
	{
	    net_setup_anon_listener(2, &daemon_port, &lfd);
	}
	else
	{
	    net_setup_listener(2, daemon_port, &lfd);
	}

	fprintf( logfile_fp, "Listening on port %d\n", daemon_port );

	if ((debug || daemon_port != 753) && !stdfd_closed)
	    fprintf( stdout_fp, "Listening on %d\n", daemon_port);
	    
	if (!debug)
	{
	    




	    if (fork())
		exit(0);

	    for (fd = 0; fd < 10; fd++)
		if (fd != lfd && fd != logfile_fd)
		    close(fd);
	    

	    fd = open ("/dev/console", 2);
	    if (fd < 0)
		fd = open ("/dev/tty", 2);
	    if (fd < 0)
		fd = open ("/dev/null", 2);
# 302 "serv_p4.c" 

	    (void) dup2(0, 1);
	    (void) dup2(0, 2);

	    (void) setpgrp();
# 316 "serv_p4.c" 

	}

	while (1)
	{
	    
	    fd = net_accept(lfd);

	    pid = fork();

	    if (pid < 0)
	    {
		fprintf( logfile_fp, "Fork failed: %s\n",
			 strerror(errno));
		exit(pid);
	    }
	    if (pid == 0)
	    {
		fprintf( logfile_fp, 
		 "Started subprocess for connection at %s with pid %d\n", 
			 timestamp(), getpid() );

		(void) setpgrp();
# 343 "serv_p4.c" 
# 350 "serv_p4.c" 

		
		if (stdfd_closed) {
		    stdin_fp  = fdopen( fd, "r" );
		    stdout_fp = fdopen( fd, "a" );
		    stderr_fp = logfile_fp;
		    if (stdin_fp == 0 || stdout_fp == 0) {
			fprintf( logfile_fp, 
				 "Could not fdopen stdin or out\n" );
			exit(1);
		    }
		    close(lfd);
		    
		    doit(fd);
		}
		else {
		    close(0);
		    dup2(fd, 0);
		    close(1);
		    dup2(fd, 1);
		    close(2);
 		    dup2( logfile_fd, 2);
		    close(lfd);
		    
		    doit(0);
		}
		exit(0);
	    }
	    

	    

	    
	    close(fd);
	}
    }
    else
    {
	doit(0);
    }
	
}




void doit(fd)
int fd;
{
    struct sockaddr_in name;
    int namelen;
    struct hostent *hp;

    struct passwd *pw;
    char client_user[80], server_user[80];
    char pgm[1024], pgm_args[1024];
    char *user_home;
    int superuser;
    int valid;
    FILE *fp;
    int stdout_port;
    char stdout_port_str[16];

    char filename[1024], progline[1024];
    struct stat statbuf, statbuf_pgm, statbuf_apps_entry;

    this_uid = getuid();
    pw = getpwuid(this_uid);
    if (pw == 0)
    {
	fprintf( logfile_fp, "Cannot get pw entry for user %d\n", this_uid);
	exit(1);
    }
    this_username = save_string(pw->pw_name);

    if (this_uid != 0)
	fprintf( logfile_fp, "WARNING: Not run as root\n");

    setbuf(stdout_fp, 0);

    fprintf( logfile_fp, "Got connection at %s", timestamp());

    namelen = sizeof(name);

    if (getpeername(fd, (struct sockaddr *) &name, &namelen) != 0)
    {
	fprintf( logfile_fp, "getpeername failed: %s\n",
		 strerror(errno));
	exit(1);
    }

    fromhost = inet_ntoa(name.sin_addr);
    
    hp = gethostbyaddr((char *) &name.sin_addr,
		       sizeof(name.sin_addr),
		       (int) name.sin_family);
    if (hp == 0)
	{sprintf(tmpbuf, "Cannot get remote address for %s",  fromhost); failure(tmpbuf);};

    fromhost = hp->h_name;

    if (!getline(client_user, sizeof(client_user)))
	failure("No client user");

    if (!getline(server_user, sizeof(server_user)))
	failure("No server user");

    pw = getpwnam(server_user);
    if (pw == 0)
	{sprintf(tmpbuf, "No such user: %s\n",  server_user); failure(tmpbuf);};

    if (this_uid != 0 && this_uid != pw->pw_uid)
	
{sprintf(tmpbuf, "Server is not running as root. Only %s can start processes\n",  		 this_username); failure(tmpbuf);};

    user_home = pw->pw_dir;
    superuser = (pw->pw_uid == 0);

    fprintf( logfile_fp, "Starting ruserok at %s\n", timestamp() );
    valid = ruserok(fromhost, superuser, client_user, server_user);
    fprintf( logfile_fp, "Completed ruserok at %s\n", timestamp() );

    if (valid != 0)
    {
	char user_pw[80];
	char *xpw;
	
	fprintf( stdout_fp, "Password\n");
	if (!getline(user_pw, sizeof(user_pw)))
	    failure("No server user");

	xpw = crypt(user_pw, pw->pw_passwd);
	if (strcmp(pw->pw_passwd, xpw) != 0)
	    failure("Invalid password");

	fprintf( stdout_fp, "Proceed\n");
    }
    else
	fprintf( stdout_fp, "Proceed\n");
    
    fflush( stdout_fp );

    sprintf(tmpbuf, "authenticated client_id=%s server_id=%s\n",
	    client_user, server_user);
    notice(tmpbuf);

    







    
    if (!getline(pgm, sizeof(pgm)))
	failure("No pgm");

    




    if (strcmp( pgm, "%id" ) == 0) {
	fprintf( stdout_fp, "Port %d for client %s and server user %s\n", 
		daemon_port, client_user, server_user );
	exit(0);
    }
    else if (strcmp( pgm, "%run" ) == 0) {
	
	if (!getline(pgm, sizeof(pgm)))
	    failure("No pgm");
    }
    else if (strcmp( pgm, "%exit" ) == 0) {
	kill( daemon_pid, 2 );
	sleep(1);
	kill( daemon_pid, 3 );
	exit(1);
    }

    if (!getline(pgm_args, sizeof(pgm_args)))
	failure("No pgm args");

    {sprintf(tmpbuf, "got args %s",  pgm_args); notice(tmpbuf);};

    if (pgm[0] != '/')
	{sprintf(tmpbuf, "%s is not a full pathname",  pgm); failure(tmpbuf);};

    if (this_uid == 0)
    {
# 543 "serv_p4.c" 


	if (seteuid(pw->pw_uid) != 0)
	    {sprintf(tmpbuf, "seteuid failed: %s",  strerror(errno)); failure(tmpbuf);};


    }
    


    sprintf(filename, "%s/.p4apps", user_home);
    valid = 0;
    
# 573 "serv_p4.c" 
# 622 "serv_p4.c" 

    fp = fopen(filename,"r");


    if (fp != (FILE *) 0)
    {
	char *s1, *s2;
	
	if (fstat( (fp)->_file, &statbuf) != 0)
	    {sprintf(tmpbuf, "cannot stat %s",  filename); failure(tmpbuf);};
	
	if (statbuf.st_mode & 077)
	    failure(".p4apps readable by others");
	
	
	while (fgets(progline, sizeof(progline), fp) != 0)
	{
	    s1 = progline;
	    while (*s1 && ((_ctype + 1)[*s1] & 010))
		s1++;
	    if (*s1 == '\0' || *s1 == '#')
		continue;
	    
	    s2 = s1;
	    while (*s2 && !((_ctype + 1)[*s2] & 010))
		s2++;
	    *s2 = 0;
	    
	    if (strcmp(pgm, s1) == 0)
	    {
		valid = 1;
		break;
	    }
	    else
	    {
		if (stat(pgm, &statbuf_pgm) != 0)
		    continue;
		if (stat(s1, &statbuf_apps_entry) != 0)
		    continue;
		if (statbuf_pgm.st_ino == statbuf_apps_entry.st_ino)
		    valid = 1;
	    }
	}
	fclose(fp);

# 679 "serv_p4.c" 

	
    }

    if (!valid)
	{sprintf(tmpbuf, "Invalid program %s",  pgm); failure(tmpbuf);};
    
    if (stat(pgm, &statbuf) != 0)
	{sprintf(tmpbuf, "Cannot stat %s",  pgm); failure(tmpbuf);};

    if (!(statbuf.st_mode & 0111))
	{sprintf(tmpbuf, "Cannot execute %s",  pgm); failure(tmpbuf);};


    
    if (!getline(stdout_port_str, sizeof(stdout_port_str)))
	failure("No stdout");
    else
	stdout_port = atoi(stdout_port_str);

    {sprintf(tmpbuf, "got stdout_port %d",  stdout_port); notice(tmpbuf);};
    

    {sprintf(tmpbuf, "executing %s %s",  pgm, pgm_args); notice(tmpbuf);};

    execute(pgm, pgm_args, pw->pw_uid, stdout_port, hp);
	    
}

void execute(pgm, pgm_args, uid, stdout_port, hp)
char *pgm, *pgm_args;
int uid, stdout_port;
struct hostent *hp;
{
    int p[2];
    int rd, wr;
    int pid, n;
    char *args[256];
    int nargs;
    char *s, *end;
    int i;
    char buf[1024];
    int stdout_fd;
    char tempbuf[100];

    s = pgm_args;
    while (*s && ((_ctype + 1)[*s] & 010))
	s++;

    args[0] = pgm;

    nargs = 1;
    while (*s)
    {
	args[nargs] = s;

	while (*s && !((_ctype + 1)[*s] & 010))
	    s++;

	end = s;

	while (*s && ((_ctype + 1)[*s] & 010))
	    s++;

	*end = 0;
	nargs++;
	if (nargs + 1>= 256)
	    failure("Too many arguments to pgm");
    }

    args[nargs] = 0;

    if (pipe(p) != 0)
	{sprintf(tmpbuf, "Cannot create pipe: %s",  strerror(errno)); failure(tmpbuf);};

    rd = p[0];
    wr = p[1];

    if (fcntl(wr, 2, 1) != 0)
	{sprintf(tmpbuf, "fcntl F_SETFD failed: %s",  strerror(errno)); failure(tmpbuf);};

    if (this_uid == 0)
    {
# 765 "serv_p4.c" 

	if (seteuid(0) != 0)
	    {sprintf(tmpbuf, "cannot seteuid: %s",  strerror(errno)); failure(tmpbuf);};

	if (setuid(uid) != 0)
	    {sprintf(tmpbuf, "cannot setuid: %s",  strerror(errno)); failure(tmpbuf);};
# 774 "serv_p4.c" 


    }
    
    pid = fork();
    if (pid < 0)
	{sprintf(tmpbuf, "fork failed: %s",  strerror(errno)); failure(tmpbuf);};

    if (pid == 0)
    {
	close(rd);

	close(0);
	open("/dev/null", 0);

	stdout_fd = connect_to_listener(hp,stdout_port);
	{sprintf(tmpbuf, "stdout_fd=%d",  stdout_fd); notice(tmpbuf);};
	close(1);
	dup(stdout_fd);
	

	close(2);
	dup(stdout_fd);
	

	






	if (execv(pgm, args) != 0)
	{
	    sprintf(tmpbuf, "Exec failed: %s\n", strerror(errno));
	    write(wr, tmpbuf, strlen(tmpbuf));
	    exit(0);
	}
    }

    close(wr);

    if ((n = read(rd, buf, sizeof(buf))) > 0)
    {
	buf[n] = 0;
	s = strchr((buf),( '\n'));
	if (s)
	    *s = 0;
	
	{sprintf(tmpbuf, "child failed: %s",  buf); failure(tmpbuf);};
    }
    fprintf( stdout_fp, "Success: Child %d started\n", pid);
    {sprintf(tmpbuf, "Child %d started",  pid); notice(tmpbuf);};
}

int getline(str, len)
char *str;
int len;
{
    char *s;
    
    if (fgets(str,  len, stdin_fp) == 0)
	return 0;

    if ((s = strchr((str),( '\n'))) != 0)
	*s = 0;
    if ((s = strchr((str),( '\r'))) != 0)
	*s = 0;
    return 1;
}
    

void failure(s)
char *s;
{
    fprintf( stdout_fp, "Failure <%s>: %s\n", fromhost, s);
    fprintf( logfile_fp, "Failure <%s>: %s\n", fromhost, s);
    fflush(logfile_fp);
    exit(1);
}

void notice(s)
char *s;
{
    fprintf( logfile_fp, "Notice <%s>: %s\n", fromhost, s);
    fflush(logfile_fp);
}





int net_accept(skt)
int skt;
{
struct sockaddr_in from;
int fromlen;
int skt2;
int gotit;

    fromlen = sizeof(from);
    gotit = 0;
    while (!gotit)
    {
	skt2 = accept(skt, (struct sockaddr *) &from, &fromlen);
	if (skt2 == -1)
	{
	    if (errno == 4)
		continue;
	    else
		error_check(skt2, "net_accept accept");
	}
	else
	    gotit = 1;
    }

    return(skt2);
}

void net_setup_listener(backlog, port, skt)
int backlog, port, *skt;
{
int sinlen;
struct sockaddr_in sin, from;

    *skt = socket(2, 2, 0);

    error_check(*skt,"net_setup_anon_listener socket");

    sin.sin_family = 2;
    sin.sin_addr.S_un.S_addr = (u_long)0x00000000;
    sin.sin_port = (port);

    sinlen = sizeof(sin);

    error_check(bind(*skt,(struct sockaddr *) &sin,sizeof(sin)),
		   "net_setup_listener bind");


    error_check(listen(*skt, backlog), "net_setup_listener listen");
}

void net_setup_anon_listener(backlog, port, skt)
int backlog, *port, *skt;
{
int sinlen;
struct sockaddr_in sin, from;

    *skt = socket(2, 2, 0);

    error_check(*skt,"net_setup_anon_listener socket");

    sin.sin_family = 2;
    sin.sin_addr.S_un.S_addr = (u_long)0x00000000;
    sin.sin_port = (0);

    sinlen = sizeof(sin);

    error_check(bind(*skt,(struct sockaddr *) &sin,sizeof(sin)),
		   "net_setup_anon_listener bind");


    error_check(listen(*skt, backlog), "net_setup_anon_listener listen");

    getsockname(*skt, (struct sockaddr *) &sin, &sinlen);
    *port = (sin.sin_port);
}

void error_check(val, str)
int val;
char *str;
{
    if (val < 0)
    {
	fprintf(logfile_fp, "%s: %s\n",
		str,
		strerror(errno));
	exit(1);
    }
}

# 1 "/usr/include/time.h" 1










# 60 "/usr/include/time.h" 
# 72 "/usr/include/time.h" 
# 78 "/usr/include/time.h" 
# 84 "/usr/include/time.h" 
# 104 "/usr/include/time.h" 
# 116 "/usr/include/time.h" 
# 124 "/usr/include/time.h" 
# 151 "/usr/include/time.h" 
# 175 "/usr/include/time.h" 
# 178 "/usr/include/time.h" 
# 180 "/usr/include/time.h" 
# 212 "/usr/include/time.h" 
# 255 "/usr/include/time.h" 

# 956 "serv_p4.c" 2
char *timestamp()
{
    long clock;
    struct tm *tmp;

    clock = time(0L);
    tmp = localtime(&clock);
    return asctime(tmp);
}

char *save_string(s)
char *s;
{
    char *rc = (char *) malloc(strlen(s) + 1);
    strcpy(rc, s);
    return rc;
}

# 1081 "serv_p4.c" 


static int connect_to_listener(hp,stdout_port)
struct hostent *hp;
int stdout_port;
{
    int conn;
    int rc;
    struct sockaddr_in addr;

    conn = socket(2, 2, 0);
    if (conn < 0)
    {
	failure("connect_to_listener: socket failed");
    }

    addr.sin_family = hp->h_addrtype;
    addr.sin_port = (stdout_port);
    memcpy(( &addr.sin_addr),(hp->h_addr_list[0]),( hp->h_length));

    rc = connect(conn, (struct sockaddr *) & addr, sizeof(addr));
    if (rc < 0)
    {
	failure("connect_to_listener: connect failed");
    }

    return conn;
}

