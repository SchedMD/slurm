#include <stdio.h>

#ifndef _EXPANDINGLIST_H_
#define _EXPANDINGLIST_H_

/* Array implementation of exapanding lists */
/* written by Ed Karrels */

/* version 2 - all macros */

/* I've started using this technique in various places, and I don't feel
   like reinventing the wheel anymore with various data types, so let's
   see if we can generalize it and do it typeless */

/*

   expandingList.h will create the following structure:
   struct xpandList_ {
     void *list;  pointer to the list data
     int size;    size of the list
     int nused;   number of elements in used, starting at 0
   };
   typedef xpandList_ *xpandList;

   The following macros are created:

   ListCreate( listVar, type, initialSize )
     creates a list in listVar (an xpandList variable)
   ListAddItem( listVar, type, newItem )
     add newItem to the list, return the index
   ListSize( listVar, type )
     returns the size of listVar
   ListItem( listVar, type, index )
     returns item # index in listVar
   ListHeadPtr( listVar, type )
     returns the pointer to the head of the list (&(list[0]))
   ListClose( listVar, type, head, nitems )
     deallocate the list and return the head and nitems
   ListDestroy( listVar, type )
     frees the memory in use by listVar
   ListClear( listVar, type )
     clear out a list, leaving it with the same allocated space 
   ListRemoveItems( listVar, type, nitems )
     remove items from the end of a list

*/


#ifndef DEFAULT_LEN
#define DEFAULT_LEN 10
#endif

#ifndef GROWTH_FACTOR
#define GROWTH_FACTOR 2
#endif

struct xpandList_ {
  void *list;		     /* pointer to the list data */
  int size;		     /*  size of the list */
  int nused;	             /*  number of elements in used, starting at 0 */
};
typedef struct xpandList_ *xpandList;


#define ListCreate( listVar, type, initialSize ) { \
  int initialLen; \
  initialLen = initialSize; \
  if (initialLen<1) initialLen = DEFAULT_LEN; \
  (listVar) = (xpandList) malloc( sizeof( struct xpandList_ ) ); \
  if ((listVar)) { \
    (listVar)->list = (void *) malloc( sizeof( type ) * initialLen ); \
    if (!(listVar)->list) { \
      fprintf( stderr, "Could not allocate memory for expanding list\n" ); \
    } \
    (listVar)->nused = 0; \
    (listVar)->size = initialLen; \
  } else { \
    fprintf( stderr, "Could not allocate memory for expanding list\n" ); \
  } \
}



#define ListAddItem( listVar, type, newItem ) { \
  void *newPtr; \
  int newSize; \
  newPtr = (listVar)->list; \
  if ((listVar)->nused == (listVar)->size) { \
    newSize = (listVar)->size; \
    if (newSize < 1) newSize = 1; \
    newSize *= GROWTH_FACTOR; \
    newPtr = (void *) malloc( sizeof( type ) * newSize ); \
    if (!newPtr) { \
      fprintf( stderr, "Could not allocate memory for expanding list\n" ); \
    } else { \
      memcpy( newPtr, (listVar)->list, sizeof( type ) * (listVar)->size ); \
      (listVar)->list = newPtr; \
      (listVar)->size = newSize; \
    } \
  } \
  if (newPtr) ((type *)((listVar)->list)) [((listVar)->nused)++] = newItem; \
}

#define ListClear( listVar, type ) {(listVar)->size = 0;}

#define ListRemoveItems( listVar, type, nitems ) { \
  (listVar)->nused -= (nitems); \
}

#define ListSize( listVar, type ) ( (listVar)->nused )

#define ListItem( listVar, type, idx ) ( ((type *)((listVar)->list)) [(idx)] )

#define ListHeadPtr( listVar, type ) ( (type *)((listVar)->list) )

#define ListClose( listVar, type, headPtr, nitems ) { \
  headPtr = ListHeadPtr( (listVar), type ); \
  nitems = ListSize( (listVar), type ); \
  free( (listVar) ); \
}

#define ListDestroy( listVar, type ) \
  {free( (listVar)->list ); free( (listVar) );}

#endif
