#ifndef PARSE_CLIQUES_H
#define PARSE_CLIQUES_H

#ifndef MALLOC
#define MALLOC malloc
#endif
#ifndef FREE
#define FREE free
#endif

int ParseCliques(char *pszCliques, int iproc, int nproc, int *count, int **members);

#endif
