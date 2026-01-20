#include <stdlib.h>
#include "memory.h"
#include "vm.h"
#include "object.h"

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

static void freeObject(Obj* object) {
  //Cleaning funtion and string objects
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      // Free the character array
      FREE_ARRAY(char, string->chars, string->length + 1);
      // Free the struct itself
      FREE(ObjString, object);
      break;
    }
    
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
  }
}

void freeObjects() {
  Obj* object = vm.objects;
  while (object != NULL) {
    Obj* next = object->next;
    freeObject(object);
    object = next;
  }
}