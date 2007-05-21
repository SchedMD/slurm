#ifndef SET_H
#define SET_H

typedef int (*cmp_function)(const void *, const void *);

struct set {
    char *buf;
    int maxsize, elemsize;
    int size, idx, need_sort;  /* The index is used for iterating over the set */
    cmp_function cmp;
};

#define SET_MAXSIZE(s)	((s)->maxsize)
#define SET_SIZE(s)	((s)->size)

/* Set creation and deletion */
extern struct set *set_create(const size_t, const size_t, 
	                   const cmp_function);
extern int set_destroy(struct set * const);

extern struct set *set_copy(const struct set * const);

/* Find an element in a set */
extern void *set_exists(struct set *const, const void * const);

/* Insert or delete an element from a set */
extern int set_insert(struct set * const, const void * const);
extern int set_delete(struct set * const, const void * const);

/* Find the union and difference of two sets, creating a new set */
extern struct set *set_union(struct set *, struct set *);
struct set *set_diff(struct set *, struct set *);

/* Set iteration functions.  set_reset() resets the idx pointer in a set and */
/* set_next() gets the next element in the set */
extern int set_reset(struct set *);
extern int set_reset_reverse(struct set *);
extern void *set_next(struct set *);
extern void *set_prev(struct set *);

/* See if two sets are equal */
extern int set_equals(struct set *, struct set *);

/* Get sizes associated with a set */
extern size_t set_size(struct set *);
extern size_t set_maxsize(struct set *);

/* Get the min and max elements of a set */
extern void *set_min(struct set *);
extern void *set_max(struct set *);


#endif /* SET_H */
