#include "mpdconf.h"
#include <stdlib.h>
#include <string.h>
#include "set.h"

/*@ 
   set_create - create a new set

Input arguments:
+ maxsize - The maximum size of the set
. elemsize - The size of each element
- cmp - A comparison function for the particular element type (qsort semantics)

Output arguments:
None

Returns:
A new set structure if successful, NULL otherwise.
@*/
struct set *set_create(const size_t maxsize, const size_t elemsize, 
	            const cmp_function cmp)
{
    struct set *tmp;
    
    /* Sanity check */
    if (maxsize == 0 || elemsize == 0 || cmp == NULL)
		return NULL;
    
    tmp = (struct set *)malloc(sizeof(struct set));
    if (tmp == NULL)
		return NULL;

    tmp->buf = malloc(maxsize*elemsize);
    if (tmp->buf == NULL){
		free(tmp);
		return NULL;
    }

    tmp->maxsize = maxsize;
    tmp->elemsize= elemsize;
    tmp->cmp = cmp;
    tmp->size = 0;
    tmp->idx = 0;
    tmp->need_sort = 0;

    return tmp;
}

/*@ 
   set_destroy - destroy a set

Input arguments:
+ s - The set to be destroyed

Output arguments:
None

Returns:
0 on success, -1 if the destruction was unsuccessful.
@*/
int set_destroy(struct set * const s)
{
    if (s == NULL)
	return -1;

    free(s->buf);
    free(s);

    return 0;
}

/*@ 
   set_copy - copy a set

Input arguments:
+ s - The set to be copied

Output arguments:
None

Returns:
A copy of the input set
@*/
struct set *set_copy(const struct set * const s)
{
    struct set *tmp;

    tmp = set_create(s->maxsize, s->elemsize, s->cmp);
    if (tmp == NULL)
	return NULL;

    memcpy(tmp->buf, s->buf, s->size*s->elemsize);
    tmp->size = s->size;

    return tmp;
}

/*@ 
   set_exists - determine if an element is in a set

Input arguments:
+ s - The set to be searched
- elem - The element for which to search

Output arguments:
None

Returns:
The element if it was found, NULL otherwise.
@*/
void *set_exists(struct set * const s, const void * const elem)
{
    if (s == NULL || elem == NULL)
	return NULL;

    if (s->need_sort){
	qsort(s->buf, s->size, s->elemsize, s->cmp);
	s->need_sort = 0;
    }
    
    return bsearch(elem, s->buf, s->size, s->elemsize, s->cmp);
}

/*@ 
   set_insert - insert an element into a set

Input arguments:
+ s - The set in which the element will be inserted
- elem - The element which will be inserted

Output arguments:
None

Returns:
0 on success, -1 on failure.
@*/
int set_insert(struct set * const s, const void * const elem)
{
    if (s == NULL || elem == NULL)
	return -1;
    
    if (s->size >= s->maxsize)
	return -1;

    /* Don't insert the same element twice */
    if (set_exists(s, elem))
	return 0;

    memcpy(s->buf+(s->size*s->elemsize), elem, s->elemsize);
    s->size++;

    /* Don't sort here -- only sort when we need something */
    s->need_sort = 1;

    return 0;
}

/*@ 
   set_delete - delete an element from a set

Input arguments:
+ s - The set from which the element will be deleted
- elem - The element which will be deleted 

Output arguments:
None

Returns:
0 on success, -1 on failure.
@*/
int set_delete(struct set * const s, const void * const elem)
{
    char *e;

    if (s == NULL || elem == NULL)
	return -1;
    
    if (s->need_sort){
	qsort(s->buf, s->size, s->elemsize, s->cmp);
	s->need_sort = 0;
    }
    
    e = bsearch(elem, s->buf, s->size, s->elemsize, s->cmp);
    if (e == NULL)
	return -1;

    /* We found it, so shift all the data after this element into  */
    /* this element's spot.  No need to resort.  Use memmove() for */
    /* overlap protection. */
    memmove(e, e+s->elemsize, (s->elemsize*s->size)-(size_t)e);
    s->size--;

    return 0;
}

/*@ 
   set_union - find the union of two sets

Input arguments:
+ s - The first set
- t - The second set

Output arguments:
None

Returns:
A new set that is the union of the two input sets if successful,
NULL otherwise.

Notes:
s and t must contain the same types of elements
@*/
struct set *set_union(struct set *s, struct set *t)
{
    void * e;
    struct set *tmp, *u, *v; 

    /* Try to make sure that these two sets contain the same types */
    if (s->cmp != t->cmp || s->elemsize != t->elemsize)
	return NULL;
    
    /* Whichever set we iterate over should be smaller because iteration is O(n) */
    /* while bsearch is O(log n) */
    if (s->size < t->size)
	u = s, v = t;
    else 
	u = t, v = s;

    tmp = set_copy(v);
    if (tmp == NULL)
	return NULL;
    
    /* Insert all the elements of u not in v */
    for (set_reset(u), e = set_next(u); e != NULL; e = set_next(u)){
	if (!set_exists(v, e))
	    set_insert(tmp, e);
    }

    return tmp;
}

/*@ 
   set_union - find the difference of two sets

Input arguments:
+ s - The first set
- t - The second set

Output arguments:
None

Returns:
A new set that contains all the elements of s not in t,
NULL otherwise.

Notes:
s and t must contain the same types of elements
@*/
struct set *set_diff(struct set *s, struct set *t)
{
    void * e;
    struct set *tmp;

    /* Try to make sure that these two sets contain the same types */
    if (s->cmp != t->cmp || s->elemsize != t->elemsize)
	return NULL;
    
    tmp = set_create(s->maxsize+t->maxsize, s->elemsize, s->cmp);
    if (tmp == NULL)
	return NULL;

    /* Insert all the elements of s not in t */
    for (set_reset(s), e = set_next(s); e != NULL; e = set_next(s)){
	if (!set_exists(t, e))
	    set_insert(tmp, e);
    }

    return tmp;
}

/*@ 
   set_reset - reset the iteration index on a set

Input arguments:
+ s - The set to be reset

Output arguments:
None

Returns:
0 if successful, -1 otherwise

Notes:
This should always be run before iterating over a set
@*/
int set_reset(struct set *s)
{
    if (s == NULL)
		return -1;

    s->idx = 0;

    if (s->need_sort){
		qsort(s->buf, s->size, s->elemsize, s->cmp);
		s->need_sort = 0;
    }

    return 0;
}

/*@ 
   set_reset_reverse - reset the iteration index on a set
   	for reversal

Input arguments:
+ s - The set to be reset

Output arguments:
None

Returns:
0 if successful, -1 otherwise

Notes:
This should always be run before iterating over a set
backwards
@*/
int set_reset_reverse(struct set *s)
{
    if (s == NULL)
		return -1;

    s->idx = s->size-1;

    if (s->need_sort){
		qsort(s->buf, s->size, s->elemsize, s->cmp);
		s->need_sort = 0;
    }

    return 0;
}

/*@ 
   set_next - return the next element in a set (while iterating)

Input arguments:
+ s - The set over which to iterate

Output arguments:
None

Returns:
The next element in the set if successful, NULL if 
	there is no next element
@*/
void *set_next(struct set *s)
{
    void *tmp;
    
    if (s == NULL || s->idx >= s->size)
	return NULL;

    tmp = s->buf+(s->idx*s->elemsize);
    s->idx++;

    return tmp;
}

/*@ 
   set_prev - return the previous element in a set 
   	(while iterating)

Input arguments:
+ s - The set over which to iterate

Output arguments:
None

Returns:
The previous element in the set if successful, NULL if 
	there is no next element
@*/
void *set_prev(struct set *s)
{
    void *tmp;
    
    if (s == NULL || s->idx <= 0)
	return NULL;

    tmp = s->buf+(s->idx*s->elemsize);
    s->idx--;

    return tmp;
}

/*@ 
   set_equals - compare two sets

Input arguments:
- s,t - The sets

Output arguments:
None

Returns:
0 if the sets are not equal, 1 if they are equal
@*/
int set_equals(struct set *s, struct set *t)
{
	if ((s->size != t->size) || (s->elemsize != t->elemsize))
		return 0;

	set_reset(s);
	set_reset(t);
	
	if (memcmp(s->buf, t->buf, s->size * s->elemsize) == 0)
		return 1;

	return 0;
}

/*@ 
   set_size - get the size of a set

Input arguments:
- s - The set

Output arguments:
None

Returns:
The size of the set
@*/
size_t set_size(struct set *s)
{
	return s->size;
}

/*@ 
   set_maxsize - get the maximum size of a set

Input arguments:
- s - The set

Output arguments:
None

Returns:
The maximum size of the set
@*/
size_t set_maxsize(struct set *s)
{
	return s->maxsize;
}

/*@ 
   set_min - get the minimum element of a set

Input arguments:
- s - The set

Output arguments:
None

Returns:
The minimum element of the set if the set is non-empty, NULL otherwise
@*/
void *set_min(struct set *s)
{
    if (s->size == 0)
        return NULL;

    if (s->need_sort){
		qsort(s->buf, s->size, s->elemsize, s->cmp);
		s->need_sort = 0;
    }

    return (void *)s->buf;
}

/*@ 
   set_max - get the maximum element of a set

Input arguments:
- s - The set

Output arguments:
None

Returns:
The maximum element of the set if the set is non-empty, NULL otherwise
@*/
void *set_max(struct set *s)
{
    if (s->size == 0)
        return NULL;

    if (s->need_sort){
		qsort(s->buf, s->size, s->elemsize, s->cmp);
		s->need_sort = 0;
    }

    return (void *)(s->buf + ((s->size-1)*s->elemsize));
}

#ifdef DEBUG_SET_C
#include <stdio.h>
#include <time.h>

int cmpint(const void *a, const void *b)
{
    int A = *(int *)a, B = *(int *)b;

    if (A < B)
	return -1;
    if (A > B)
	return 1;
    
    return 0;
}

int main(int argc, char *argv[])
{
    int i, size = 100, *x;
    struct set *s, *t, *u;
    
    if (argc > 1)
	size = atoi(argv[1]);
    
    srand(time(NULL));

    s = set_create(size, sizeof(int), cmpint);
    t = set_create(size, sizeof(int), cmpint);
    if (s == NULL || t == NULL){
	printf("Unable to create the test set\n");
	return 1;
    }

    printf("Inserting into first set...\n");
    for (i = 0; i < size; i++){
	int y = rand() % size;

	printf("%d ", y);
	set_insert(s, &y);
    }
    printf("\n\n");

    printf("The set contains:\n");
    for (set_reset(s), x = set_next(s); x != NULL; x = set_next(s)) 
	printf("%d ", *x);
    printf("\n\n");

    printf("Inserting into second set...\n");
    for (i = 0; i < size; i++){
	int y = rand() % size;

	printf("%d ", y);
	set_insert(t, &y);
    }
    printf("\n\n");

    printf("The set contains:\n");
    for (set_reset(t), x = set_next(t); x != NULL; x = set_next(t)) 
	printf("%d ", *x);
    printf("\n\n");

    u = set_union(s, t);
    printf("The union of the two sets is:\n");
    for (set_reset(u), x = set_next(u); x != NULL; x = set_next(u)) 
	printf("%d ", *x);
    printf("\n\n");
    set_destroy(u);

    u = set_diff(s, t);
    printf("The difference of the two sets is:\n");
    for (set_reset(u), x = set_next(u); x != NULL; x = set_next(u)) 
	printf("%d ", *x);
    printf("\n\n");
    
    return 0;
}
#endif /* DEBUG_SET_C */
