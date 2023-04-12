#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)

#define MAX(x, y) ((x) > (y)? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc))

#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

#define HDRP(bp)    ((char *)(bp) - WSIZE)
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)   ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp)   ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

#define PRVP(bp)    (*(void **)(bp))
#define NEXP(bp)    (*(void **)((char *)bp + WSIZE))
#define SEGLIST_INDEX 32

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team3",
    /* First member's full name */
    "Jeon-Yoojin",
    /* First member's email address */
    "jeonryu@sookmyung.ac.kr",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static void *heap_listp;
const char* seg_list[SEGLIST_INDEX];
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void append_linked_list(void *bp);
static void remove_linked_list(char *bp);
/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));

    /* 각각의 루트 초기화 */
    for(int i = 0; i < SEGLIST_INDEX ; i++) {
        seg_list[i] = NULL;
    }

    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;
    return 0;
}

/* 해당 size의 index를 찾아 주는 함수 */
static void *find_index(size_t size) {
    int index = 0;
    
    /* 2 ** index보다 size가 크고 index가 32(seglist 크기)를 넘지 않으면 index 1 증가 */
    for(index = 0; (1 << index) < size && index < SEGLIST_INDEX; index++);
    return index;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2)? (words + 1) * WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;
    
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

static void *find_fit(size_t asize)
{
    void *ptr = NULL;
    int index = find_index(asize);
    
    /* 현재 들어갈 수 있는 index는 find_index를 통해 찾은 index보다 크거나 같은 인덱스이다. */
    /* 만약 해당 index에 가용 블록이 없는 상황이라면 더 큰 size의 index에 할당해야 한다. */
    for(int i = index; i < SEGLIST_INDEX; i++){
        for(ptr = seg_list[i]; ptr != NULL; ptr = (NEXP(ptr))) {
            if (!GET_ALLOC(HDRP(ptr)) && (asize <= GET_SIZE(HDRP(ptr)))) {
                return ptr;
            }
        }
    }
    return NULL;
}

static void place(void *bp, size_t asize)
{
    // 나머지 부분의 크기가 최소 블록 크기와 같거나 큰지 확인
    size_t csize = GET_SIZE(HDRP(bp));
    if ((csize - asize) >= (2 * DSIZE)) {
        remove_linked_list(bp);
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
        append_linked_list(bp);
    }
    else {
        remove_linked_list(bp);
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;
    size_t extendsize;
    char *bp;

    if (size == 0) {
        return NULL;
    }

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);

    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

static void append_linked_list(void *bp) {
    // 현재 블록의 next를 가장 상위 블록의 prev를 가리키도록
    int index = find_index(GET_SIZE(HDRP(bp)));
    NEXP(bp) = seg_list[index];
    // 현재 블록의 root가 있는 경우
    if (seg_list[index] != NULL){
        // 연결 리스트의 가장 상위에 있던 원소의 prev 노드에 bp 연결
        PRVP(seg_list[index]) = bp;
    }
    // 현재 블록의 root가 없는 경우, 루트가 bp를 가리키도록
    seg_list[index] = bp;
}

static void remove_linked_list(char *bp) {
    int index = find_index(GET_SIZE(HDRP(bp)));
    // bp가 루트가 아닌 경우
    if (bp != seg_list[index]) {
        // 삭제할 이전 블록의 next를 다음 블록의 prev를 가리키도록
            NEXP(PRVP(bp)) = NEXP(bp);
        // 삭제할 다음 블록의 prev를 이전 블록의 prev를 가리키도록
        if (NEXP(bp) != NULL){
            //PUT(NEXP(bp), PRVP(bp));
            PRVP(NEXP(bp)) = PRVP(bp);
        }
    }
    // bp가 루트인 경우
    else {
        // 루트에 새로운 top이 될 블록을 연결
        seg_list[index] = NEXP(bp);
        //PRVP(bp) = NULL;
    }
}

/* coalesce 함수는 explicit 방식과 차이가 없다. */
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    /* CASE 1 */
    if (prev_alloc && next_alloc) {
        append_linked_list(bp);
        return bp;
    }
    /* CASE 2 */
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        remove_linked_list(NEXT_BLKP(bp));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        append_linked_list(bp);
    }
    /* CASE 3 */
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        remove_linked_list(PREV_BLKP(bp));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        append_linked_list(bp);

    }
    /* CASE 4 */
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        remove_linked_list(PREV_BLKP(bp));
        remove_linked_list(NEXT_BLKP(bp));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        append_linked_list(bp);
    }
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    // 할당 해제할 블록의 사이즈를 구한다.
    size_t size = GET_SIZE(HDRP(bp));

    // 블록의 header와 footer도 0으로 변경한다.
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    // copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    copySize = GET_SIZE(HDRP(oldptr));
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}
