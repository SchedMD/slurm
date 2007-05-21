/* mpe_graphics.c */
/* Custom Fortran interface file */
#include <stdio.h>
#include <string.h>
#include "mpeconf.h"
#ifdef POINTER_64_BITS
#define MPE_INTERNAL
#endif
#include "mpe.h"

#include "mpetools.h"
#include "basex11.h"
#include "mpe.h"

#ifndef HAVE_MPI_COMM_F2C
#define MPI_Comm_c2f(comm) (MPI_Fint)(comm)
#define MPI_Comm_f2c(comm) (MPI_Comm)(comm)
#endif

typedef MPI_Fint MPE_Fint;

#ifdef POINTER_64_BITS
/* Assume Fortran ints are 32 bits */
#define MPE_XGraph_c2f(xgraph)    (MPE_Fint)(xgraph->fort_index)
extern MPE_XGraph MPE_fort_head;
MPE_XGraph MPE_XGraph_f2c( MPE_Fint xgraph ) 
{
    MPE_XGraph p = MPE_fort_head;
    while (p && p->fort_index != xgraph ) p = p->next;
    return p;
}
#else
#define MPE_XGraph_c2f(xgraph)    (MPE_Fint)(xgraph)
#define MPE_XGraph_f2c(xgraph)    (MPE_XGraph)(xgraph)
#endif

#define MPE_Color_c2f(color)      (MPE_Fint)(color)
#define MPE_Color_f2c(color)      (MPE_Color)(color)

/* In order to suppress warnings about prototypes, each routine is prototyped 
   right before the definition */
#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_open_graphics_ PMPE_OPEN_GRAPHICS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_open_graphics_ pmpe_open_graphics__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_open_graphics_ pmpe_open_graphics
#else
#define mpe_open_graphics_ pmpe_open_graphics_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_open_graphics_ MPE_OPEN_GRAPHICS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_open_graphics_ mpe_open_graphics__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_open_graphics_ mpe_open_graphics
#endif
#endif

void mpe_open_graphics_( MPE_Fint *, MPE_Fint *, char *,
                         MPE_Fint *, MPE_Fint *, MPE_Fint *, MPE_Fint *,
                         MPE_Fint *, MPE_Fint *,
                         MPE_Fint );
void mpe_open_graphics_( MPE_Fint *graph, MPE_Fint *comm, char *display,
                         MPE_Fint *x, MPE_Fint *y, MPE_Fint *w, MPE_Fint *h,
                         MPE_Fint *is_collective, MPE_Fint *__ierr,
                         MPE_Fint display_len )
{
    MPE_XGraph local_graph;
    char *local_display;
    int local_display_len;
    int ii;

    /* trim the trailing blanks in display */
    for ( ii = (int) display_len-1; ii >=0; ii-- )
        if ( display[ii] != ' ' ) break;
    if ( ii < 0 ) {
        local_display_len = 0;
        local_display = NULL;
    }	
    else {
        local_display_len = ii + 1;
        local_display = (char *) MALLOC( (local_display_len+1) * sizeof(char) );
        strncpy( local_display, display, local_display_len );
        local_display[ local_display_len ] = '\0';
    }

    *__ierr = MPE_Open_graphics( &local_graph, MPI_Comm_f2c(*comm),
                                 local_display, (int)*x, (int)*y,
                                 (int)*w, (int)*h, (int)*is_collective );
    *graph = MPE_XGraph_c2f(local_graph);

    if ( local_display )
        FREE( local_display );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_capturefile_ PMPE_CAPTUREFILE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_capturefile_ pmpe_capturefile__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_capturefile_ pmpe_capturefile
#else
#define mpe_capturefile_ pmpe_capturefile_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_capturefile_ MPE_CAPTUREFILE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_capturefile_ mpe_capturefile__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_capturefile_ mpe_capturefile
#endif
#endif

void mpe_capturefile_( MPE_Fint *, char *, MPE_Fint *,
                       MPE_Fint *, MPE_Fint );
void mpe_capturefile_( MPE_Fint *graph, char *fname, MPE_Fint *freq,
                       MPE_Fint *__ierr, MPE_Fint fname_len )
{
    char *local_fname;
    int local_fname_len;
    int ii;
	
    /* trim the trailing blanks in fname */
    for ( ii = (int) fname_len-1; ii >=0; ii-- )
        if ( fname[ii] != ' ' ) break;
    if ( ii < 0 )
        ii = 0;
    local_fname_len = ii + 1;
    local_fname = (char *) MALLOC( (local_fname_len+1) * sizeof(char) );
    strncpy( local_fname, fname, local_fname_len );
    local_fname[ local_fname_len ] = '\0';

    *__ierr = MPE_CaptureFile( MPE_XGraph_f2c(*graph), local_fname,
                               (int)*freq );

    if ( local_fname )
        FREE( local_fname );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_point_ PMPE_DRAW_POINT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_point_ pmpe_draw_point__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_point_ pmpe_draw_point
#else
#define mpe_draw_point_ pmpe_draw_point_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_point_ MPE_DRAW_POINT
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_point_ mpe_draw_point__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_point_ mpe_draw_point
#endif
#endif

void mpe_draw_point_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
			          MPE_Fint *, MPE_Fint * );
void mpe_draw_point_( MPE_Fint *graph, MPE_Fint *x, MPE_Fint *y,
			          MPE_Fint *color, MPE_Fint *__ierr )
{
    *__ierr = MPE_Draw_point( MPE_XGraph_f2c(*graph), (int)*x, (int)*y,
                              MPE_Color_f2c(*color) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_points_ PMPE_DRAW_POINTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_points_ pmpe_draw_points__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_points_ pmpe_draw_points
#else
#define mpe_draw_points_ pmpe_draw_points_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_points_ MPE_DRAW_POINTS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_points_ mpe_draw_points__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_points_ mpe_draw_points
#endif
#endif

void mpe_draw_points_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                       MPE_Fint * );
void mpe_draw_points_( MPE_Fint *graph, MPE_Fint *points, MPE_Fint *npoints,
                       MPE_Fint *__ierr )
{
    MPE_Point *local_points;
    MPE_Fint *fpoint;
    int Npts;
    int ii;

    Npts = (int)*npoints;
    local_points = ( MPE_Point * ) MALLOC( Npts * sizeof( MPE_Point ) );

    fpoint = points;
    for ( ii = 0; ii < Npts; ii++ ) {
        local_points[ ii ].x = (int) *fpoint;
        fpoint ++;
        local_points[ ii ].y = (int) *fpoint;
        fpoint ++;
        local_points[ ii ].c = MPE_Color_f2c(*fpoint);
        fpoint ++;
    }

    *__ierr = MPE_Draw_points( MPE_XGraph_f2c(*graph), local_points, Npts );

    if ( local_points )
        FREE( local_points );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_line_ PMPE_DRAW_LINE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_line_ pmpe_draw_line__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_line_ pmpe_draw_line
#else
#define mpe_draw_line_ pmpe_draw_line_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_line_ MPE_DRAW_LINE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_line_ mpe_draw_line__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_line_ mpe_draw_line
#endif
#endif

void mpe_draw_line_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                     MPE_Fint *, MPE_Fint *, MPE_Fint *,
                     MPE_Fint * );
void mpe_draw_line_( MPE_Fint *graph, MPE_Fint *x1, MPE_Fint *y1,
                     MPE_Fint *x2, MPE_Fint *y2, MPE_Fint *color,
                     MPE_Fint *__ierr )
{
    *__ierr = MPE_Draw_line( MPE_XGraph_f2c(*graph),(int)*x1,(int)*y1,
                             (int)*x2, (int) *y2, MPE_Color_f2c(*color) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_fill_rectangle_ PMPE_FILL_RECTANGLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_fill_rectangle_ pmpe_fill_rectangle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_fill_rectangle_ pmpe_fill_rectangle
#else
#define mpe_fill_rectangle_ pmpe_fill_rectangle_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_fill_rectangle_ MPE_FILL_RECTANGLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_fill_rectangle_ mpe_fill_rectangle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_fill_rectangle_ mpe_fill_rectangle
#endif
#endif

void mpe_fill_rectangle_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                          MPE_Fint *, MPE_Fint *, MPE_Fint *,
                          MPE_Fint * );
void mpe_fill_rectangle_( MPE_Fint *graph, MPE_Fint *x, MPE_Fint *y,
                          MPE_Fint *w, MPE_Fint *h, MPE_Fint *color,
                          MPE_Fint *__ierr )
{
    *__ierr = MPE_Fill_rectangle( MPE_XGraph_f2c(*graph), (int)*x, (int)*y,
                                  (int)*w, (int)*h, MPE_Color_f2c(*color) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_update_ PMPE_UPDATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_update_ pmpe_update__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_update_ pmpe_update
#else
#define mpe_update_ pmpe_update_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_update_ MPE_UPDATE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_update_ mpe_update__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_update_ mpe_update
#endif
#endif

void mpe_update_( MPE_Fint *, MPE_Fint * );
void mpe_update_( MPE_Fint *graph, MPE_Fint *__ierr )
{
    *__ierr = MPE_Update( MPE_XGraph_f2c(*graph) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_close_graphics_ PMPE_CLOSE_GRAPHICS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_close_graphics_ pmpe_close_graphics__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_close_graphics_ pmpe_close_graphics
#else
#define mpe_close_graphics_ pmpe_close_graphics_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_close_graphics_ MPE_CLOSE_GRAPHICS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_close_graphics_ mpe_close_graphics__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_close_graphics_ mpe_close_graphics
#endif
#endif

void mpe_close_graphics_( MPE_Fint *, MPE_Fint * );
void mpe_close_graphics_( MPE_Fint *graph, MPE_Fint *__ierr )
{
    MPE_XGraph local_graph;

    local_graph = MPE_XGraph_f2c( *graph );

    *__ierr = MPE_Close_graphics( &local_graph );

#ifdef POINTER_64_BITS
    /* We could update the list of objects by removing *graph from the
       list pointed at by MPE_fort_head */
#endif
    *graph = 0;
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_make_color_array_ PMPE_MAKE_COLOR_ARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_make_color_array_ pmpe_make_color_array__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_make_color_array_ pmpe_make_color_array
#else
#define mpe_make_color_array_ pmpe_make_color_array_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_make_color_array_ MPE_MAKE_COLOR_ARRAY
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_make_color_array_ mpe_make_color_array__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_make_color_array_ mpe_make_color_array
#endif
#endif

void mpe_make_color_array_( MPE_Fint *, MPE_Fint *,
                            MPE_Fint *, MPE_Fint * );
void mpe_make_color_array_( MPE_Fint *graph, MPE_Fint *ncolors,
                            MPE_Fint *array, MPE_Fint *__ierr )
{
    MPE_Color *local_array;
    int       Ncolors;
    int       ii;

    Ncolors = (int)*ncolors;
    local_array = ( MPE_Color * ) MALLOC( Ncolors * sizeof( MPE_Color ) );

    *__ierr = MPE_Make_color_array( MPE_XGraph_f2c(*graph), Ncolors,
                                    local_array );

    for ( ii = 0; ii < Ncolors; ii++ )
        array[ ii ] = MPE_Color_c2f( local_array[ ii ] );
    if ( local_array )
        FREE( local_array );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_num_colors_ PMPE_NUM_COLORS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_num_colors_ pmpe_num_colors__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_num_colors_ pmpe_num_colors
#else
#define mpe_num_colors_ pmpe_num_colors_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_num_colors_ MPE_NUM_COLORS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_num_colors_ mpe_num_colors__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_num_colors_ mpe_num_colors
#endif
#endif

void mpe_num_colors_( MPE_Fint *, MPE_Fint *, MPE_Fint * );
void mpe_num_colors_( MPE_Fint *graph, MPE_Fint *ncolors, MPE_Fint *__ierr )
{
    int local_ncolors;
    *__ierr = MPE_Num_colors( MPE_XGraph_f2c(*graph), &local_ncolors );
                              *ncolors = (MPE_Fint) local_ncolors;
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_circle_ PMPE_DRAW_CIRCLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_circle_ pmpe_draw_circle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_circle_ pmpe_draw_circle
#else
#define mpe_draw_circle_ pmpe_draw_circle_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_circle_ MPE_DRAW_CIRCLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_circle_ mpe_draw_circle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_circle_ mpe_draw_circle
#endif
#endif

void mpe_draw_circle_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                       MPE_Fint *, MPE_Fint *, MPE_Fint * );
void mpe_draw_circle_( MPE_Fint *graph, MPE_Fint *centerx, MPE_Fint *centery,
                       MPE_Fint *radius, MPE_Fint *color, MPE_Fint *__ierr )
{
    *__ierr = MPE_Draw_circle( MPE_XGraph_f2c(*graph),
                               (int)*centerx, (int)*centery,
                               (int)*radius, MPE_Color_f2c(*color) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_fill_circle_ PMPE_FILL_CIRCLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_fill_circle_ pmpe_fill_circle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_fill_circle_ pmpe_fill_circle
#else
#define mpe_fill_circle_ pmpe_fill_circle_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_fill_circle_ MPE_FILL_CIRCLE
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_fill_circle_ mpe_fill_circle__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_fill_circle_ mpe_fill_circle
#endif
#endif

void mpe_fill_circle_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                       MPE_Fint *, MPE_Fint *, MPE_Fint * );
void mpe_fill_circle_( MPE_Fint *graph, MPE_Fint *centerx, MPE_Fint *centery,
                       MPE_Fint *radius, MPE_Fint *color, MPE_Fint *__ierr )
{
    *__ierr = MPE_Fill_circle( MPE_XGraph_f2c(*graph),
                               (int)*centerx, (int)*centery,
                               (int)*radius, MPE_Color_f2c(*color) );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_string_ PMPE_DRAW_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_string_ pmpe_draw_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_string_ pmpe_draw_string
#else
#define mpe_draw_string_ pmpe_draw_string_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_string_ MPE_DRAW_STRING
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_string_ mpe_draw_string__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_string_ mpe_draw_string
#endif
#endif

void mpe_draw_string_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                       MPE_Fint *, char *, MPE_Fint *,
                       MPE_Fint string_len );
void mpe_draw_string_( MPE_Fint *graph, MPE_Fint *x, MPE_Fint *y,
                       MPE_Fint *color, char *string, MPE_Fint *__ierr,
                       MPE_Fint string_len )
{
    char *local_string;
    int local_string_len;
    int ii;
	
    /* trim the trailing blanks in string */
    for ( ii = (int) string_len-1; ii >=0; ii-- )
        if ( string[ii] != ' ' ) break;
    if ( ii < 0 )
        ii = 0;
    local_string_len = ii + 1;
    local_string = (char *) MALLOC( (local_string_len+1) * sizeof(char) );
    strncpy( local_string, string, local_string_len );
    local_string[ local_string_len ] = '\0';

    *__ierr = MPE_Draw_string( MPE_XGraph_f2c(*graph), (int)*x, (int)*y,
                               MPE_Color_f2c(*color), local_string );

    if ( local_string )
        FREE( local_string );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_draw_logic_ PMPE_DRAW_LOGIC
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_logic_ pmpe_draw_logic__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_logic_ pmpe_draw_logic
#else
#define mpe_draw_logic_ pmpe_draw_logic_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_draw_logic_ MPE_DRAW_LOGIC
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_draw_logic_ mpe_draw_logic__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_draw_logic_ mpe_draw_logic
#endif
#endif

void mpe_draw_logic_( MPE_Fint *, MPE_Fint *, MPE_Fint * );
void mpe_draw_logic_( MPE_Fint *graph, MPE_Fint *function, MPE_Fint *__ierr )
{
    *__ierr = MPE_Draw_logic( MPE_XGraph_f2c(*graph), (int)*function );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_line_thickness_ PMPE_LINE_THICKNESS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_line_thickness_ pmpe_line_thickness__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_line_thickness_ pmpe_line_thickness
#else
#define mpe_line_thickness_ pmpe_line_thickness_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_line_thickness_ MPE_LINE_THICKNESS
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_line_thickness_ mpe_line_thickness__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_line_thickness_ mpe_line_thickness
#endif
#endif

void mpe_line_thickness_( MPE_Fint *, MPE_Fint *,
                          MPE_Fint * );
void mpe_line_thickness_( MPE_Fint *graph, MPE_Fint *thickness,
                          MPE_Fint *__ierr )
{
    *__ierr = MPE_Line_thickness( MPE_XGraph_f2c(*graph), (int)*thickness );
}

#ifdef MPI_BUILD_PROFILING
#ifdef F77_NAME_UPPER
#define mpe_add_rgb_color_ PMPE_ADD_RGB_COLOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_add_rgb_color_ pmpe_add_rgb_color__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_add_rgb_color_ pmpe_add_rgb_color
#else
#define mpe_add_rgb_color_ pmpe_add_rgb_color_
#endif
#else
#ifdef F77_NAME_UPPER
#define mpe_add_rgb_color_ MPE_ADD_RGB_COLOR
#elif defined(F77_NAME_LOWER_2USCORE)
#define mpe_add_rgb_color_ mpe_add_rgb_color__
#elif !defined(F77_NAME_LOWER_USCORE)
#define mpe_add_rgb_color_ mpe_add_rgb_color
#endif
#endif

void mpe_add_rgb_color_( MPE_Fint *, MPE_Fint *, MPE_Fint *,
                         MPE_Fint *, MPE_Fint *, MPE_Fint * );
void mpe_add_rgb_color_( MPE_Fint *graph, MPE_Fint *red, MPE_Fint *green,
                         MPE_Fint *blue, MPE_Fint *mapping, MPE_Fint *__ierr )
{
     MPE_Color local_mapping;

     local_mapping = MPE_Color_f2c( *mapping );

     *__ierr = MPE_Add_RGB_color( MPE_XGraph_f2c(*graph),
                                 (int)*red, (int) *green, (int) *blue,
                                 &local_mapping );

     *mapping = local_mapping;
}
