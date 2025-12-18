#include <stdlib.h>
#include "memory.h"

void* reallocate(void* pointer, size_t oldSize, size_t newSize){
//CASE 1: Delete the Memory (newSize is 0)
    if(newSize==0){
        free(pointer);
        return NULL;
    }

//Case 2: Grow or Shrink the memory
void* result = realloc(pointer, newSize);

//Check for overflow in RAM
if(result == NULL) exit(1);

return result;
}

