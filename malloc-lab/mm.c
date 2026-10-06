/* 
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"
#include <signal.h>

team_t team = {
    /* Team name */
    "Krafton Jungle",
    /* First member's full name */
    "안다빈",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

//! Premade Macros =====================================================================================
#define ALIGNMENT 8                                      // Single word (4) or double word (8) alignment
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)  // Rounds up to the nearest multiple of ALIGNMENT
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))              // Always 8 on 64 bit

//! New Macros & Scalar Variable =======================================================================
#define WSIZE     4          // Word, header, footer size (4 bytes)
#define DSIZE     8          // Double word size (8 bytes)
#define CHUNKSIZE (1<<12)    // Size of the initial free block and the default amount to grow the heap (2^12 bytes = 4096 bytes = 4KB) 
#define MAX(x, y) ((x) > (y)? (x) : (y))

#define GET_VAL(p)                  (*(unsigned int *)(p))                      // Read one 4 byte word at address p. Used for GET_SIZE
#define PUT_VAL(p, val)             (*(unsigned int *)(p) = (val))              // Write one 4 byte word at p 
#define PACK_HDR(size, prev, cur)   ((size) | ((prev) <<1) | (cur))             // Combine size, prev alloc (1 or 0), cur alloc (1 or 0) into header
#define PACK_FTR(size, alloc)       ((size) | (alloc))                          // Combine size, cur alloc (1 or 0) into footer

#define UPDATE_SIZE(p, size)        (PUT_VAL(p, ((GET_VAL(p) & 0x7) | (size))))          // Write in new size (29 bits)
#define UPDATE_PREV_ALLOC(p, alloc) (PUT_VAL(p, ((GET_VAL(p) & ~0x2) | ((alloc) <<1))))  // Write in new bit 1
#define UPDATE_CUR_ALLOC(p, alloc)  (PUT_VAL(p, (GET_VAL(p) & ~0x1) | (alloc)))          // Write in new bit 0

#define GET_PTR(p)        ((char *)*(unsigned long *)(p))                       // Get char pointer that p points to
#define PUT_PTR(p, ptr)   (*(unsigned long *)(p) = (unsigned long)(ptr))        // Write in address at p

#define GET_SIZE(p)       (GET_VAL(p) & ~0x7)                                   // Get size field. Clear the low 3 bits with bitwise AND(&).
#define GET_PREV_ALLOC(p) ((GET_VAL(p) & 0x2) >>1)                              // Get allocated flag of previuos block. Returns 1 or 0
#define GET_CUR_ALLOC(p)  (GET_VAL(p) & 0x1)                                    // Get allocated flag of current block

#define HDRP(bp)          ((char *)(bp) - WSIZE)                                // Compute address of bp's header. Header is 1 word before bp
#define FTRP(bp)          ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)           // Compute address of bp's footer. Footer is at (bp + size - 8)
#define PRED(bp)          ((char *)(bp))                                        // Compute address of bp's predecessor. Same as bp
#define SUCC(bp)          ((char *)(bp) + DSIZE)                                // Compute address of bp's successor. 8 byte after bp
  
#define NEXT_BLKP(bp)     ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))     // Compute address of next block. bp + (block size obtained from header)
#define PREV_BLKP(bp)     ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))     // Compute address of prev block. bp - (block size obtained from footer)
                                                                                // ONLY CALL WHEN PREV BLOCK IS FREE! ALLOCATED BLOCK HAS NO FOOTER
static char *heap_listp;    // points to the prologue block (between its header and footer)
static char *free_head;     // points to the first free block. always insert new free block through the front



//! INITIALIZE HEAP ======================================================================================
/*
-  mm_init : initialize the malloc package. Creates a heap with an initial free block
- [ header 4B ][ pred 8B ][ succ 8B ][ footer 4B ] = 24 bytes
*/
int mm_init(void) {
    // Create the initial empty heap
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) {return -1;}
    PUT_VAL(heap_listp, 0);                                  // Alignment padding : Fill padding with 0
    PUT_VAL(heap_listp + (1*WSIZE), PACK_HDR(DSIZE, 0, 1));  // Prologue header : block size(8), prev alloc(0), cur alloc(1)
    PUT_VAL(heap_listp + (2*WSIZE), PACK_FTR(DSIZE, 1));     // Prologue footer : block size(8), cur alloc(1)
    PUT_VAL(heap_listp + (3*WSIZE), PACK_HDR(0, 1, 1));      // Epilogue header : Terminating header / block size(0), prev alloc(0), cur alloc(1)
    heap_listp += (2*WSIZE);                                 // Set heap_listp to between prologue's header and footer
    free_head = NULL;                                        // Set head of free list to NULL

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) {return -1;}
    return 0;
}


//! EXTEND HEAP =========================================================================================
void *extend_heap(size_t words) {
    char *bp;
    size_t size;
    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {return NULL;}

    //* bp is last byte of heap returned by mem_sbrk. i.e. start of epilogue
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));

    /* Initialize overhead of new free block + move epilogue*/
    PUT_VAL(HDRP(bp), PACK_HDR(size, prev_alloc, 0));   // 1. Set HEADER
    PUT_VAL(FTRP(bp), PACK_FTR(size, 0));               // 2. Set FOOTER (ftr is found from size in hdr)
    insertFreeList(bp);                                 // 3. Insert new free block to free list
    PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK_HDR(0, 0, 1));    // 4. Set new EPILOGUE


    // printf("✅ after extend —————————————————————————————————————————————————————————————\n");
    // print_heap();
    /* Coalesce if the previous block was free */
    return coalesce(bp);
}



//! MALLOC ============================================================================================
/* 
- mm_malloc : Allocate a block by incrementing the brk pointer.
- Always allocates a block whose size is a multiple of the alignment.
*/
void *mm_malloc(size_t size) {
    size_t asize;               // Adjusted block size (address alignment)
    size_t extendsize;          // Amount to extend heap if no fit
    char *bp;                   // block pointer
    if (size == 0) {return NULL;} // Ignore spurious requests


    /* Adjust block size to include overhead and alignment reqs. 
       NOTE : 
       The minimum size for free block is 24 bytes (hdr 4, prd 8, suc 8, ftr 4)
       Allocated block must meet this size. Hence 6 words including header
     */
    if (size <= 5*WSIZE) {asize = 6*WSIZE;}
    else {asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);}

    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE); // size needed to allocate might be bigger than 4KB
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

//! FIT ======================================================================================================

void *find_fit(size_t asize) {
    return firstFit(asize);
}

// FIRST FIT : Traverse free list from head until fit is found
void *firstFit(size_t asize) {
    char *bp = free_head;
    // printf("first fit : search for %zu\n", asize);
    while (bp != NULL) {    // successor of last free block will be NULL
        // printf("%d\n", GET_CUR_ALLOC(HDRP(bp)));
        // printf("%d\n", GET_SIZE(HDRP(bp)));

        if ((GET_CUR_ALLOC(HDRP(bp))==0) && (GET_SIZE(HDRP(bp))>=asize)) {return bp;}
        bp = GET_PTR(SUCC(bp));
    }
    return NULL;
}

void *nextFit(size_t asize) {
    return 0;
}

void *bestFit(size_t asize) {
    return 0;
}


//! PLACE ===================================================================================================
/* 
- place : create an allocated block. remove node from free list
*/
void place(void *bp, size_t asize) {
    size_t size = GET_SIZE(HDRP(bp));   // Size of the [whole block]
    char *next_bp;                      // Next block in heap
    int next_size;

    //* 1. Split (because leftover space is enough for free overhead)
    if (size >= asize + (6*WSIZE)) {
        //* 1.1 Update header of allocated block
        // PUT_VAL(HDRP(bp), PACK_HDR(asize, GET_PREV_ALLOC(HDRP(bp)), 1));
        UPDATE_SIZE(HDRP(bp), asize);
        UPDATE_CUR_ALLOC(HDRP(bp), 1);

        //* 1.2 Update overhead of free block
        next_bp = ((char *)(bp) + asize);                   // next_bp : bp of the free block
        next_size = size - asize;                           // next_size : size of the free block
        PUT_VAL(HDRP(next_bp), PACK_HDR(next_size, 1, 0));  // Set header
        PUT_VAL(FTRP(next_bp), PACK_FTR(next_size, 0));     // Set footer

        //* 1.3 Update free list
        removeFreeList(bp);                                 // Remove old free block from freeList
        insertFreeList(next_bp);                            // Add new free block to freeList
    }

    //* 2. No Split
    else {
        //* 2.1 Update header of allocated block
        // PUT_VAL(HDRP(bp), PACK_HDR(size, GET_PREV_ALLOC(HDRP(bp)), 1));
        UPDATE_SIZE(HDRP(bp), size);
        UPDATE_CUR_ALLOC(HDRP(bp), 1);

        //* 2.2 Update free list
        removeFreeList(bp);

        //* 2.3 Update prev alloc flag of the next block
        UPDATE_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
    }
}


//! FREE & COALESCE ====================================================================================
/*
* mm_free : frees an allocated block. add node to front of free list
*/
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    UPDATE_CUR_ALLOC(HDRP(bp), 0);              // 1. Update header (only cur alloc flag)
    PUT_VAL(FTRP(bp), PACK_FTR(size, 0));       // 2. Set footer
    UPDATE_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 0);  // 3. Update prev alloc flag of the next block
    insertFreeList(bp);
    coalesce(bp);
}


/* 
- Coalesce
    - Remove both all free blocks from free List
    - Insert the merged free block back to the free List
*/
void *coalesce(void *bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_CUR_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    //* Case 1 [alloc - free - alloc] = No merging
    if (prev_alloc && next_alloc) {return bp;}

    //* Case 2 [alloc - free - free] = Merge current + next
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        removeFreeList(NEXT_BLKP(bp));
        UPDATE_SIZE(HDRP(bp), size);           // Update size in header
        UPDATE_SIZE(FTRP(bp), size);           // Update size in footer (ftr is found using size from hdr)
        removeFreeList(bp);
        insertFreeList(bp);
    }

    //* Case 3 [free - free - alloc] = Merge previous + next
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        UPDATE_SIZE(FTRP(bp), size);                      // Update size in footer
        UPDATE_SIZE(HDRP(PREV_BLKP(bp)), size);           // Update size in header
        removeFreeList(bp);
        removeFreeList(PREV_BLKP(bp));
        insertFreeList(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);                               // Set block pointer
    }

    //* Case 4 [free - free - free] = Merge all 3
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        UPDATE_SIZE(FTRP(NEXT_BLKP(bp)), size);           // Update size in footer
        UPDATE_SIZE(HDRP(PREV_BLKP(bp)), size);           // Update size in header (prev alloc, cur alloc stays the same)
        removeFreeList(bp);
        removeFreeList(PREV_BLKP(bp));
        removeFreeList(NEXT_BLKP(bp));
        insertFreeList(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);                               // Set block pointer
    }

    return bp;
}

//! FREE LIST HELPER ===================================================================================================
/* 
- NOTE : free list manipulation has nothing to do with block size. only pred & succ
- When inserting, pre-existing info in pred and succ is irrelevant. 
*/
void insertFreeList(void *bp) {
    if (free_head != NULL) {PUT_PTR(PRED(free_head), bp);}  // 1. Set PRED of old free_head to new free block
    PUT_PTR(SUCC(bp), free_head);                           // 2. Set SUCC to old free_head (same if free_head is NULL)
    PUT_PTR(PRED(bp), NULL);                                // 3. Set SUCC. (successor of extended block is always NULL)
    free_head = bp;                                         // 4. Set new free block as free_head
}

void *removeFreeList(void *bp) {
    char *pred = GET_PTR(PRED(bp));
    char *succ = GET_PTR(SUCC(bp));

    if (pred == NULL) {                 
        if (succ == NULL) {             // Case 1 : only free_head in free list. set head to NULL
            free_head = NULL;
        } else {                        // Case 2 : bp is free_head. remove from front
            free_head = succ;
            PUT_PTR(PRED(free_head), NULL);
        }
    } else {                            
        if (succ == NULL) {             // Case 3 : bp is last in free list. remove from end
            PUT_PTR(SUCC(pred), NULL);
        } else {                        // Case 4 : bp is not free_head. remove from middle
            PUT_PTR(SUCC(pred), succ);
            PUT_PTR(PRED(succ), pred);
        }
    }
    return bp;
}



//! REALLOC ======================================================================================================

void *mm_realloc(void *bp, size_t size) {
    void *old_bp = bp;
    void *new_bp;
    size_t copySize;

    new_bp = mm_malloc(size);
    if (new_bp == NULL) {return NULL;}

    copySize = *(size_t *)((char *)old_bp - SIZE_T_SIZE);
    if (size < copySize) {copySize = size;}
    memcpy(new_bp, old_bp, copySize);
    mm_free(old_bp);
    return new_bp;
}



//! PRINT HEAP & HEAP CHECK ====================================================================================

void print_heap() {
    char *bp;
    printf("✅ printHeap ————————————————————————————————————————————————————————————————\n");
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        printf("[%p] size: %u\t prev alloc : %u\t cur alloc: %u\t",
            bp,
            GET_SIZE(HDRP(bp)),
            GET_PREV_ALLOC(HDRP(bp)),
            GET_CUR_ALLOC(HDRP(bp)));
        if (GET_CUR_ALLOC(HDRP(bp)) == 0) {printf("ftr alloc: %u\n", GET_CUR_ALLOC(FTRP(bp)));}
        else {printf("\n");}
    }
}

// void *mm_check() {

// }