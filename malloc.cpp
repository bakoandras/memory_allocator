#include <string.h>

#define FREE true
#define NOT_FREE false
#define NO_FREE_BLOCK nullptr

// Egy block definíciója a memóriában
struct Block {
    size_t size;
    bool free;
    struct Block* prev;
    struct Block* next;
    void* data;
} typedef Block;

static Block *heapStart = nullptr;           // Address of the first block in the heap
static Block *searchStart = nullptr;         // Starting point for searches
static Block *top = nullptr;                 // Current top of the heap

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>

    #define sbrk(X) fake_sbrk(X)

    void* fake_sbrk(size_t increment) {
        constexpr size_t MAX_HEAP_SIZE = 1024 * 1024;

        static char *heapStart = nullptr;
        static char *currentBreak = nullptr;

        if (heapStart == nullptr) {
            heapStart = (char*)VirtualAlloc(nullptr, MAX_HEAP_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            if (heapStart == nullptr)
                return (void *)-1;

            currentBreak = heapStart;
        }

        char *newBreak = currentBreak + increment;
        if (newBreak < heapStart || newBreak > heapStart + MAX_HEAP_SIZE)
            return (void *)-1;

        void *oldBreak = currentBreak;
        currentBreak = newBreak;

        return oldBreak;
    }
#else
    // #include <<sys/mman.h>
    // #include <unistd.h>>
#endif

// legközelebbi 8-al osztható értékre kerekíti fel. Hozzáad hetet, hogy feltolja majd az alsó 3 bitet kinullázza.
static inline size_t align(size_t n){
    return (n + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
}

// retrieves a blocks header from a payload pointer and allows us to modify metadata of a block
static Block* getBlock(void* data){
    return (Block*)((char*)data - sizeof(Block) + sizeof(data));
}

// memória felszabadítás
void _free(void* data){
    Block* block = getBlock(data);
    block->free = FREE;
}

// merge free blocks
static bool canMergre(Block* block){
    Block* next = block->next;
    return next != nullptr && next->free && next <= top;
}

static void merge(Block* block){
    Block* next = block->next;

    if(next == nullptr || next->free == NOT_FREE){
        return;
    }

    block->size += next->size + sizeof(Block);
    block->next = next->next;

    if(block->next){
        block->next->prev = block;
    }
}

// finding free blocks
// first fit: finds the first space where the block of given size can fit
static Block* firstFit(size_t size){
    for(Block* current = heapStart; current; current = current->next){
        if(current->free && current->size >= size){
            return current;
        }
    }

    return NO_FREE_BLOCK;
}

// next-fit: continues first fit search from the last allocation
static Block* nextFit(size_t size){
    static Block* searchStart = nullptr;
    if(searchStart == nullptr){
        searchStart = heapStart;
    }

    Block* current = searchStart;

    do{
        if(current->free && current->size >= size){
            searchStart = current->next;
            return current;
        }
        current = current->next ? current->next : heapStart;
    }while(current != searchStart);

    return nullptr;
}

// best fit: searches for the smallest free block that is larger than the requested size
static Block* bestFit(size_t size) {
    Block* best = nullptr;

    for(Block* current = heapStart; current; current = current->next) {
        if(current->free && current->size >= size){
            best = current;
        }
    }

    return best;
}

// choose preferred fit method, default: best fit
enum class SearchMode {
    BestFit,
    FirstFit,
    NextFit,
};

static SearchMode searchMode = SearchMode::FirstFit;

void selectAlgorithm(SearchMode algorithm) {
    searchMode = algorithm;
}

// block splitting
static bool canSplit(Block* block, size_t size){
    return block->size >= size + sizeof(Block);
}

static void split(Block* block, size_t size){
    size_t originalSize = block->size;
    block->size = size;

    Block* newBlock = (Block*)((char*)block + sizeof(Block) + size);
    newBlock->size = originalSize - size;
    newBlock->free = FREE;

    newBlock->next = block->next;
    if(newBlock->next){
        newBlock->next->prev = newBlock;
    }

    block->next = newBlock;
    newBlock->prev = block;
}

static Block *findBlock(size_t size) {
    Block *block;

    switch (searchMode) {
        case SearchMode::BestFit:
            block = bestFit(size);
            break;
        case SearchMode::FirstFit:
            block = firstFit(size);
            break;
        case SearchMode::NextFit:
            block = nextFit(size);
            break;
    }

    if (block && canSplit(block, size))
        split(block, size);

    return block;
}

// if there is no space left we ask for more from the OS
static Block* requestFromOS(size_t size){
    size = align(size + sizeof(Block));
    Block* block = (Block*)sbrk(size);

    if(block == (void*) - 1){
        return NO_FREE_BLOCK;
    }

    block->size = size - sizeof(Block);
    block->free = FREE;
    block->next = nullptr;
    block->prev = top;

    if(top){
        top->next = block;
    }

    top = block;

    if(!heapStart){
        heapStart = block;
    }

    return block;
}

// malloc function
void* _malloc(size_t size){
    if(size <= 0){
        return nullptr;
    }

    size = align(size);
    Block* block = findBlock(size);

    if(block != nullptr){
        block->free = NOT_FREE;
        return block->data;
    }

    // if no free block is found request from the OS
    block = requestFromOS(size + sizeof(Block));

    if(block != nullptr){
        block->size = size;
        block->free = NOT_FREE;
        return block->data;
    }

    return nullptr;
}

void* _calloc(size_t n, size_t size){
    void* ptr = _malloc(n * size);
    if(ptr){
        memset(ptr, 0, n * size);
    }

    return ptr;
}

void* _realloc(void* data, size_t newSize){
    if(data == nullptr){
        return _malloc(newSize);
    }

    Block* oldBlock = getBlock(data);
    if(newSize <= oldBlock->size){
        return data;
    }

    void* newBlock = _malloc(newSize);
    if(newBlock){
        memcpy(newBlock, data, oldBlock->size);
        _free(data);
    }

    return newBlock;
}
