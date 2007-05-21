/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Anthony Chan
 */

#include "trace_impl.h"
#if defined( STDC_HEADERS ) || defined( HAVE_STDIO_H )
#include <stdio.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STRING_H )
#include <string.h>
#endif
#include "trace_API.h"

#define DRAW_TRUE    1
#define DRAW_FALSE   0

static
void bswp_byteswap( const int    Nelem,
                    const int    elem_sz,
                          char  *bytes );
static
void bswp_byteswap( const int    Nelem,
                    const int    elem_sz,
                          char  *bytes )
{
    char *bptr;
    char  btmp;
    int end_ii;
    int ii, jj;

    bptr = bytes;
    for ( jj = 0; jj < Nelem; jj++ ) {
         for ( ii = 0; ii < elem_sz/2; ii++ ) {
             end_ii          = elem_sz - 1 - ii;
             btmp            = bptr[ ii ];
             bptr[ ii ]      = bptr[ end_ii ];
             bptr[ end_ii ] = btmp;
         }
         bptr += elem_sz;
    }
}

typedef struct {
    TRACE_Category_head_t *hdr;
    char                  *legend;
    char                  *label;
    int                    num_methods;
    int                   *methods;
} DRAW_Category;

#define  MAX_COLNAMES    10
#define  MAX_NAME_LEN    128

typedef struct {
    int               num_rows;
    int               num_columns;
    char              title_name[ MAX_NAME_LEN ];
    char              column_names[ MAX_COLNAMES ][ MAX_NAME_LEN ];
    int              *elems;
    int               num_methods;
    int              *methods;
} DRAW_YCoordMap;

typedef struct {
    double            starttime;
    double            endtime;
    int               type_idx;
    int               num_info;
    char             *info;
    int               num_tcoords;
    double           *tcoords;
    int               num_ycoords;
    int              *ycoords;
} DRAW_Primitive;

typedef struct {
    double            starttime;
    double            endtime;
    int               type_idx;
    int               num_info;
    char             *info;
    int               num_primes;
    /* DRAW_Primitive  **primes; */
    char            **lines;   /* each line contains one DRAW_Primitive */
    int               idx2prime;
} DRAW_Composite;

#define  MAX_CATEGORIES  128
#define  MAX_LINE_LEN    1024

typedef struct _trace_file {
    FILE             *fd;
    char              line[ MAX_LINE_LEN ];
    int               max_types;
    int               num_types;
    DRAW_Category   **types;
    DRAW_YCoordMap   *ymap;
    DRAW_Primitive   *prime;
    DRAW_Composite   *cmplx;
} DRAW_File;

#define  MAX_LEGEND_LEN  128
#define  MAX_LABEL_LEN   512

static
DRAW_YCoordMap *YCoordMap_alloc( int Nrows, int Ncols, int Nmethods );
static
DRAW_YCoordMap *YCoordMap_alloc( int Nrows, int Ncols, int Nmethods )
{
    DRAW_YCoordMap   *map;

    map               = (DRAW_YCoordMap *) malloc( sizeof(DRAW_YCoordMap) );

    map->num_rows     = Nrows;
    map->num_columns  = Ncols;
    if ( Nrows * Ncols > 0 )
        map->elems    = (int *) malloc( Nrows * Ncols * sizeof( int ) );
    else
        map->elems    = NULL;

    map->num_methods  = Nmethods;
    if ( Nmethods > 0 )
        map->methods  = (int *) malloc( Nmethods * sizeof( int ) );
    else
        map->methods  = NULL;
    return map;
}

static
void YCoordMap_free( DRAW_YCoordMap *map );
static
void YCoordMap_free( DRAW_YCoordMap *map )
{
    if ( map != NULL ) {
        if ( map->methods != NULL ) {
            free( map->methods );
            map->methods = NULL;
        }
        if ( map->elems != NULL ) {
            free( map->elems );
            map->elems = NULL;
        }
        free( map );
    }
}

/* legend_len & label_len are lengths of the string withOUT counting NULL */
static
DRAW_Category *Category_alloc( int legend_len, int label_len, int Nmethods );
static
DRAW_Category *Category_alloc( int legend_len, int label_len, int Nmethods )
{
    DRAW_Category    *type;

    type              = (DRAW_Category *) malloc( sizeof(DRAW_Category) );
    type->hdr         = (TRACE_Category_head_t *)
                        malloc( sizeof(TRACE_Category_head_t) );

    if ( legend_len > 0 )
        type->legend  = (char *) malloc( (legend_len+1) * sizeof(char) );
    else
        type->legend  = NULL;

    if ( label_len > 0 )
        type->label   = (char *) malloc( (label_len+1) * sizeof(char) );
    else
        type->label   = NULL;

    type->num_methods  = Nmethods;
    if ( Nmethods > 0 )
        type->methods = (int *) malloc( Nmethods * sizeof( int ) );
    else
        type->methods = NULL;
    return type;
}

static
void Category_free( DRAW_Category *type );
static
void Category_free( DRAW_Category *type )
{
    if ( type != NULL ) {
        if ( type->methods != NULL ) {
            free( type->methods );
            type->methods = NULL;
        }
        if ( type->label != NULL ) {
            free( type->label );
            type->label = NULL;
        }
        if ( type->legend != NULL ) {
            free( type->legend );
            type->legend = NULL;
        }
        if ( type->hdr != NULL ) {
            free( type->hdr );
            type->hdr = NULL;
        }
        free( type );
    }
}

static
void Category_head_copy(       TRACE_Category_head_t *hdr_copy,
                         const TRACE_Category_head_t *hdr_copier );
static
void Category_head_copy(       TRACE_Category_head_t *hdr_copy,
                         const TRACE_Category_head_t *hdr_copier )
{
    if ( hdr_copy != NULL && hdr_copier != NULL ) {
        hdr_copy->index  = hdr_copier->index;
        hdr_copy->shape  = hdr_copier->shape;
        hdr_copy->red    = hdr_copier->red  ;
        hdr_copy->green  = hdr_copier->green;
        hdr_copy->blue   = hdr_copier->blue ;
        hdr_copy->alpha  = hdr_copier->alpha;
        hdr_copy->width  = hdr_copier->width;
    }
}

static
DRAW_Primitive *Primitive_alloc( int num_vtxs );
static
DRAW_Primitive *Primitive_alloc( int num_vtxs )
{
    DRAW_Primitive    *prime;

    prime               = (DRAW_Primitive *) malloc( sizeof(DRAW_Primitive) );
    prime->num_info     = 0;
    prime->info         = NULL;
    prime->num_tcoords  = num_vtxs;
    prime->tcoords      = (double *) malloc( num_vtxs * sizeof(double) );
    prime->num_ycoords  = num_vtxs;
    prime->ycoords      = (int *) malloc( num_vtxs * sizeof(int) );
    return prime;
}

static
void Primitive_free( DRAW_Primitive *prime );
static
void Primitive_free( DRAW_Primitive *prime )
{
    if ( prime != NULL ) {
        if ( prime->num_info > 0 && prime->info != NULL ) {
            free( prime->info );
            prime->num_info  = 0;
            prime->info      = NULL;
        }
        if ( prime->num_tcoords > 0 && prime->tcoords != NULL ) {
            free( prime->tcoords );
            prime->num_tcoords  = 0;
            prime->tcoords      = NULL;
        }
        if ( prime->num_ycoords > 0 && prime->ycoords != NULL ) {
            free( prime->ycoords );
            prime->num_ycoords  = 0;
            prime->ycoords      = NULL;
        }
        free( prime );
    }
}

static
DRAW_Composite *Composite_alloc( int Nprimes );
static
DRAW_Composite *Composite_alloc( int Nprimes )
{
    DRAW_Composite    *cmplx;
    int                idx;

    cmplx               = (DRAW_Composite *) malloc( sizeof(DRAW_Composite) );
    cmplx->num_info     = 0;
    cmplx->info         = NULL;
    cmplx->num_primes   = Nprimes;
    /*
    cmplx->primes       = (DRAW_Primitive **)
                          malloc( Nprimes * sizeof(DRAW_Primitive *) );
    */
    cmplx->lines        = (char **) malloc( Nprimes * sizeof(char *) );
    for ( idx = 0; idx < Nprimes; idx++ )
        cmplx->lines[ idx ] = (char *) malloc( MAX_LINE_LEN * sizeof(char) );
    return cmplx;
}

/*
int Composite_setPrimitiveAt( DRAW_Composite *cmplx,
                              int idx, DRAW_Primitive *prime )
{
    if ( cmplx == NULL )
        return DRAW_FALSE;
    if ( idx < 0 || idx >= cmplx->num_primes )
        return DRAW_FALSE;
    cmplx->primes[ idx ] = prime;
    return DRAW_TRUE;
}
*/

static
void Composite_free( DRAW_Composite *cmplx );
static
void Composite_free( DRAW_Composite *cmplx )
{
    int idx;
    if ( cmplx != NULL ) {
        if ( cmplx->num_info > 0 && cmplx->info != NULL ) {
            free( cmplx->info );
            cmplx->num_info  = 0;
            cmplx->info      = NULL;
        }
        for ( idx = 0; idx < cmplx->num_primes; idx++ )
            if ( cmplx->lines[ idx ] != NULL ) {
                free( cmplx->lines[ idx ] ); 
                cmplx->lines[ idx ] = NULL;
            }
            /*
            if ( cmplx->primes[ idx ] != NULL ) {
                Primitive_free( cmplx->primes[ idx ] );
                cmplx->primes[ idx ] = NULL;
            }
            */
        cmplx->num_primes = 0;
        free( cmplx );
    }
}


/*  Actual TRACE-API implementation  */

TRACE_EXPORT
char *TRACE_Get_err_string( int ierr )
{
    switch ( ierr ) {
        case 0:
            return "Usage: executable_name ASCII_drawable_filename";
        case 1:
            return "Error: fopen() fails!";
        case 10:
            return "Maximum of Categories has been reached.";
        case 20:
            return "Cannot locate CATEGORY in the internal table.";
        case 21:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected before writing Legend.\n";
        case 22:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected after writing Legend.\n";
        case 23:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected before writing Label.\n";
        case 24:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected after writing Label.\n";
        case 25:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected before writing MethodIDs.\n";
        case 26:
            return "TRACE_Get_next_category(): Memory violation "
                   "detected after writing MethodIDs.\n";
        case 30:
            return "Cannot locate PRIMITIVE in the internal table.";
        case 31:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected before writing ByteInfo.\n";
        case 32:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected after writing ByteInfo.\n";
        case 33:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected before writing Time coordinates.\n";
        case 34:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected after writing Time coordinates.\n";
        case 35:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected before writing Yaxis coordinates.\n";
        case 36:
            return "TRACE_Get_next_primitive(): Memory violation "
                   "detected after writing Yaxis coordinates.\n";
        case 40:
            return "Cannot locate COMPOSITE in the internal table.";
        case 41:
            return "TRACE_Get_next_composite(): Memory violation "
                   "detected before writing ByteInfo.\n";
        case 42:
            return "TRACE_Get_next_composite(): Memory violation "
                   "detected after writing ByteInfo.\n";
        case 49:
            return "TRACE_Peek_next_composite(): Unexpected EOF detected.";
        case 60:
            return "Cannot locate YCOORDMAP in the internal table.";
        case 61:
            return "TRACE_Peek_next_ycoordmap(): Inconsistency detected "
                   "in the number of methods from input text file.\n";
        case 63:
            return "TRACE_Get_next_ycoordmap(): Memory violation "
                   "detected before writing Yaxis coordinate map.\n";
        case 64:
            return "TRACE_Get_next_ycoordmap(): Memory violation "
                   "detected after writing Yaxis coordinate map.\n";
        case 65:
            return "TRACE_Get_next_ycoordmap(): Memory violation "
                   "detected before writing MethodIDs.\n";
        case 66:
            return "TRACE_Get_next_ycoordmap(): Memory violation "
                   "detected after writing Methods.\n";
        default:
            return "Unknown Message ID ";
    }
}

TRACE_EXPORT
int TRACE_Open( const char filespec[], TRACE_file *fp )
{
    TRACE_file     tr;

    if ( strncmp( filespec, "-h", 2 ) == 0 ) {
        *fp  = NULL;
        return 0;
    }

    tr             = (TRACE_file) malloc( sizeof(struct _trace_file) );
    tr->fd         = fopen( filespec, "r" );
    if ( tr->fd == NULL ) {
        *fp  = NULL;
        return 1;
    }

    tr->max_types  = MAX_CATEGORIES;
    tr->num_types  = 0;
    tr->types      = (DRAW_Category **) malloc( tr->max_types
                                              * sizeof(DRAW_Category *) );
    tr->ymap       = NULL; 
    tr->prime      = NULL;
    tr->cmplx      = NULL;
	*fp            = tr;
    return 0;
}

TRACE_EXPORT
int TRACE_Close( TRACE_file *fp )
{
    TRACE_file     tr;
    int            idx;

    tr             = *fp;
    if ( tr->types != NULL ) {
        for ( idx = 0; idx < tr->num_types; idx++ ) {
             Category_free( tr->types[ idx ] );
             tr->types[ idx ] = NULL;
        }
        tr->num_types = 0;
        free( tr->types );
    }
    if ( tr->prime != NULL ) {
        Primitive_free( tr->prime );
        tr->prime = NULL;
    }
    if ( tr->cmplx != NULL ) {
        Composite_free( tr->cmplx );
        tr->cmplx = NULL;
    }
    if ( tr->fd != NULL )
        fclose( tr->fd );
    *fp = NULL;
    return 0;
}

TRACE_EXPORT
int TRACE_Peek_next_kind( const TRACE_file fp, TRACE_Rec_Kind_t *next_kind )
{
    while ( fgets( fp->line, MAX_LINE_LEN, fp->fd ) != NULL ) {
        if ( strncmp( fp->line, "Category", 8 ) == 0 ) {
            *next_kind = TRACE_CATEGORY;
            return 0;
        }
        else if ( strncmp( fp->line, "YCoordMap", 9 ) == 0 ) {
            *next_kind = TRACE_YCOORDMAP;
            return 0;
        }
        else if ( strncmp( fp->line, "Primitive", 9 ) == 0 ) {
            *next_kind = TRACE_PRIMITIVE_DRAWABLE;
            return 0;
        }
        else if ( strncmp( fp->line, "Composite", 9 ) == 0 ) {
            *next_kind = TRACE_COMPOSITE_DRAWABLE;
            return 0;
        }
    }
    *next_kind = TRACE_EOF;
    return 0;
}

TRACE_EXPORT
int TRACE_Peek_next_category( const TRACE_file fp,
                              int *num_legend, int *num_label,
                              int *num_methods )
{
    DRAW_Category  *type;
    TRACE_Category_head_t *hdr;
    char            typename[10];
    int             type_idx;
    int             line_pos;
    char           *newline;
    char           *info_A, *info_B;

    char            category_format[] = "%s index=%d name=%s topo=%s "
                                        "color=(%d,%d,%d,%d,%s width=%d %n";
    char            topology[10];
    int             red, green, blue, alpha;
    char            mody[10];
    int             width;
    char            legend[MAX_LEGEND_LEN];
    int             legend_len;
    char            label[MAX_LABEL_LEN];
    int             label_len;
    char            str4methods[MAX_LABEL_LEN];
    int             methods_len;

    sscanf( fp->line, category_format, typename, &type_idx,
            legend, topology, &red, &green, &blue, &alpha,
            mody, &width, &line_pos );
#if defined( DEBUG )
    printf( "%s %d %s %s (%d,%d,%d,%d) %d ", typename, type_idx,
            legend, topology, red, green, blue, alpha, width );
    fflush( NULL );
#endif
    legend_len  = strlen( legend );
    newline     = (char *) (fp->line + line_pos);


    /* Set InfoKeys */
    info_A = NULL;
    info_B = NULL;
    if (    ( info_A = strstr( newline, "< " ) ) != NULL
         && ( info_B = strstr( info_A, " >" ) ) != NULL ) {
        info_A = (char *) (info_A + 2);
        sprintf( info_B, "%c", '\0' );
        strncpy( label, info_A, MAX_LABEL_LEN );
#if defined( DEBUG )
        printf( "<%s>", label );
        fflush( NULL );
#endif
        newline = (char *) (info_B + 2);
        label_len = strlen( label );
    }
    else
        label_len = 0;
#if defined( DEBUG )
    printf( "\n" );
    fflush( NULL );
#endif

    /* Set Methods */
    info_A = NULL;
    info_B = NULL;
    if (    ( info_A = strstr( newline, "{ " ) ) != NULL
         && ( info_B = strstr( info_A, " }" ) ) != NULL ) {
        info_A = (char *) (info_A + 2);
        sprintf( info_B, "%c", '\0' );
        strncpy( str4methods, info_A, MAX_LABEL_LEN );
#if defined( DEBUG )
        printf( "{%s}", str4methods );
        fflush( NULL );
#endif
        newline = (char *) (info_B + 2);
        /* Assume only 1 method ID */
        methods_len = 1;
    }
    else
        methods_len = 0;
#if defined( DEBUG )
    printf( "\n" );
    fflush( NULL );
#endif

    type = Category_alloc( legend_len, label_len, methods_len );
    hdr  = type->hdr;

    /* Set the Output Parameters of the routine */
    hdr->index = type_idx;
    if ( strncmp( topology, "Event", 5 ) == 0 )
        hdr->shape = TRACE_SHAPE_EVENT;
    else if ( strncmp( topology, "State", 5 ) == 0 )
        hdr->shape = TRACE_SHAPE_STATE;
    else if ( strncmp( topology, "Arrow", 5 ) == 0 )
        hdr->shape = TRACE_SHAPE_ARROW;
    else {
        fprintf( stderr, "TRACE_Peek_next_category(): Unknown shape.\n" );
        hdr->shape = -1;
    }
    hdr->red    = red;
    hdr->green  = green;
    hdr->blue   = blue;
    hdr->alpha  = alpha;
    hdr->width  = width;

    if ( legend_len > 0 )
        strcpy( type->legend, legend );
    if ( label_len > 0 )
        strcpy( type->label, label );
    if ( methods_len > 0 )
        /* Assume 1 method ID */
        type->methods[ 0 ] = atoi( str4methods );

    if ( fp->num_types >= fp->max_types )
        return 10;
    fp->types[ fp->num_types ] = type;

    *num_legend  = legend_len;
    *num_label   = label_len;
    *num_methods = methods_len;

    return 0;
}

TRACE_EXPORT
int TRACE_Get_next_category( const TRACE_file fp,
                             TRACE_Category_head_t *head,
                             int *num_legend, char legend_base[],
                             int *legend_pos, const int legend_max,
                             int *num_label, char label_base[],
                             int *label_pos, const int label_max,
                             int *num_methods, int method_base[],
                             int *method_pos, const int method_max )
{
    DRAW_Category  *type;
    int             legend_len, label_len;

    type  = fp->types[ fp->num_types ];
    if ( type == NULL ) {
        fprintf( stderr, "TRACE_Get_next_category(): Cannot locate "
                         "current category in Category Table.\n" );
        return 20;
    }
    (fp->num_types)++;

    /* Copy current Category_head_t to the caller's allocated buffer */
    Category_head_copy( head, type->hdr );

    if ( type->legend != NULL ) {
        legend_len = strlen( type->legend );
        if ( legend_len > 0 ) {
            if ( *legend_pos >= legend_max )
                return 21;
            memcpy( &(legend_base[ *legend_pos ]), type->legend,
                    sizeof( char ) * legend_len );
            *num_legend  = legend_len;
            *legend_pos += *num_legend;
            if ( *legend_pos > legend_max )
                return 22;
        }
    }

    if ( type->label != NULL ) {
        label_len = strlen( type->label );
        if ( label_len > 0 ) {
            if ( *label_pos >= label_max )
                return 23;
            memcpy( &(label_base[ *label_pos ]), type->label,
                    sizeof( char ) * label_len );
            *num_label  = label_len;
            *label_pos += *num_label;
            if ( *label_pos > label_max )
                return 24;
        }
    }

    if ( type->num_methods > 0 ) {
        if ( *method_pos >= method_max )
            return 25;
        memcpy( &(method_base[ *method_pos ]), type->methods,
                sizeof( int ) * type->num_methods );
        *num_methods = type->num_methods;
        *method_pos += *num_methods;
        if ( *method_pos > method_max )
            return 26;
    }

    return 0;
}

TRACE_EXPORT
int TRACE_Peek_next_ycoordmap( TRACE_file fp,
                               int *num_rows, int *num_columns,
                               int *max_column_name,
                               int *max_title_name,
                               int *num_methods )
{
    DRAW_YCoordMap *ymap;
    char            mapname[10];
    int             Nrows, Ncols, Nmeths;
    int             line_pos;

    char           *newline;
    char            ymap_sz_fmt[] = "%s Nrows=%d Ncolumns=%d Nmethods=%d %n";
    char            title_fmt[] = "title=%s colnames=< %n";
    int            *map_elems;
    int             max_colnames;

    char           *info_A, *info_B;
    char            str4methods[MAX_LABEL_LEN];
    int             methods_len;

    int             icol, irow, idx;

    sscanf( fp->line, ymap_sz_fmt,
            mapname, &Nrows, &Ncols, &Nmeths, &line_pos );
#if defined( DEBUG )
    printf( "%s(%d,%d,%d)] :\n", mapname, Nrows, Ncols, Nmeths );
#endif
    newline = (char *)(fp->line+line_pos);

    ymap = YCoordMap_alloc( Nrows, Ncols, Nmeths );
    sscanf( newline, title_fmt, ymap->title_name, &line_pos );
    newline = (char *) (newline+line_pos);
#if defined( DEBUG )
    printf( "Title=%s \nColumnLabels=< LineID -> ", ymap->title_name );
#endif

    max_colnames = 0;
    for ( icol = 0; icol < Ncols-1; icol++ ) {
        sscanf( newline, "%s %n", ymap->column_names[icol], &line_pos );
        newline = (char *) (newline+line_pos);
#if defined( DEBUG )
        printf( "%s ", ymap->column_names[icol] );
#endif
        if ( max_colnames < strlen( ymap->column_names[icol] ) + 1 )
            max_colnames = strlen( ymap->column_names[icol] ) + 1;
    }
    newline += 2;
#if defined( DEBUG )
    printf( ">\n" );
#endif

    map_elems = ymap->elems;
    idx = 0;
    for ( irow = 0; irow < Nrows; irow++ ) {
        sscanf( newline, "( %d %n", &map_elems[ idx++ ], &line_pos ); 
        newline = (char *) (newline+line_pos);
#if defined( DEBUG )
        printf( "%d -> ", map_elems[ idx-1 ] );
#endif
        for ( icol = 1; icol < Ncols-1; icol++ ) {
            sscanf( newline, "%d %n", &map_elems[ idx++ ], &line_pos );
            newline = (char *) (newline+line_pos);
#if defined( DEBUG )
            printf( "%d ", map_elems[ idx-1] );
#endif
        }
        sscanf( newline, "%d ) %n", &map_elems[ idx++ ], &line_pos ); 
        newline = (char *) (newline+line_pos);
#if defined( DEBUG )
        printf( "%d\n", map_elems[ idx-1 ] );
#endif
    }

    /* Set Methods */
    info_A = NULL;
    info_B = NULL;
    if (    ( info_A = strstr( newline, "{ " ) ) != NULL
         && ( info_B = strstr( info_A, " }" ) ) != NULL ) {
        info_A = (char *) (info_A + 2);
        sprintf( info_B, "%c", '\0' );
        strncpy( str4methods, info_A, MAX_LABEL_LEN );
#if defined( DEBUG )
        printf( "{%s}", str4methods );
#endif
        newline = (char *) (info_B + 2);
        /* Assume only 1 method ID */
        methods_len = 1;
    }
    else
        methods_len = 0;
#if defined( DEBUG )
    printf( "\n" );
#endif

    if ( methods_len != Nmeths ) {
        fprintf( stderr, "TRACE_Peek_next_ycoordmap(): The number of methods "
                         "defined is %d and number read is %d\n",
                         Nmeths, methods_len );
        return 61;
    }

    if ( methods_len > 0 ) {
        /* Assume 1 method ID */
        ymap->methods[ 0 ] = atoi( str4methods );
    }

    *num_rows         = ymap->num_rows;
    *num_columns      = ymap->num_columns;
    *max_column_name  = max_colnames;
    *max_title_name   = strlen( ymap->title_name ) + 1;
    *num_methods      = methods_len;

    /* Free the previous allocated YCoordMap stored at TRACE_file */
    YCoordMap_free( fp->ymap );
    fp->ymap = ymap;

    return 0;
}

TRACE_EXPORT
int TRACE_Get_next_ycoordmap( TRACE_file fp,
                              char *title_name,
                              char **column_names,
                              int *coordmap_sz, int coordmap_base[],
                              int *coordmap_pos, const int coordmap_max,
                              int *num_methods, int method_base[],
                              int *method_pos, const int method_max )
{
    DRAW_YCoordMap  *ymap;
    int              icol;

    if ( fp->ymap == NULL ) {
        fprintf( stderr, "TRACE_Get_next_ycoordmap(): Cannot locate "
                         "YCoordMap in TRACE_file.\n" );
        return 60;
    }
    ymap = fp->ymap;

    strcpy( title_name, ymap->title_name );
    /*
    fprintf( stderr, "strlen(%s) = %d\n", title_name, strlen(title_name) );
    fflush( stderr );
    */
    for ( icol = 0; icol < ymap->num_columns - 1; icol++ ) {
        /*
        fprintf( stderr, "strlen(%s) = %d\n", ymap->column_names[icol],
                         strlen(ymap->column_names[icol]) );
        fflush( stderr );
        */
        strcpy( column_names[icol], ymap->column_names[icol] );
    }

    if ( *coordmap_pos >= coordmap_max )
        return 63;
    memcpy( &(coordmap_base[ *coordmap_pos ]), ymap->elems,
            sizeof( int ) * ymap->num_rows * ymap->num_columns );
    *coordmap_sz   = ymap->num_rows * ymap->num_columns;
    *coordmap_pos += *coordmap_sz;
    if ( *coordmap_pos > coordmap_max )
        return 64;

    if ( ymap->num_methods > 0 ) {
        if ( *method_pos >= method_max )
            return 65;
        memcpy( &(method_base[ *method_pos ]), ymap->methods,
                sizeof( int ) * ymap->num_methods );
        *num_methods = ymap->num_methods;
        *method_pos += *num_methods;
        if ( *method_pos > method_max )
            return 66;
    }

    return 0;
}

#define MAX_VERTICES 10

TRACE_EXPORT
int TRACE_Peek_next_primitive( const  TRACE_file fp,
                               double *start_time, double *end_time,
                               int *num_tcoords, int *num_ycoords,
                               int *num_bytes )
{
    DRAW_Category  *type;
    DRAW_Primitive *prime;
    DRAW_Composite *cmplx;
    char            typename[10];
    int             type_idx;
    int             line_pos;
    char           *linebuf;
    char           *newline;
    char           *info_A, *info_B;

    char            coord_format[] = "(%lf, %d) %n";
    char            primitive_format[] = "%s TimeBBox(%lf,%lf) Category=%d %n";
    double          starttime, endtime;
    double          tcoords[ MAX_VERTICES ];
    int             ycoords[ MAX_VERTICES ];
    int             num_vertices;
    int             infovals[2];
    int             idx;

    /* Check if this Primitive is part of the Composite */
    if ( fp->cmplx != NULL ) {
        cmplx = fp->cmplx;
        if ( cmplx->idx2prime < cmplx->num_primes ) {
            linebuf = cmplx->lines[ cmplx->idx2prime ];
            cmplx->idx2prime++;
        }
        else {
            /* Free the composite and set it to NULL to minimize the overhead */
            Composite_free( fp->cmplx );
            fp->cmplx = NULL;
            linebuf = fp->line;
        }
    }
    else
        linebuf = fp->line;

    sscanf( linebuf, primitive_format, typename, &starttime, &endtime,
            &type_idx, &line_pos );
#if defined( DEBUG )
    printf( "%s %lf %lf %d ", typename, starttime, endtime, type_idx );
#endif
    newline = (char *) (linebuf+line_pos);

    num_vertices = 0;
    while ( newline[0] == '(' ) {
        sscanf( newline, coord_format,
                &tcoords[num_vertices], &ycoords[num_vertices], &line_pos );
#if defined( DEBUG )
        printf( "(%lf, %d) ", tcoords[num_vertices], ycoords[num_vertices] );
#endif
        newline = (char *) (newline+line_pos);
        num_vertices++;
    }

    /* Search for the valid Category in the category table */
    type = NULL;
    for ( idx = 0; idx < fp->num_types; idx++ ) {
        if ( fp->types[ idx ]->hdr->index == type_idx ) {
            type = fp->types[ idx ];
            break;
        }
    }
/*
    if ( type == NULL ) {
        fprintf( stderr, "TRACE_Peek_next_primitive(): Cannot locate "
                         "CATEGORY in catgeory table.\n" );
        return 20;
    }
*/

    *num_bytes = 0;
    info_A = NULL;
    info_B = NULL;
    if (    ( info_A = strstr( newline, "< " ) ) != NULL
         && ( info_B = strstr( info_A, " >" ) ) != NULL
         && type != NULL ) {
        info_A = (char *) (info_A + 2);
        sprintf( info_B, "%c", '\0' );
        sscanf( info_A, type->label, &infovals[0], &infovals[1] );
#if defined( DEBUG )
        printf( "<[0]=%d [1]=%d>", infovals[0], infovals[1] );
#endif
        *num_bytes = 8;
    }
#if defined( DEBUG )
    printf( "\n" );
#endif

    *start_time   = starttime;
    *end_time     = endtime;
    *num_tcoords  = num_vertices;
    *num_ycoords  = num_vertices;

    /* Allocate a new Primitive */
    prime = Primitive_alloc( num_vertices );
    prime->starttime = *start_time;
    prime->endtime   = *end_time;
    prime->type_idx  = type_idx;
    if ( *num_bytes > 0 ) {
        prime->num_info  = *num_bytes;
        prime->info      = (char *) malloc( prime->num_info * sizeof(char) );
        memcpy( prime->info, infovals, prime->num_info );
#if ! defined( WORDS_BIGENDIAN )
        bswp_byteswap( 2, sizeof( int ), prime->info );
#endif
    }
    prime->num_tcoords = num_vertices;
    prime->num_ycoords = num_vertices;
    for ( idx = 0; idx < num_vertices; idx++ ) {
        prime->tcoords[ idx ] = tcoords[ idx ];
        prime->ycoords[ idx ] = ycoords[ idx ];
    }

    /* Free the previous allocated Primitive stored at TRACE_file */
    Primitive_free( fp->prime );
    fp->prime = prime;

    return 0;
}

TRACE_EXPORT
int TRACE_Get_next_primitive( const TRACE_file fp,
                              int *category_index,
                              int *num_tcoords, double tcoord_base[],
                              int *tcoord_pos, const int tcoord_max,
                              int *num_ycoords, int ycoord_base[],
                              int *ycoord_pos, const int ycoord_max,
                              int *num_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{
    DRAW_Primitive *prime;

    if ( fp->prime == NULL ) {
        fprintf( stderr, "TRACE_Get_next_primitive(): Cannot locate "
                         "Primitive in TRACE_file.\n" );
        return 30;
    }
    prime = fp->prime;
    *category_index = prime->type_idx;

    if ( prime->num_info > 0 ) {
        if ( *byte_pos >= byte_max )
            return 31;
        memcpy( &(byte_base[ *byte_pos ]), prime->info,
                sizeof( char ) * prime->num_info );
        *num_bytes = prime->num_info;
        *byte_pos += *num_bytes;
        if ( *byte_pos > byte_max )
            return 32;
    }

    if ( *tcoord_pos >= tcoord_max )
        return 33;
    memcpy( &(tcoord_base[ *tcoord_pos ]), prime->tcoords,
            sizeof( double ) * prime->num_tcoords );
    *num_tcoords = prime->num_tcoords;
    *tcoord_pos += *num_tcoords;
    if ( *tcoord_pos > tcoord_max )
        return 34;

    if ( *ycoord_pos >= ycoord_max )
        return 35;
    memcpy( &(ycoord_base[ *ycoord_pos ]), prime->ycoords,
            sizeof( int ) * prime->num_ycoords );
    *num_ycoords = prime->num_ycoords;
    *ycoord_pos += *num_ycoords;
    if ( *ycoord_pos > ycoord_max )
        return 36;

    return 0;
}

TRACE_EXPORT
int TRACE_Peek_next_composite( const TRACE_file fp,
                               double *start_time, double *end_time,
                               int *num_primitives, int *num_bytes )
{
    DRAW_Category  *type;
    DRAW_Composite *cmplx;
    char            typename[10];
    int             type_idx;
    int             line_pos;
    char           *newline;
    char           *info_A, *info_B;

    char            composite_format[] = "%s TimeBBox(%lf,%lf) Category=%d "
                                         "NumPrimes=%d %n";
    double          starttime, endtime;
    int             num_primes;
    int             infovals[2];
    int             idx;

    sscanf( fp->line, composite_format, typename, &starttime, &endtime,
            &type_idx, &num_primes, &line_pos );
#if defined( DEBUG )
    printf( "%s %lf %lf %d %d ", typename, starttime, endtime,
            type_idx, num_primes );
#endif
    newline = (char *) (fp->line+line_pos);

    /* Search for the valid Category in the category table */
    type = NULL;
    for ( idx = 0; idx < fp->num_types; idx++ ) {
        if ( fp->types[ idx ]->hdr->index == type_idx ) {
            type = fp->types[ idx ];
            break;
        }
    }
/*
    if ( type == NULL ) {
        fprintf( stderr, "TRACE_Peek_next_composite(): Cannot locate "
                         "CATEGORY in catgeory table.\n" );
        return 20;
    }
*/

    *num_bytes = 0;
    info_A = NULL;
    info_B = NULL;
    if (    ( info_A = strstr( newline, "< " ) ) != NULL
         && ( info_B = strstr( info_A, " >" ) ) != NULL
         && type != NULL ) {
        info_A = (char *) (info_A + 2);
        sprintf( info_B, "%c", '\0' );
        sscanf( info_A, type->label, &infovals[0], &infovals[1] );
#if defined( DEBUG )
        printf( "<[0]=%d [1]=%d>", infovals[0], infovals[1] );
#endif
        *num_bytes = 8;
    }
#if defined( DEBUG )
    printf( "\n" );
#endif

    *start_time      = starttime;
    *end_time        = endtime;
    *num_primitives  = num_primes;

    /* Allocate a new Composite */
    cmplx = Composite_alloc( num_primes );
    cmplx->starttime = *start_time;
    cmplx->endtime   = *end_time;
    cmplx->type_idx  = type_idx;
    if ( *num_bytes > 0 ) {
        cmplx->num_info  = *num_bytes;
        cmplx->info      = (char *) malloc( cmplx->num_info * sizeof(char) );
        memcpy( cmplx->info, infovals, cmplx->num_info );
#if ! defined( WORDS_BIGENDIAN )
        bswp_byteswap( 2, sizeof( int ), cmplx->info );
#endif
    }
    cmplx->num_primes = num_primes;

    /* Fetch next "num_primes" primitive record from file to be parsed later */
    for ( idx = 0; idx < cmplx->num_primes; idx++ ) {
        if ( fgets( cmplx->lines[ idx ], MAX_LINE_LEN, fp->fd ) == NULL ) {
            fprintf( stderr, "TRACE_Peek_next_composite(): Unexpected EOF "
                             "while reading Composite's Primitive[%d].\n",
                             idx );
            return 49;
        }
    }
    /* Set up accessment index to the primitive line records */
    cmplx->idx2prime = 0;

    /* Free the previous allocated Composite stored at TRACE_file */
    Composite_free( fp->cmplx );
    fp->cmplx = cmplx;

    return 0;
}

TRACE_EXPORT
int TRACE_Get_next_composite( const TRACE_file fp,
                              int *category_index,
                              int *num_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{
    DRAW_Composite  *cmplx;

    if ( fp->cmplx == NULL ) {
        fprintf( stderr, "TRACE_Get_next_composite(): Cannot locate "
                         "Composite in TRACE_file.\n" );
        return 40;
    }
    cmplx = fp->cmplx;
    *category_index = cmplx->type_idx;

    if ( cmplx->num_info > 0 ) {
        if ( *byte_pos >= byte_max )
            return 41;
        memcpy( &(byte_base[ *byte_pos ]), cmplx->info,
                sizeof( char ) * cmplx->num_info );
        *num_bytes = cmplx->num_info;
        *byte_pos += *num_bytes;
        if ( *byte_pos > byte_max )
            return 42;
    }

    return 0;
}
