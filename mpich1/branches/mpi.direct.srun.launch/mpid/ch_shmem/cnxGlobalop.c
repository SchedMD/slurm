/*	$CHeader: cnxGlobalop.c 1.1 1995/11/08 13:59:57 $
 *	Copyright 1995  Convex Computer Corp.
 */
/* original obtained from Pat Estep does prefetch across hypernodes */

static void internal_pre_sum_double();
void read_prefetch_region();                /* prefetch 0, 1 & 2 */
extern int MPID_SHMEM_CNX_SAME_NODE;

void MPID_SHMEM_Sum_double(aa,bb,nd)
double *aa,*bb;
int nd;
{
    int n;
    int offset;
    int i;
    unsigned int a;
    unsigned int b;
    double *ax,*bx;

    if (MPID_SHMEM_CNX_SAME_NODE) {
        for ( i = 0;i<nd;i++) aa[i] += bb[i];
        return;
    }
    n= nd*sizeof(double);
    a = (unsigned int)aa; 
    b = (unsigned int)bb; 
    /* make life easy.  only work on chunks of at least 4 lines */
    if (n < 320) {
        read_prefetch_region(b & ~0x3f, n + (b & 0x3f));
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<nd;i++) ax[i] += bx[i];
        return;
    }
    /* force starting alignment */
    offset = (unsigned int)b & 0x3f;
    if (offset) {
        offset = 64 - offset;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<offset/8;i++) ax[i] += bx[i];
        b += offset;
        a += offset;
        n -= offset;
    }

    /* deal with case where n is not a multiple of 64 */
    offset = n & 0x3f;
    if (offset) {
        n &= ~0x3f;
        internal_pre_sum_double(a, b, n);
        b += n;
        a += n;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<offset/8;i++) ax[i] += bx[i];
        return;
    }

    /* n is a multiple of 64 */
    internal_pre_sum_double(a, b, n);
    return;
}

static void
internal_pre_sum_double(a, b, n)
unsigned int a,b;
int n;
{
    double *ax,*bx;
    int i;
    read_prefetch_region(b, 192);                /* prefetch 0, 1 & 2 */
    while (1) {
        read_prefetch_region(b+192, 64);        /* prefetch 3 */
        if (n == 256)
            break;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<8;i++) ax[i] += bx[i];
        b += 64; a += 64; n -= 64;
        read_prefetch_region(b+192, 64);        /* prefetch 0 */
        if (n == 256)
            break;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<8;i++) ax[i] += bx[i];
        b += 64; a += 64; n -= 64;
        read_prefetch_region(b+192, 64);        /* prefetch 1 */
        if (n == 256)
            break;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<8;i++) ax[i] += bx[i];
        b += 64; a += 64; n -= 64;
        read_prefetch_region(b+192, 64);        /* prefetch 2 */
        if (n == 256)
            break;
        ax = (double *)a; bx = (double *)b;
        for ( i = 0;i<8;i++) ax[i] += bx[i];
        b += 64; a += 64; n -= 64;
    }
    ax = (double *)a; bx = (double *)b;
    for ( i = 0;i<32;i++) ax[i] += bx[i];
}
