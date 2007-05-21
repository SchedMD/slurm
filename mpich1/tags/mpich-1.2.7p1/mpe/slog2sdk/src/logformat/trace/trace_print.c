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

#define MAX_FILESPEC_LEN  512
#define MAX_LABEL_LEN     1024
#define MAX_LEGEND_LEN    128
#define MAX_TIME_COORDS   10
#define MAX_VERT_COORDS   10
#define MAX_INFO_LEN      128
#define MAX_METHODS       10

int main( int argc, char **argv )
{
    TRACE_file               tf;
    TRACE_Rec_Kind_t         next_kind;
    char                     filespec[ MAX_FILESPEC_LEN ];
    int                      ierr;

    TRACE_Category_head_t    type_hdr;
    int                      legend_sz, label_sz, methodIDs_sz;
    char                     legend_base[ MAX_LEGEND_LEN ];
    char                     label_base[ MAX_LABEL_LEN ];
    int                      methodID_base[ MAX_METHODS ];
    int                      legend_pos, label_pos, methodID_pos;

    int                      nrows, ncolumns;
    int                      max_column_name, max_title_name;
    char                    *title_name;
    char                   **column_names;
    int                      coordmap_sz, coordmap_max;
    int                     *coordmap_base;
    int                      coordmap_pos;

    double                   stime, etime;
    int                      tcoord_sz, ycoord_sz, info_sz;
    double                   tcoord_base[ MAX_TIME_COORDS ];
    int                      ycoord_base[ MAX_VERT_COORDS ];
    char                     info_base[ MAX_INFO_LEN ];
    int                      tcoord_pos, ycoord_pos, info_pos;

    double                   cmplx_stime, cmplx_etime;
    int                      num_primes;
    int                      cmplx_type_idx;
    int                      cmplx_info_sz;
    char                     cmplx_info_base[ MAX_INFO_LEN ];
    int                      cmplx_info_pos;

    int                      idx, idx2prime, type_idx;
    int                      irow, icol;
    long                     obj_no;

/*
    strncpy( filespec, argv[ 1 ], MAX_FILESPEC_LEN );
*/
    filespec[ 0 ] = 0;
    for ( idx = 1; idx < argc-1; idx++ ) {
        strcat( filespec, argv[ idx ] );
        strcat( filespec, " " );
    }
    strcat( filespec, argv[ argc-1 ] );

    /* filespec describes multiple files + file selection criteria */
    ierr = TRACE_Open( filespec, &tf );
    if ( tf == NULL ) {
        if ( ierr == 0 ) {
            fprintf( stdout, "%s\n", TRACE_Get_err_string( ierr ) );
            fflush( stdout );
            exit( 0 );
        }
        else {
            fprintf( stderr, "%s\n", TRACE_Get_err_string( ierr ) );
            fflush( stderr );
            exit( 1 );
        }
    }
    obj_no = 0;

    ierr = TRACE_Peek_next_kind( tf, &next_kind );
    if ( ierr != 0 ) {
        fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
        fflush( stderr );
        exit( 1 );
    }
    while ( next_kind != TRACE_EOF ) {
        switch (next_kind) {
        case TRACE_COMPOSITE_DRAWABLE:
            /* Get the time range [stime,etime] and the number of drawables */
            num_primes    = 0;
            cmplx_info_sz = 0;
            ierr = TRACE_Peek_next_composite( tf, &cmplx_stime, &cmplx_etime,
                                              &num_primes, &cmplx_info_sz );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            if ( cmplx_info_sz >= 0 ) {
                /* Allocate the space and get common_info */
                cmplx_info_pos = 0;
                ierr = TRACE_Get_next_composite( tf, &cmplx_type_idx,
                                                 &cmplx_info_sz,
                                                 cmplx_info_base,
                                                 &cmplx_info_pos,
                                                 MAX_INFO_LEN );
                if ( ierr != 0 ) {
                    fprintf( stderr, "Error: %s\n",
                             TRACE_Get_err_string( ierr ) );
                    fflush( stderr );
                    exit( 1 );
                }
            }
            obj_no++;
            printf( "%ld : Composite: index=%d times=(%lf, %lf) ",
                    obj_no, cmplx_type_idx, cmplx_stime, cmplx_etime );
            printf( "Nprimes=%d ", num_primes );
            printf( "info_sz=%d\n", cmplx_info_sz );

            for ( idx2prime = 0; idx2prime < num_primes; idx2prime++ ) {
                /* Find the space needed */
                tcoord_sz = 0;
                ycoord_sz = 0;
                info_sz   = 0;
                ierr = TRACE_Peek_next_primitive( tf, &stime, &etime,
                                                  &tcoord_sz, &ycoord_sz,
                                                  &info_sz );
                if ( ierr != 0 ) {
                    fprintf( stderr, "Error: %s\n",
                             TRACE_Get_err_string( ierr ) );
                    exit( 1 );
                }
                /* Allocate the space, then get the shape */
                tcoord_pos = 0;
                ycoord_pos = 0;
                info_pos   = 0;
                ierr = TRACE_Get_next_primitive( tf, &type_idx,
                                                 &tcoord_sz, tcoord_base,
                                                 &tcoord_pos, MAX_TIME_COORDS,
                                                 &ycoord_sz, ycoord_base,
                                                 &ycoord_pos, MAX_VERT_COORDS,
                                                 &info_sz, info_base,
                                                 &info_pos, MAX_INFO_LEN );
                if ( ierr != 0 ) {
                    fprintf( stderr, "Error: %s\n",
                             TRACE_Get_err_string( ierr ) );
                    fflush( stderr );
                    exit( 1 );
                }
                printf( "\tPrimitive: index=%d times=(%lf, %lf) ",
                        type_idx, stime, etime );
                for ( idx = 0; idx < tcoord_sz; idx++ )
                     printf( "(%lf, %d) ", tcoord_base[idx], ycoord_base[idx] );
                printf( "info_sz=%d\n", info_sz );
            }
            break;
        case TRACE_PRIMITIVE_DRAWABLE:
            /* Find the space needed */
            tcoord_sz = 0;
            ycoord_sz = 0;
            info_sz   = 0;
            ierr = TRACE_Peek_next_primitive( tf, &stime, &etime,
                                              &tcoord_sz, &ycoord_sz,
                                              &info_sz );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            /* Allocate the space, then get the shape */
            tcoord_pos = 0;
            ycoord_pos = 0;
            info_pos   = 0;
            ierr = TRACE_Get_next_primitive( tf, &type_idx,
                                             &tcoord_sz, tcoord_base,
                                             &tcoord_pos, MAX_TIME_COORDS,
                                             &ycoord_sz, ycoord_base,
                                             &ycoord_pos, MAX_VERT_COORDS,
                                             &info_sz, info_base,
                                             &info_pos, MAX_INFO_LEN );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            obj_no++;
            printf( "%ld : Primitive: index=%d times=(%lf, %lf) ",
                    obj_no, type_idx, stime, etime );
            for ( idx = 0; idx < tcoord_sz; idx++ )
                 printf( "(%lf, %d) ", tcoord_base[idx], ycoord_base[idx] );
            printf( "info_sz=%d\n", info_sz );
            break;
        case TRACE_CATEGORY:
            /* Find the space needed */
            legend_sz     = 0;
            label_sz      = 0;
            methodIDs_sz  = 0;
            ierr = TRACE_Peek_next_category( tf,
                                             &legend_sz, &label_sz,
                                             &methodIDs_sz );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            /* Allocate the space, then get the Category */
            label_pos     = 0;
            legend_pos    = 0;
            methodID_pos  = 0;
            ierr = TRACE_Get_next_category( tf, &type_hdr,
                                            &legend_sz, legend_base,
                                            &legend_pos, MAX_LEGEND_LEN,
                                            &label_sz, label_base,
                                            &label_pos, MAX_LABEL_LEN,
                                            &methodIDs_sz, methodID_base,
                                            &methodID_pos, MAX_METHODS );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            legend_base[ legend_pos ]  = '\0';
            label_base[ label_pos ]    = '\0';
            printf( "Category: index=%d shape=%d color=(%d,%d,%d,%d) width=%d "
                    "legend=%s ", type_hdr.index, type_hdr.shape,
                    type_hdr.red, type_hdr.green, type_hdr.blue, type_hdr.alpha,
                    type_hdr.width, legend_base );
            if ( label_sz > 0 && label_pos > 0 )
                printf( "label=< %s > ", label_base );
            if ( methodIDs_sz > 0 ) {
                printf( "methods={ " );
                for ( idx = 0; idx < methodIDs_sz; idx++ )
                    printf( "%d ", methodID_base[ idx ] );
                printf( "}" );
            }
            printf( "\n" );
            break;
        case TRACE_YCOORDMAP:
            /* Find the space needed */
            nrows            = 0;
            ncolumns         = 0;
            max_column_name  = 0;
            max_title_name   = 0;
            methodIDs_sz     = 0;
            ierr = TRACE_Peek_next_ycoordmap( tf, &nrows, &ncolumns,
                                              &max_column_name,
                                              &max_title_name,
                                              &methodIDs_sz );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            fprintf( stderr, "max_column_name = %d, max_title_name = %d\n",
                             max_column_name, max_title_name );
            /* Allocate the space, then get the YCoordMap */
            title_name    = (char *) malloc( max_title_name * sizeof(char) );
            column_names  = (char **) malloc( (ncolumns-1) * sizeof(char *) );
            for ( icol = 0; icol < ncolumns-1; icol++ )
                column_names[ icol ] = (char *) malloc( max_column_name
                                                      * sizeof(char) );
            coordmap_max  = nrows * ncolumns;
            coordmap_base = (int *) malloc( coordmap_max * sizeof( int ) );
            coordmap_sz   = 0;
            coordmap_pos  = 0;
            methodID_pos  = 0;

            ierr = TRACE_Get_next_ycoordmap( tf, title_name, column_names,
                                             &coordmap_sz, coordmap_base,
                                             &coordmap_pos, coordmap_max,
                                             &methodIDs_sz, methodID_base,
                                             &methodID_pos, MAX_METHODS );
            if ( ierr != 0 ) {
                fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
                fflush( stderr );
                exit( 1 );
            }
            /* Print the YCoordMap */
            printf( "YCoordMap: %s[%d][%d]\n", title_name, nrows, ncolumns );
            printf( "LineID -> " );
            for ( icol = 0; icol < ncolumns-1; icol++ )
                printf( "%s ", column_names[ icol ] );
            printf( "\n" );
            idx = 0;
            for ( irow = 0; irow < nrows; irow++ ) {
                printf( "%d -> ", coordmap_base[ idx++ ] );
                for ( icol = 1; icol < ncolumns; icol++ )
                    printf( "%d ", coordmap_base[ idx++ ] );
                printf( "\n" );
            }
            if ( methodIDs_sz > 0 ) {
                printf( "methods={ " );
                for ( idx = 0; idx < methodIDs_sz; idx++ )
                    printf( "%d ", methodID_base[ idx ] );
                printf( "}\n" );
            }
            /* Release the allocated memory */
            for ( icol = 0; icol < ncolumns-1; icol++ )
                free( column_names[ icol ] );
            free( column_names );
            column_names = NULL;
            free( title_name );
            title_name = NULL;
            break;
        default:
            fprintf( stderr, "unknown TRACE_Rec_Kind_t\n" );
            fflush( stderr );
            exit( 1 );
        }
        ierr = TRACE_Peek_next_kind( tf, &next_kind );
        if ( ierr != 0 ) {
            fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
            fflush( stderr );
            exit( 1 );
        }
    }

    ierr = TRACE_Close( &tf );
    if ( ierr != 0 ) {
        fprintf( stderr, "Error: %s\n", TRACE_Get_err_string( ierr ) );
        fflush( stderr );
        exit( 1 );
    }

    return 0;
}
