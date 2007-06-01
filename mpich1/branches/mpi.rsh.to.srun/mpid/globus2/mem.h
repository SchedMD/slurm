#ifndef __glodev_mem__
#define __glodev_mem__

/*******************/
/* Mem mgmt macros */
/*******************/

#define g_malloc(Var, Type, Size) \
{ \
    size_t size = (Size); \
    if (size > 0) \
    { \
        if (((Var) = (Type) globus_libc_malloc (size)) == (Type) NULL) \
        { \
globus_libc_printf("FATAL ERROR: failed malloc %d bytes: file %s line %d\n", \
                   (int) size, __FILE__, __LINE__); \
            abort(); \
        } \
    } \
    else \
    { \
        (Var) = (Type) NULL; \
    } \
}

#define g_free(Ptr) \
{ \
    if ((Ptr) != NULL) {  globus_libc_free((void *)(Ptr));  } \
}

/**********************************************************************/
/* allocate memory and check the return pointer.  MPID_Abort if NULL */
#define g_malloc_chk(sz) g_malloc_chk_internal(sz, __FILE__, __LINE__)

extern void *
g_malloc_chk_internal (const size_t, const char *, const int);

#endif /* __glodev_mem__ */
