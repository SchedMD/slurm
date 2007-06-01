/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Bill Gropp, Anthony Chan
 */

/*
 * API to read a trace file for the SLOG algorithm
 */

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _trace_file *TRACE_file;

#if defined( WIN32 )
#define TRACE_EXPORT   __declspec(dllexport)
#define TRACE_int64_t  __int64
#else
#define TRACE_EXPORT
#define TRACE_int64_t  long long
#endif

/*E
  TRACE_Rec_Kind_t - Types of records returned by the TRACE API

  Types:
+ TRACE_EOF - End of file.  Indicates that no more items are available.
. TRACE_PRIMITIVE_DRAWABLE - Primitive Drawable;
                             for example, an event, state or arrow.
. TRACE_COMPOSITE_DRAWABLE - Composite Drawable;
                             a collection of primitive drawables.
. TRACE_CATEGORY           - Category, describing classes of drawables.
- TRACE_YCOORDMAP          - Y-axis Coordinate map, describing how to 
                             interpret or label the y coordinate values

  Notes:
  These record types represent the type of data that the TRACE API presents to 
  the calling program.  The source file that the TRACE API is reading may
  or may not contain any of these record types.  In fact, most trace files
  will not contain any of these record types; instead, the implementation of
  the TRACE API will read the source trace file and create these from the 
  raw data in the original source file.
  E*/
typedef enum { TRACE_EOF=0, 
               TRACE_PRIMITIVE_DRAWABLE=1, 
               TRACE_COMPOSITE_DRAWABLE=2, 
               TRACE_CATEGORY=3,
               TRACE_YCOORDMAP=4 } 
TRACE_Rec_Kind_t;

/*
   Predefined Shapes ID - for 'TRACE_Category_head_t'
 */
#define TRACE_SHAPE_EVENT 0
#define TRACE_SHAPE_STATE 1
#define TRACE_SHAPE_ARROW 2

/*
   Predefined Method IDs - for 'TRACE_Get_next_category()'
                           and 'TRACE_Get_next_ycoordmap()'
 */
#define TRACE_METHOD_CONNECT_COMPOSITE_STATE 1

/*S
  TRACE_Category_head_t - Structure defining the basic information about a 
  category

+ index - integer value by which records will identify themselves
  as belonging to this category.  index is assumed to be non-negative.
  negative index is reserved for internal use.
. shape - Shape of the category.  This is an integer defined by the drawing
  program; the value 'TRACE_SHAPE_EVENT' (=0) is reserved for an event
  ( a marker at one point on a timeline ), the value 'TRACE_SHAPE_STATE' (=1)
  is reserved for a basic state (a rectangle along a timeline), 
  'TRACE_SHAPE_ARROW' (=2) is reserved for an arrow (such as used to 
  describe a message from a send state to a receive state), 
. red, green, blue - Color of the shape; each is in the range [0,255]
. alpha - Transparency value, in the range of [0,255].  Some display programs
  may ignore this value.  An alpha value of 255 means that the color
  is completely opaque and an alpha value of 0 means that the color 
  is completely transparent.  (reference java.awt.Color)
- width - the pixel width of the stroke when drawing the shape.  Some display
  programs may ignore this value.

  S*/
typedef struct {
  int   index;
  int   shape;
  int   red, green, blue, alpha;
  int   width;
} TRACE_Category_head_t;

/*
. name - name of the category (See below)

  Questions:
  A prior version left the 'name' out of the 'TRACE_Category_head_t'.  
  This was done to separate the frequently accessed data (shape, color, 
  width) that is needed to render a member of this category from the data
  needed to describe the category on a legend and to describe a particular
  instance (the label).  A more recent version included 'char *name', but
  the API provided no way to allocate or free the associated storage for
  this data.  I have removed 'name' until the issues are resolved.

*/

TRACE_EXPORT
int TRACE_Open( const char filespec[], TRACE_file *fp );

TRACE_EXPORT
int TRACE_Close( TRACE_file *fp );

/*
TRACE_EXPORT
int TRACE_Get_total_time( const TRACE_file fp, 
                          double *starttime, double *endtime );
*/

TRACE_EXPORT
int TRACE_Peek_next_kind( const TRACE_file fp, TRACE_Rec_Kind_t *next_kind );

TRACE_EXPORT
int TRACE_Get_next_method( const TRACE_file fp,
                           char method_name[], char method_extra[], 
                           int *methodID );

TRACE_EXPORT
int TRACE_Peek_next_category( const TRACE_file fp,
                              int *n_legend, int *n_label,
                              int *n_methodIDs );

TRACE_EXPORT
int TRACE_Get_next_category( const TRACE_file fp, 
                             TRACE_Category_head_t *head,
                             int *n_legend, char legend_base[],
                             int *legend_pos, const int legend_max,
                             int *n_label, char label_base[],
                             int *label_pos, const int label_max,
                             int *n_methodIDs, int methodID_base[],
                             int *methodID_pos, const int methodID_max );

TRACE_EXPORT
int TRACE_Peek_next_ycoordmap( TRACE_file fp,
                               int *n_rows, int *n_columns,
                               int *max_column_name,
                               int *max_title_name,
                               int *n_methodIDs );

TRACE_EXPORT
int TRACE_Get_next_ycoordmap( TRACE_file fp,
                              char *title_name,
                              char **column_names,
                              int *coordmap_sz, int coordmap_base[],
                              int *coordmap_pos, const int coordmap_max,
                              int *n_methodIDs, int methodID_base[],
                              int *methodID_pos, const int methodID_max );

TRACE_EXPORT
int TRACE_Peek_next_primitive( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *n_tcoords, int *n_ycoords, int *n_bytes );

TRACE_EXPORT
int TRACE_Get_next_primitive( const TRACE_file fp, 
                              int *category_index, 
                              int *n_tcoords, double tcoord_base[],
                              int *tcoord_pos, const int tcoord_max, 
                              int *n_ycoords, int ycoord_base[], 
                              int *ycoord_pos, const int ycoord_max,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max );

TRACE_EXPORT
int TRACE_Peek_next_composite( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *n_primitives, int *n_bytes );

TRACE_EXPORT
int TRACE_Get_next_composite( const TRACE_file fp,
                              int *category_index,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max );


TRACE_EXPORT
int TRACE_Get_position( TRACE_file fp, TRACE_int64_t *offset );

TRACE_EXPORT
int TRACE_Set_position( TRACE_file fp, TRACE_int64_t offset );

TRACE_EXPORT
char *TRACE_Get_err_string( int ierr );

#if defined(__cplusplus)
}
#endif
