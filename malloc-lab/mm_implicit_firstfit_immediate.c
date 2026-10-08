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

// Premade Macros ===========================================================
#define ALIGNMENT 8                                      // Single word (4) or double word (8) alignment
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)  // Rounds up to the nearest multiple of ALIGNMENT
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))              // 

// New Macros & Scalar Variable =======================================================
#define WSIZE     4          // Word, header, footer size (4 bytes)
#define DSIZE     8          // Double word size (8 bytes)
#define CHUNKSIZE (1<<12)    // Size of the initial free block and the default amount to grow the heap (2^12 bytes = 4096 bytes = 4KB) 
#define MAX(x, y) ((x) > (y)? (x) : (y))

#define PACK_HDR(size, alloc)  ((size) | (alloc))            // ⭐ Combine a size and an allocated bit into one header/footer value. | is bitwise OR

#define GET(p)        (*(unsigned int *)(p))             // Read one 4 byte word at address p. Used for GET_SIZE
#define PUT_VAL(p, val)   (*(unsigned int *)(p) = (val))     // ⭐ Write one 4 byte word at address p 
// Cast to (unsigned int *) since p is usually (void *), which can't be dereferenced

#define GET_SIZE(p)   (GET(p) & ~0x7)                    // ⭐ Get size field from address p. Clear the low 3 bits with bitwise AND(&).
#define GET_ALLOC(p)  (GET(p) & 0x1)                     // ⭐ Get allocated flag from address p. Keep only the lowest bit

#define HDRP(bp)      ((char *)(bp) - WSIZE)                            // ⭐ Compute address of bp's header. Header is 1 word before bp
#define FTRP(bp)      ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)       // ⭐ Compute address of bp's footer. Footer is at (bp + size - 8)

#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) // ⭐ Compute address of next block. bp + (this block's size)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) // ⭐ Compute address of prev block. bp - (previous block's size)

static char *heap_listp;    // points to the prologue block (between its header and footer)




/* INIT ======================================================================
    mm_init - initialize the malloc package. Creates a heap with an initial free block


    heap_listp = mem_sbrk(4*WSIZE)
        get 16 byte memory for padding(4 byte), prologue header(4byte), prologue footer(4byte), epilogue header(4byte)
        check for -1 because it means sbrk has failed
    

    

*/

int mm_init(void) {
    // Create the initial empty heap
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) {return -1;}

    PUT_VAL(heap_listp, 0);                            // Alignment padding : Fill padding with 0
    PUT_VAL(heap_listp + (1*WSIZE), PACK_HDR(DSIZE, 1));   // Prologue header : Write block size and allocated flag to header. Block size is 8bytes from header and footer.
    PUT_VAL(heap_listp + (2*WSIZE), PACK_HDR(DSIZE, 1));   // Prologue footer
    PUT_VAL(heap_listp + (3*WSIZE), PACK_HDR(0, 1));       // Epilogue header : Terminating header. Size 0 allocated block made of just a header*/
    heap_listp += (2*WSIZE);                       // Set heap_listp to between prologue's header and footer

    // Extend the empty heap with a free block of CHUNKSIZE bytes
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}



/* EXTEND ======================================================================
    
*/
void *extend_heap(size_t words) {
    char *bp;     // block pointer
    size_t size;

    /* Allocate an even number of words to maintain alignment */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    /* Initialize free block header/footer and the epilogue header */
    PUT_VAL(HDRP(bp), PACK_HDR(size, 0));          /* Free block header */
    PUT_VAL(FTRP(bp), PACK_HDR(size, 0));          /* Free block footer */
    PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK_HDR(0, 1));  /* New epilogue header */

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}




/* MALLOC ====================================================================
mm_malloc - Allocate a block by incrementing the brk pointer.
Always allocate a block whose size is a multiple of the alignment.

*/
void *mm_malloc(size_t size) {
    size_t asize;               // Adjusted block size (address alignment)
    size_t extendsize;          // Amount to extend heap if no fit
    char *bp;                   // block pointer

    if (size == 0) {return NULL;} // Ignore spurious requests

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE) {asize = 2*DSIZE;}
    else {asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);}

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




void *find_fit(size_t asize) {
    char *bp = heap_listp + (2*WSIZE);     // Set block pointer to the payload(after header) of the first regular block
    // Traverse free list until epilogue is reached (i.e. size is 0) or free block of asize is found
    // If free block of size asize is found, return bp
    // If not and we reach epilogue, return NULL
    while (GET_SIZE(HDRP(bp)) > 0) {
        if (GET_ALLOC(HDRP(bp))==0 && GET_SIZE(HDRP(bp))>=asize) {return bp;}
        bp = NEXT_BLKP(bp);
    }
    return NULL;
}


// Split the block and allocate. Return pointer to the payload(after header) of the allocated block
void place(void *bp, size_t asize) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT_VAL(HDRP(bp), PACK_HDR(asize, 1));
    PUT_VAL(FTRP(bp), PACK_HDR(asize, 1));  // Ftr is calculated from Hdr info which is already updated from previous line
    if (size > asize) {
        PUT_VAL(HDRP(NEXT_BLKP(bp)), PACK_HDR(size - asize, 0));
        PUT_VAL(FTRP(NEXT_BLKP(bp)), PACK_HDR(size - asize, 0));
    }
}



/* FREE ======================================================================
mm_free - Freeing a block does nothing.

*/
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT_VAL(HDRP(bp), PACK_HDR(size, 0));
    PUT_VAL(FTRP(bp), PACK_HDR(size, 0));
    coalesce(bp);
}



/* COALESCE ======================================================================

*/
void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT_VAL(HDRP(bp), PACK_HDR(size, 0));
        PUT_VAL(FTRP(bp), PACK_HDR(size, 0));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT_VAL(FTRP(bp), PACK_HDR(size, 0));
        PUT_VAL(HDRP(PREV_BLKP(bp)), PACK_HDR(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                     /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) +
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT_VAL(HDRP(PREV_BLKP(bp)), PACK_HDR(size, 0));
        PUT_VAL(FTRP(NEXT_BLKP(bp)), PACK_HDR(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}




/* REALLOC ====================================================================
mm_realloc - Implemented simply in terms of mm_malloc and mm_free

*/
void *mm_realloc(void *bp, size_t size) {
    void *oldptr = bp;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}