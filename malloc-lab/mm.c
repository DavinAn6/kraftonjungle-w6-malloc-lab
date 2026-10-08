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
#define CHUNKSIZE (256)    // Size of the initial free block and the default amount to grow the heap (2^12 bytes = 4096 bytes = 4KB) 
#define MIN_BLOCK_SIZE 24
#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y))

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

#define SIZE_CLASS_COUNT  11
    // Class granularity. 11 size classes (2^3, 2^4, ... , 2^12, catchall for anything bigger)
#define SIZE_CLASS(size)            (MIN(MAX((32 -__builtin_clz((size) - 1)), 3), 13) - 3)   
    // Returns 0~10 value. Make sure at least 3 and at most 13
#define SIZE_CLASS_FREE_LIST(class) (heap_start + ((class) * DSIZE))
    // Compute pointer to heap position where size class's free list head is stored
#define ROUND_POW2(size)  ((size_t)1 << (SIZE_CLASS(size) + 3))

static char *heap_start;    // points to first byte of heap
static char *heap_listp;    // points to the prologue block (between its header and footer)
static char *heap_end;      // points to start of the epilogue


/* 
# Heap Design : [< 2^3] [2^4]...[2^12] [2^12 <]  [Pad] [ProHdr|ProFtr]...[EpiHdr]
#               <——— array of free lists —————>  <————— same as explicit ———————>

# __builtin_clz(size) : count leading zeros from the left
    GCC built in function. Counts the number of leading zeros in the binary representation of size.
    Subtracting from 31 gives the position of the highest set bit (0-indexed from the right).
    Equivalent to floor(log2(size)), which maps a size to its power-of-2 size class index.
    Since unsigned int is 32 bits, function returns out of 32 bits.
    Example : __builtin_clz(8) = 28 ————> 31-28 = 3 
*/



//! INITIALIZE HEAP ======================================================================================
/* 
# Array of Free Lists
    Can't create global or static compound data structures
    Embed the array at the start of the heap itself as extra overhead
    
# Size Classes
    (1-8), (9-16), (17-32), (33-64),..., (2049-4096)
    2^3 ~ 2^12 —> 10 free lists
*/

int mm_init(void) {
    // Create the initial empty heap
    if ((heap_listp = mem_sbrk((4*WSIZE) + (DSIZE*SIZE_CLASS_COUNT))) == (void *)-1) {return -1;}
    heap_start = heap_listp;

    // Here, heap_listp is the first byte of the heap
    // Traverse array of free list and set free list head of each size class to NULL
    for (int i=0; i < SIZE_CLASS_COUNT; i++) {
        PUT_PTR(heap_listp, NULL);
        heap_listp += (DSIZE);
    }
    
    // heap_listp is now at padding. Proceed the same as before
    PUT_VAL(heap_listp, 0);                                  // Alignment padding : Fill padding with 0
    PUT_VAL(heap_listp + (1*WSIZE), PACK_HDR(DSIZE, 0, 1));  // Prologue header : block size(8), prev alloc(0), cur alloc(1)
    PUT_VAL(heap_listp + (2*WSIZE), PACK_FTR(DSIZE, 1));     // Prologue footer : block size(8), cur alloc(1)
    PUT_VAL(heap_listp + (3*WSIZE), PACK_HDR(0, 1, 1));      // Epilogue header : Terminating header / block size(0), prev alloc(0), cur alloc(1)
    heap_listp += (2*WSIZE);                                 // Set heap_listp to between prologue's header and footer

    // printf("✅ Initial Extend —————————————————————————————————————————————————————————————\n");
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

    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));


    /* Initialize overhead of new free block + move epilogue*/
    PUT_VAL(HDRP(bp), PACK_HDR(size, prev_alloc, 0));   // 1. Set HEADER
    PUT_VAL(FTRP(bp), PACK_FTR(size, 0));               // 2. Set FOOTER (ftr is found from size in hdr)
    PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK_HDR(0, 0, 1));    // 4. Set new EPILOGUE
    heap_end = NEXT_BLKP(bp);
    insertFreeList(bp);

    // printf("✅ After extend —————————————————————————————————————————————————————————————\n");
    // print_heap();
    /* Coalesce if the previous block was free */
    return coalesce(bp);
}



//! MALLOC ============================================================================================
void *mm_malloc(size_t size) {
    size_t asize;                   // Adjusted block size (address alignment)
    size_t extendsize;              // Amount to extend heap if no fit
    char *bp;                       // block pointer
    if (size == 0) {return NULL;}   // Ignore spurious requests


    if (size <= MIN_BLOCK_SIZE - WSIZE) {asize = MIN_BLOCK_SIZE;}
    else {asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);}

    // printf("✅ Malloc : size %zu —————————————————————————————————————————————————————————————\n", asize);
    /* Search the free list for a fit */
    if ((bp = find_fit(asize)) != NULL) {
        bp = place(bp, asize);
        return bp;
    }

    /* No fit found. Get more memory and place the block */
    if (GET_PREV_ALLOC(HDRP(heap_end)) == 0) {extendsize = asize - GET_SIZE(HDRP(PREV_BLKP(heap_end)));}
    else {extendsize = asize;}
    extendsize = MAX(extendsize, MIN_BLOCK_SIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL) {return NULL;}
    bp = place(bp, asize);
    return bp;
}

//! FIT ======================================================================================================
void *find_fit(size_t asize) {
    return bestFit(asize);
}

// FIRST FIT : Traverse free list from head until fit is found
void *firstFit(size_t asize) {
    int class = SIZE_CLASS(asize);  // between 0~10. size bigger than 2^12 goes into catch all bucket
    char *bp;
    // printf("✅ [First Fit] asize : %zu\t size class : 2^%u\n", asize, class+3);

    // If class is 3, i traverses (3 ~ 12)
    // free_head starts at size class 3 and moves to next size class through offset 8 bytes
    // Keep searching up until largest size class
    for (int i = class; i < SIZE_CLASS_COUNT; i++) {
        bp = GET_PTR(SIZE_CLASS_FREE_LIST(i));
        // printf("i : %u\n", i+3);
        while (bp != NULL) {    // successor of last free block will be NULL
            // printf("bp: %p\t  cur_alloc: %u\n", bp, GET_CUR_ALLOC(HDRP(bp)));
            // printf("size: %zu\n", GET_SIZE(HDRP(bp)));
            if ((GET_CUR_ALLOC(HDRP(bp))==0) && (GET_SIZE(HDRP(bp))>=asize)) {
                return bp;}
            bp = GET_PTR(SUCC(bp));
        }
    }
    return NULL;
}

void *nextFit(size_t asize) {
    return 0;
}

void *bestFit(size_t asize) {
    int class = SIZE_CLASS(asize);  // between 0~10. size bigger than 2^12 goes into catch all bucket
    char *bp;
    char *min = NULL;

    for (int i = class; i < SIZE_CLASS_COUNT; i++) {
        bp = GET_PTR(SIZE_CLASS_FREE_LIST(i));
        while (bp != NULL) {    // successor of last free block will be NULL
            if ((GET_CUR_ALLOC(HDRP(bp))==0) && (GET_SIZE(HDRP(bp))>=asize)) {
                if (min == NULL) {min = bp;}
                else {min = (GET_SIZE(HDRP(min)) < GET_SIZE(HDRP(bp))) ? min : bp;}
            }
            bp = GET_PTR(SUCC(bp));
        }
        if (min != NULL) {return min;}
    }
    return NULL;
}


//! PLACE ===================================================================================================
void *place(void *bp, size_t asize) {
    size_t size = GET_SIZE(HDRP(bp));   // Size of the [whole block]
    char *next_bp;                      // Next block in heap
    size_t remainder;

    //* 1. Split (because leftover space is enough for free overhead)
    if (size >= asize + MIN_BLOCK_SIZE) {
        remainder = size - asize;       // size of the free block
        next_bp = ((char *)(bp) + asize);                   // next_bp : bp of the free block
        PUT_VAL(HDRP(next_bp), PACK_HDR(remainder, 1, 0));  // Set header
        PUT_VAL(FTRP(next_bp), PACK_FTR(remainder, 0));     // Set footer
        removeFreeList(bp);                                 // Remove old free block from freeList
        UPDATE_SIZE(HDRP(bp), asize);
        UPDATE_CUR_ALLOC(HDRP(bp), 1);
        insertFreeList(next_bp);                            // Add new free block to freeList 
    }

    //* 2. No Split
    else {
        removeFreeList(bp);
        UPDATE_SIZE(HDRP(bp), size);
        UPDATE_CUR_ALLOC(HDRP(bp), 1);
        UPDATE_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
    }
    return bp;
}


//! FREE & COALESCE ====================================================================================
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    UPDATE_CUR_ALLOC(HDRP(bp), 0);              // 1. Update header (only cur alloc flag)
    PUT_VAL(FTRP(bp), PACK_FTR(size, 0));       // 2. Set footer
    UPDATE_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 0);  // 3. Update prev alloc flag of the next block
    insertFreeList(bp);
    bp = coalesce(bp);
}


void *coalesce(void *bp) {
    size_t prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t next_alloc = GET_CUR_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    //* Case 1 [alloc - free - alloc] = No merging
    if (prev_alloc && next_alloc) {return bp;}

    //* Case 2 [alloc - free - free] = Merge current + next
    else if (prev_alloc && !next_alloc) {
        // printf("✅ Coalesce [alloc - free - free] —————————————————————————————————————————————————————————————\n");
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        removeFreeList(NEXT_BLKP(bp));
        removeFreeList(bp);
        UPDATE_SIZE(HDRP(bp), size);           // Update size in header
        UPDATE_SIZE(FTRP(bp), size);           // Update size in footer (ftr is found using size from hdr)
        insertFreeList(bp);
    }

    //* Case 3 [free - free - alloc] = Merge previous + next
    else if (!prev_alloc && next_alloc) {
        // printf("✅ Coalesce [free - free - alloc] —————————————————————————————————————————————————————————————\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        removeFreeList(bp);
        removeFreeList(PREV_BLKP(bp));
        UPDATE_SIZE(FTRP(bp), size);            // increase cur block footer
        UPDATE_SIZE(HDRP(PREV_BLKP(bp)), size); // increase prev block footer (head found through prev block's old footer size)
        bp = PREV_BLKP(bp);
        insertFreeList(bp);
    }

    //* Case 4 [free - free - free] = Merge all 3
    else {
        // printf("✅ Coalesce [free - free - alloc] —————————————————————————————————————————————————————————————\n");
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        removeFreeList(bp);
        removeFreeList(PREV_BLKP(bp));
        removeFreeList(NEXT_BLKP(bp));
        UPDATE_SIZE(FTRP(NEXT_BLKP(bp)), size);           // Update size in footer
        UPDATE_SIZE(HDRP(PREV_BLKP(bp)), size);           // Update size in header (prev alloc, cur alloc stays the same)
        insertFreeList(PREV_BLKP(bp));
        bp = PREV_BLKP(bp);                               // Set block pointer
    }
    return bp;
}

//! FREE LIST HELPER ===================================================================================================
void insertFreeList(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int class = SIZE_CLASS(size);
    char *free_head = GET_PTR(SIZE_CLASS_FREE_LIST(class));
    if (free_head != NULL) {PUT_PTR(PRED(free_head), bp);}  // 1. Set PRED of old free_head to new free block
    PUT_PTR(SUCC(bp), free_head);                           // 2. Set SUCC to old free_head (same if free_head is NULL)
    PUT_PTR(PRED(bp), NULL);                                // 3. Set SUCC. (successor of extended block is always NULL)
    PUT_PTR(SIZE_CLASS_FREE_LIST(class), bp);               // 4. Set new free block as 
}

void *removeFreeList(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int class = SIZE_CLASS(size);
    char *pred = GET_PTR(PRED(bp));
    char *succ = GET_PTR(SUCC(bp));

    if (pred == NULL) {                 
        if (succ == NULL) {             // Case 1 : only one block in free list. set head to NULL
            PUT_PTR(SIZE_CLASS_FREE_LIST(class), NULL);
        } else {                        // Case 2 : head of free list. set successor to NULL
            PUT_PTR(SIZE_CLASS_FREE_LIST(class), succ);
            PUT_PTR(PRED(succ), NULL);
        }
    } else {                            
        if (succ == NULL) {             // Case 3 : last block of free list. set predecessor's successor to NULL
            PUT_PTR(SUCC(pred), NULL);
        } else {                        // Case 4 : middle of free list. set pred & succ's pointer to skip bp
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
    size_t asize;
    size_t curr_size = GET_SIZE(HDRP(old_bp));
    int next_alloc = GET_CUR_ALLOC(HDRP(NEXT_BLKP(old_bp)));
    size_t next_size = GET_SIZE(HDRP(NEXT_BLKP(old_bp)));

    //* Edge cases
    if (bp==NULL) {return mm_malloc(size);}
    if (size==0) {return NULL;}

    //* Calculate adjusted size
    if (size <= MIN_BLOCK_SIZE - WSIZE) {asize = MIN_BLOCK_SIZE;}
    else {asize = DSIZE * ((size + (WSIZE) + (DSIZE-1)) / DSIZE);}

    //* Optimization 1 : if new size fits current block, return same pointer
    if (curr_size >= asize) {return old_bp;}  

    //* Optimization 2 : if new size fits (current + next) block, merge and return pointer
    if ((next_alloc == 0) && ((curr_size + next_size) >= asize)) {return merge_next(old_bp);}

    //* Optimization 3 : allocate a new block, copy, free the old one
    if ((new_bp = mm_malloc(size)) == NULL) return NULL;
    memcpy(new_bp, old_bp, curr_size - WSIZE);   // new block already has hdr. only copy payload
    mm_free(old_bp);
    return new_bp;
}



void *merge_next(void *bp) {
    size_t size = GET_SIZE(HDRP(bp)) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
    removeFreeList(NEXT_BLKP(bp));
    UPDATE_SIZE(HDRP(bp), size);           // Update size in header
    UPDATE_PREV_ALLOC(HDRP(NEXT_BLKP(bp)), 1);
    return bp;
}



//! PRINT HEAP & HEAP CHECK ====================================================================================
void print_heap() {
    char *bp;
    bp = heap_start;
    for (int i = 0; i < SIZE_CLASS_COUNT; i++) {
        printf("[%p] size class : 2^%u\t  free head : %p\n", bp, i+3, GET_PTR(SIZE_CLASS_FREE_LIST(i)));
        bp += DSIZE;
    }
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
