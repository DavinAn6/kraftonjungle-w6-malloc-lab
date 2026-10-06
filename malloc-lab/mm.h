#include <stdio.h>

extern int mm_init (void);
extern void *extend_heap(size_t words);
extern void *mm_malloc (size_t size);
extern void *find_fit(size_t asize);
extern void *firstFit(size_t asize);
extern void *nextFit(size_t asize);
extern void *bestFit(size_t asize);
extern void place(void *bp, size_t asize);
extern void mm_free (void *ptr);
extern void *coalesce(void *bp);
extern void insertFreeList(void *bp);
extern void *removeFreeList(void *bp);
extern void *mm_realloc(void *ptr, size_t size);
extern void print_heap();


/* 
 * Students work in teams of one or two.  Teams enter their team name, 
 * personal names and login IDs in a struct of this
 * type in their bits.c file.
 */
typedef struct {
    char *teamname; /* ID1+ID2 or ID1 */
    char *name1;    /* full name of first member */
    char *id1;      /* login ID of first member */
    char *name2;    /* full name of second member (if any) */
    char *id2;      /* login ID of second member */
} team_t;

extern team_t team;

