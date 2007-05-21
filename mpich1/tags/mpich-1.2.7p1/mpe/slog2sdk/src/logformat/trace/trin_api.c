/*
 *  (C) 2001 by Argonne National Laboratory
 *      See COPYRIGHT in top-level directory.
 */

/*
 *  @author  Bill Gropp, Anthony Chan
 */

/*
 * API to read a trace file for the SLOG algorithm
 * 
 * We want to defer the choice of representation within the SLOG code
 * while providing an interface the requires the fewest possible
 * copies.
 *
 * This API is optimized for *performance*, not for simplicity.
 *
 * The basic operation is "get next drawable".  All functions
 * assumes some sense of "current" record. Each drawable has the 
 * properties
 *    starttime, endtime 
 *    category (controls shape and color)
 *    coords   (auxillery data for drawing)
 *    text     (popup text input)
 *
 * To allow aggregate of drawables with different categories or texts
 * to be viewed as one drawable, Drawable will be categorized into 2
 * types.  They are called Primitive Drawable and Composite Drawable.
 * Primitive drawable is a simple drawable with a well defined draw() 
 * method + a catgory and text string, e.g. event, state and arrow.  
 * Composite drawable is just a collection of any drawables.  For 
 * simplicity, composite drawable will be assumed to a collection 
 * of primitive drawables in this API.
 *
 * The assumption is that data is read into linear arrays, to
 * optimize performance in the case that there are many small pieces
 * of text and coordinates.
 *
 * An earlier version assumed that category and drawable descriptions
 * were separate.  This version is more general, but requires the
 * user to peek at an object (using Peek_next_kind) to get its kind before 
 * reading it.  Since we encourage a buffered read implementation, this
 * should not introduce any significant inefficiency.
 *
 * One bad thing about this is that the data for a single drawable is not
 * together on a cacheline.  However, the same fields for nearby
 * objects are likely to be nearby, and this approach handles
 * variable length data well.  In fact, for many objects with significant
 * popup data (argument values, source code location), this may provide 
 * *better* cache locality for the drawing information.
 *
 * The reason that we have *not* chosen to define the structure layout for
 * the items read is that this API is used both by the SLOG program *and*
 * by the Display program, in the case where the SLOG Annotation form 
 * is used.
 * 
 * One alternative is to define the structure layout interms of an array of
 * offsets and addresses, and allow the routine to use that info to 
 * decide where to put data.  In that case, it would be possible to use
 * a single call to fill in a data structure.
 */

#include "trin_api.h"

/*@
  TRACE_Open - Open a trace file for input

  Input Parameter:
. filespec - Name of file (or files; see below) to open. 

  Output Parameter:
. fp - Trace file handle (see Notes).

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  In order to allow TRACE-API to provide its own help message, i.e '-h',
  in 'filespec' string, when the API is used in program like TraceToSlog2.
  Calling program of TRACE_Open() should check if the returned TRACE_file 
  handle 'fp' is NULL instead of just checking of the returned error status.
  If 'fp' is NULL, it means calling program should call TRACE_Get_err_string()
  for either error message or possible help message.  The help message should
  be stored at ierr=0, so the calling program of TRACE_Open() knows if it
  should exit the program with error or normally.
  
  The trace file may be a collection of files, however, to the user of the 
  TRACE API, there is a single (virtual) file.  The 'filespec' is any
  string that is accepted by the TRACE API.  Since the 
  Slog program will only pass this string through (e.g., from the command-line
  to this call), it need not be a file name.  

  Possible interpretations of 'filespec' include a filename, an indirect file
  (i.e., a file that contains the names of other files), a colon separated 
  list of files (i.e., 'file1:file2:file3'), a file pattern (i.e., 'file%d'),
  any of the above along with other options (for the trace file reader), such 
  as limits on the time range or node numbers to accept, or even a shell 
  command (i.e., 'find . -name ''*.log'' ').  'filespec' could contain 
  tracefile selection criteria, e.g. '-s [5,6-8] trc.*'.
  The implementation of the TRACE API must document the acceptable 
  'filespec' so that programs that make use of the TRACE API can provide 
  complete documentation to the user.
  @*/
int TRACE_Open( const char filespec[], TRACE_file *fp )
{}

/*@
  TRACE_Close - Close a trace file

  Input/Output Parameter:
. fp - Pointer to a trace file handle

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes: 
  The pointer 'fp' is set to NULL on a successful close.
@*/
int TRACE_Close( TRACE_file *fp )
{}

/*
  TRACE_Get_total_time - Return the time range covered by an trace file

  Input/Output Parameter:
. fp - Trace file handle

  Output Parameters:
+ starttime - Time when log file begins (no event before this time)
- endtime - Time when log file end (no event after this time)

  Questions:
  Do we want to require this?  In some cases, it may be difficult to 
  return this time.  
*/
/*
int TRACE_Get_total_time( const TRACE_file fp, 
                          double *starttime, double *endtime )
{}
*/

/*@
  TRACE_Peek_next_kind - Determine the kind of the next record

  Input Parameter:
. fp - Trace file handle

  Output Parameters:
. next_kind - Type of next record.  The kind 'TRACE_EOF', 
  which has the value '0', is returned at end-of-file.

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  The structure and ordering of data in a foreign trace file is not defined.
  This routine allows us to find out the type of the next record and then
  use the appropriate 'TRACE_Peek_xxx' routine to discover the size of any 
  variable-sized fields and 'TRACE_Get_xxx' routine to read it.  
  A high-performance implementation of these routines will likely 
  use buffered I/O. 
  @*/
int TRACE_Peek_next_kind( const TRACE_file fp, TRACE_Rec_Kind_t *next_kind )
{}

/* Once the kind of the next item is determined, one of the next 4
   routines may be called */
/* @
  TRACE_Get_next_method - Get the next method description

  Input Parameter:
. fp - Trace file handle

  Output Parameters:
+ method_name - 
. method_extra - 
- method_id - 

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Note:
  A typical trace file may have `no` methods.

  Question:
  Should methods have ids so that categories can refer to them by id instead
  of by name?

  How do we ensure that the data areas are large enough?  Do we need 
  a 'TRACE_Peek_next_method' or an input/output parameter indicating the 
  amount of available storage (with an error return of "insufficient 
  memory")?

  @ */
int TRACE_Get_next_method( const TRACE_file fp,
                           char method_name[], char method_extra[], 
                           int *method_id )
{}

/*@
  TRACE_Peek_next_category - Peek at the next category to determine necessary 
  data sizes

  Input Parameter:
. fp - Trace file handle

  Output Parameters:
+ n_legend - Number of characters needed for the legend
. n_label - Number of characters needed for the label
- n_methodIDs - Number of methods (Always zero or one in this version) 

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  The output parameters allow the calling code to allocate space for the 
  variable-length data in a categiry before calling 
  'TRACE_Get_next_category()'.

  @*/
int TRACE_Peek_next_category( const TRACE_file fp,
                              int *n_legend, int *n_label, 
                              int *n_methodIDs )
{}

/*@
  TRACE_Get_next_category - Get the next category description

  Input Parameter:
+ fp - Trace file handle
. legend_max - Allocated size of 'legend_base' array
. label_max - Allocated size of 'label_base' array
- methodID_max - Allocated size of 'methodID_base' array

  Input/Output Parameters:
+ legend_pos - On input, the first available position in 'legend_base'
  On output, changed to indicate the new first available position.
. label_pos - On input, the first available position in 'label_base'
  On output, changed to indicate the new first available position.
- methodID_pos - On input, the first available position in 'methodID_base'
  On output, changed to indicate the new first available position.
 
  Output Parameters:
+ head - Contains basic category info (see the description of 
  'TRACE_Category_head_t')
. n_legend - Size of 'legend_base' array to be used
. legend_base - Pointer to storage to hold legend information
. n_label - Size of 'label_base' array to be used
. label_base - Pointer to storage to hold label information.
               The order of the % tokens specified here, 'label_base', 
               must match the order of operands in the byte array,
               'byte_base[]', specified in 'TRACE_Get_next_primitive()'
               and 'TRACE_Get_next_composite()'.
. n_methodIDs - number of method IDs associated with this category.
- methodID_base - Pointer to storage to hold method IDs.

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.


  Notes:
  The interface to this (and similar routines such as 
  'TRACE_Get_next_primitive()') is designed to give flexibility in how
  data is read.  See 'SLOG2_Get_next_category()' for more details.

  The legend string is used to hold a label for a legend desribing the 
  category.  A typical visualization program will use that text to label 
  and draw a sample of a member from that 
  category.  For example, a blue rectangle with the text 'MPI_Send'.

  The label string is used to describe a particular drawable in that
  category.  For example, a label string of 
.vb
    "Tag = %s\nDestination rank = %s\nmessage size = %s"
.ve
  allows a visualization program to pop up a text box describing any
  drawable while allowing the drawable itself to store only the information 
  that is specific to each instance of the drawable (i.e., the three
  string values referenced).  These string values are provided through the 
  'byte' arguments to 'TRACE_Get_next_primitive'.

  The routine 'TRACE_Peek_next_category' may be used to determine the 
  number of characters of label and legend that are required.
  @*/
int TRACE_Get_next_category( const TRACE_file fp,
                             TRACE_Category_head_t *head,
                             int *n_legend, char legend_base[],
                             int *legend_pos, const int legend_max,
                             int *n_label, char label_base[],
                             int *label_pos, const int label_max,
                             int *n_methodIDs, int methodID_base[],
                             int *methodID_pos, const int methodID_max )
{}
/*
  Old text

. category_methods - Null-terminated array of null-terminated strings 
                     describing methods used to process record-specific data.
- category_method_extra - Extra data for each method
...
                             char category_methods[][], 
                             char category_method_extra[][] )

  To simplify the use of these routines, an empty category method will 
  be interpreted as the default method.  The entries in the category methods
  are interpreted as follows\:
 
+ 0 - Method to use in displaying the legend entry.  
. 1 - Method to use in displaying the popup text.
- >1 - Other popup methods (such as a source code browser).

  The API for describing the methods has not yet been defined, but will likely
  be Java code that works with a display program.  
*/

/*@
  TRACE_Peek_next_primitive - Peek at the next primitive drawable to 
  determine necessary data sizes and time range

  Input Parameter:
. fp - Trace file handle

  Output Parameters:
+ starttime, endtime - time range for drawable
. nt_coords - Number of time coordinates
. ny_coords - Number of y coordinates
- n_bytes - Number of data bytes

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  This function really serves two purposes.
  The time range allows the SLOG2 algorithm to determine which treenode a
  drawable should be placed in (which may influence where in memory the data 
  is read by 'TRACE_Get_next_primitive()').  
  The other return values allow the calling code to allocate space for the 
  variable-length data in a drawable before calling 'TRACE_Get_next_primitive'.

  @*/
int TRACE_Peek_next_primitive( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *nt_coords, int *ny_coords, int *n_bytes )
{}

/*@
  TRACE_Get_next_primitive - Get the next primitive drawable

  Input Parameter:
+ fp - Trace file handle
. tcoord_max - Size of 'tcoord_base'
. ycoord_max - Size of 'ycoord_base'
- byte_max - Size of 'byte_base'

  Input/Output Parameters:
+ tcoord_pos - On input, the first free location in 'tcoord_base'.  Updated
               on output to the new first free location.
. ycoord_pos - The same, for 'ycoord_base'
- byte_pos -  The same, for 'byte_base'

  Output Parameters:
+ starttime, endtime - time range for drawable
. category_index - Index of the category that this drawable belongs to
. nt_coords - Number of time coordinates
. tcoord_base - Pointer to storage to hold time coordinates
. ny_coords - Number of y coordinates
. ycoord_base - Pointer to storage to hold y coordinates
- byte_base - Pointer to storage to hold bytes.  The order of operands
              in the byte array, 'byte_base[]', specified here must match
              the order of the % tokens in the label string, 'label_base',
              in the TRACE_Get_next_category().


  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  The 'ycoord' values stored in 'ycoord_base + ycoord_pos' represent 
  y-coordinate index values.  These may be simple 'int' values or they may
  be indexes into a y-coordinate mapping table.  For example, a simple 
  trace file format that only records the rank in 'MPI_COMM_WORLD' as the
  y coordinate would return that rank value directly.  A more sophisticated
  trace file format that wished to return the nodename, process id, MPI rank, 
  and thread id would instead return an integer index value into a table 
  that contained that data.  The rows of this table (representing the values 
  for a single index value) are provided through a `routine to be determined`.
  In the latter case, it is better to think of the y coordinate values as
  'thread_id_index' values.

  Rationale:
  The somewhat complex argument list is intended to provide the maximum 
  flexibility in reading and storing the data.  For example, the calling
  program can either allocate new data for each call (using information 
  returned by 'TRACE_Peek_next_primitive') or use preallocated stacks
  (allowing, for example, all 'double' data to be stored contiguously).
  An alternative interface 
  could return a C structure or an instance of a C++ class that contained all
  of this data.  However, that approach imposes a particular representation 
  on any application that chooses to use the code.  If, for example, these
  routines are being used from another language, such as Java, a C or C++
  style interface may be inefficient.  It is expected that this routine will
  appear only within a single higher-level routine that reads data into
  storage organized in a convenient way for the calling application.

  @*/
int TRACE_Get_next_primitive( const TRACE_file fp, 
                              int *category_index, 
                              int *nt_coords, double tcoord_base[],
                              int *tcoord_pos, const int tcoord_max, 
                              int *ny_coords, int ycoord_base[], 
                              int *ycoord_pos, const int ycoord_max,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{}

/*@
  TRACE_Peek_next_composite - Peek at the next composite drawable to
  determine the number of primitive drawables in this composite object,
  time range, and size of pop up data.

  Input Parameter:
. fp - Trace file handle

  Output Parameters:
+ starttime, endtime - time range for drawable
. n_primitives - Number of primitive drawables in this composite object.
- n_bytes - Number of data bytes

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  This function really serves two purposes.
  The time range allows the SLOG2 algorithm to determine which treenode this 
  drawable should be placed in (which may influence where in memory the data
  is read by 'TRACE_Get_next_composite()').
  The number of primitives returned allows the calling program to invoke
  'TRACE_Get_next_primitives()' the same number of times to collect all 
  the primitive drawables in the composite object.
  The other return values allow the calling code to allocate space for the
  variable-length data in the composite drawable before calling 
  'TRACE_Get_next_composite()'.

  @*/
int TRACE_Peek_next_composite( const TRACE_file fp,
                               double *starttime, double *endtime,
                               int *n_primitives, int *n_bytes )
{}

/*@
  TRACE_Get_next_composite - Get the header information of the 
                             next composite drawable

  Input Parameter:
+ fp - Trace file handle
- byte_max - Size of 'byte_base'

  Input/Output Parameters:
. byte_pos -  The same, for 'byte_base'

  Output Parameters:
+ starttime, endtime - time range for drawable
. category_index - Index of the category that this drawable belongs to
- byte_base - Pointer to storage to hold bytes.  The order of operands
              in the byte array, 'byte_base[]', specified here must match
              the order of the % tokens in the label string, 'label_base',
              in the TRACE_Get_next_category().


  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  The interface to this is designed to allow flexibility in how data is read.
  See 'TRACE_Get_next_primitive' for more details.
  @*/
int TRACE_Get_next_composite( const TRACE_file fp,
                              int *category_index,
                              int *n_bytes, char byte_base[],
                              int *byte_pos, const int byte_max )
{}


/*@
  TRACE_Get_position - Return the current position in an trace file

  Input Parameter:
. fp - Trace file handle

  Output Parameter:
. offset - Current file offset.

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.


  Notes:
  This routine and 'TRACE_Set_position' are used in the construction of an
  annotated Slog file.  In an annotated Slog file, the Slog file records the
  location in the original trace file of the records, rather than making a
  copy of the records.  

  If the trace file is actually a collection of files, then that information
  should be encoded within the position.  
  @*/
int TRACE_Get_position( TRACE_file fp, TRACE_int64_t *offset )
{}

/*@
  TRACE_Set_position - Set the current position of a trace file

  Input Parameters:
+ fp - Trace file handle
- offset - Position to set file at

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  The file refered to here is relative to the 'filespec' given in a 
  'TRACE_Open' call.  If that 'filespec' describes a collection of real files,
  then this calls sets the position to the correct location in the correct
  real file.
@*/
int TRACE_Set_position( TRACE_file fp, TRACE_int64_t offset )
{}

/*@
  TRACE_Get_err_string - Return the error string corresponding to an error code

  Input Parameter:
. ierr - Error code returned by a TRACE routine

  Return Value:
  Error message string.

  Notes:
  This routine is responsible for providing internationalized (translated)
  error strings.  Implementors may want to consider the GNU 'gettext' style
  functions.  To avoid returning messages of the form 'Message catalog not 
  found', the message catalog routines such as 'catopen' and 'catgets' should
  not be used unless a provision is made to return a message string if no
  message catalog can be found.   The help message for the TRACE-API 
  implementation should be stored at ierr=0, so the calling program of
  TRACE-API knows if it should exit the program normally.
  
 @*/
char *TRACE_Get_err_string( int ierr )
{}

/* 
 * The following allow the input api to specify how to identify the
 * y-axis coordinates
 */
/*@
  TRACE_Peek_next_ycoordmap - Get the size and the description
  of the y-axis coordinate map

  Input Parameter:
. fp - Pointer to a trace file handle

  Output Parameters:
+ n_rows - Number of rows of the y-axis coordinate map
. n_columns - Number of columns of the Yaxis coordinate map
. max_column_name - The maximum length of the column name arrays, i.e.
                    max_column_name = MAX( { column_name[i] } )
. max_title_name - Title string for this map
- n_methodIDs - Number of Method IDs associated with this map

  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  Both 'max_column_name' and 'max_title_name' includes the NULL character
  needed at the end of the 'title_name' and 'column_names[i]' used in
  'TRACE_Get_next_ycoordmap()'
  @*/
int TRACE_Peek_next_ycoordmap( TRACE_file fp,
                               int *n_rows, int *n_columns,
                               int *max_column_name,
                               int *max_title_name,
                               int *n_methodIDs )
{}

/*@
  TRACE_Get_next_ycoordmap - Return the content of a y-axis coordinate map 


  Output Parameters:

  Input Parameters:
+ fp - Pointer to a trace file handle
. coordmap_max - Allocated size of 'coordmap_base' array
- methodID_max - Allocated size of 'methodID_base' array

  Input/Output Parameters:
+ coordmap_pos - On input, the first free location in 'coordmap_base'.
                 Updated on output to the new first free location.
- methodID_pos - On input, the first available position in 'methodID_base'
                 On output, changed to indicate the new first available
                 position.

  Output Parameters:
+ title_name - character array of length, 'max_title_name', is assumed 
               on input, where 'max_title_name' is defined by 
               'TRACE_Peek_next_ycoordmap()'.  The title name of this 
               map which is NULL terminated will be stored in this
               character array on output.
. column_names - an array of character arrays to store the column names.
                 Each character array is of length of 'max_column_name'.
                 There are 'ncolumns-1' character arrays altogether.
                 where 'ncolumns' and 'max_column_name' are returned by 
                 'TRACE_Peek_next_ycoordmap()'.  The name for the first 
                 column is assumed to be known, only the last 'ncolumns-1' 
                 columns need to be labeled.
. coordmap_sz - Total number of integers in 'coordmap[][]'.
                'coordmap_sz' = 'nrows' * 'ncolumns', 
                otherwise an error will be flagged.
                Where 'nrows' and 'ncolumns' are returned by
                'TRACE_Peek_next_ycoordmap()'
. coordmap_base - Pointer to storage to hold y-axis coordinate map.
. n_methodIDs - number of method IDs associated with this map.
- methodID_base - Pointer to storage to hold method IDs.


  Return Value:
. ierr - Returned integer error status.  It will be used as an argument
  in TRACE_Get_err_string() for possible error string.

  Notes:
  Each entry in y-axis coordinate map is assumed to be __continuously__ 
  stored in 'coordmap_base[]', i.e. every 'ncolumns' consecutive integers 
  in 'coordmap_base[]' is considered one coordmap entry.
  @*/
int TRACE_Get_next_ycoordmap( TRACE_file fp,
                              char *title_name,
                              char **column_names,
                              int *coordmap_sz, int coordmap_base[],
                              int *coordmap_pos, const int coordmap_max,
                              int *n_methodIDs, int methodID_base[],
                              int *methodID_pos, const int methodID_max )
{}
