/* $Id$ */

/* nodelist parsing and utility functions
 *
 * Author: Moe Jette <jette@llnl.gov>
 */

#ifndef _NODELIST_H
#define _NODELIST_H

int * bitfmt2int (char *bit_str_ptr);
int parse_node_name(char *name, char **fmt, int *start, int *end, int *count); 

#endif /* !_NODELIST_H */
