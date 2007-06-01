/*
 *  $Id: fstrutils.c,v 1.1 2000/07/03 20:02:20 gropp Exp $
 *
 *  (C) 2000 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 *
 */

/*
 * Update log
 * Nov 29 1996 jcownie@dolphinics.com: Created to handle fortran string conversions.
 *
 */

#include "mpi_fortimpl.h"
#if defined(STDC_HEADERS) || defined(HAVE_STRING_H)
#include <string.h>
#endif

/*
   MPIR_fstr2cstr - Convert a Fortran string into a C string.

   Input Parameters: 
   
+  res     - Pointer to the result space
.  resLen  - Length of result space
.  src     - The Fortran string
-  srcLen  - Length of the Fortran string

   The result is 1 if the assignment was possible without truncation,
   zero otherwise.

   Trailing blanks are removed from the Fortran string.
*/
int MPIR_fstr2cstr (
	char * res,
	long reslen,
	char *src,
	long srclen)
{
  /* Count the trailing blanks on the Fortran string */
  char *p = src + srclen;

  while (p > src && *--p == ' ')
    ;

  /* Assign the actual source length after trailing blanks are stripped */
  if (p == src && *p == ' ') {
      /* Special case of an all blank string */
      if (reslen == 0) return 0;
      res[0] = 0;
      return 1;
  }
  srclen = p-src+1;

  /* Check for overflow in the output string */
  if (reslen-1 < srclen)
    { /* It overflowed, truncate */
      strncpy(res, src, reslen-1);
      res[reslen-1] = 0;
      return 0;
    }
  else
    { /* It's OK, we can put it in */
      strncpy(res, src, srclen);
      /* Make sure that the string is null terminated */
      res[srclen] = 0;
      return 1;
    }
}

/*
   MPIR_cstr2fstr - Convert a C string into a Fortran string.

   Input Parameters: 
   
.  res     - Pointer to the result space
.  resLen  - Length of result space
.  src     - The C string

   The result is 1 if the assignment was possible without truncation,
   zero otherwise.

   Blank padding is added to the Fortran string as required.
*/
int MPIR_cstr2fstr ( 
	char * res,
	long reslen,
	char * src)
{
  long srclen = strlen(src);

  /* Does it need truncation ? */
  if (srclen <= reslen)
    { /* No, we can assign it all */
      char *p = res+srclen;

      strncpy(res, src, srclen);

      /* But it may need blank padding */
      while (p < res+reslen)
	*p++ = ' ';
      
      return 1;
    }
  else
    { /* Needs to be truncated */
      strncpy(res, src, reslen);
      return 0;
    }
}
