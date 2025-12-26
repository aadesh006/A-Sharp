#ifndef clox_memory_h
#define clox_memory_h

#include "common.h"

//Calculate the new size
#define GROW_CAPACITY(capacity) \
    ((capacity)<8?8:(capacity)*2)

//Resize command
#define GROW_ARRAY(type, pointer, oldCount, newCount) \
    (type*)reallocate(pointer, sizeof(type)*(oldCount),\
        sizeof(type)*(newCount))

//The Delete Command
#define FREE(type, pointer) reallocate(pointer, sizeof(type), 0)
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

void freeObjects();

#define ALLOCATE(type, count) \
    (type*)reallocate(NULL, 0, sizeof(type) * (count))

//Function
void* reallocate(void*pointer, size_t oldSize, size_t newSize);

#endif

