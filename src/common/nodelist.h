/* $Id$ */

/* nodelist parsing and utility functions
 *
 * Author: Moe Jette <jette@llnl.gov>
 */

#ifndef _NODELIST_H
#define _NODELIST_H

extern int * bitfmt2int (char *bit_str_ptr);
extern int parse_node_names(char *name, char **fmt, int *start, int *end, int *count); 

#endif /* !_NODELIST_H */
