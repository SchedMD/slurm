#ifndef _SLURM_PARSE_H_
#define	_SLURM_PARSE_H_

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else	/* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

/* 
 * slurm_parser - parse the supplied specification into keyword/value pairs
 *	only the keywords supplied will be searched for. the supplied specification
 *	is altered, overwriting the keyword and value pairs with spaces.
 * input: spec - pointer to the string of specifications
 *	sets of three values (as many sets as required): keyword, type, value 
 *	keyword - string with the keyword to search for including equal sign 
 *		(e.g. "name=")
 *	type - char with value 'd' for int, 'f' for float, 's' for string
 *	value - pointer to storage location for value (char **) for type 's'
 * output: spec - everything read is overwritten by speces
 *	value - set to read value (unchanged if keyword not found)
 *	return - 0 if no error, otherwise errno code
 * NOTE: terminate with a keyword value of "END"
 * NOTE: values of type (char *) are xfreed if non-NULL. caller must xfree any 
 *	returned value
 */
extern int slurm_parser (char *spec, ...);

#endif
