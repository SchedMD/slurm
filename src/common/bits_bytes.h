/*
 * definitions for use with bits_bytes.c functions
 */

#ifndef _BITS_BYTES_H_
#define	_BITS_BYTES_H_

/*
 * bitfmt2int - convert a string describing bitmap (e.g. "0-30,45,50-60") 
 *	into an array of integer (start/end) pairs terminated by -1
 *	(e.g. "0, 30, 45, 45, 50, 60, -1")
 * input: bitmap string as produced by bitstring.c : bitfmt
 * output: an array of integers
 * NOTE: the caller must free the returned memory
 */
extern int *bitfmt2int (char *bit_str_ptr) ;

#endif /* !_BITS_BYTES_H_ */
