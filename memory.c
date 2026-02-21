#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "object.h"
#include "compiler.h"

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

void freeObject(Obj* object) {
  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
      FREE(ObjClosure, object);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      freeChunk(&function->chunk);
      FREE(ObjFunction, object);
      break;
    }
    case OBJ_NATIVE: {
      FREE(ObjNative, object);
      break;
    }
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      FREE_ARRAY(char, string->chars, string->length + 1);
      FREE(ObjString, object);
      break;
    }
    case OBJ_UPVALUE: {
      FREE(ObjUpvalue, object);
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

void markObject(Obj* object) {
  if (object == NULL) return;
  if (object->isMarked) return;

#ifdef DEBUG_LOG_GC
  printf("%p mark ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  object->isMarked = true;

  // Add it to the Gray Stack
  if (vm.grayCapacity < vm.grayCount + 1) {
    vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
    //use system realloc here so GC doesn't trigger itself!
    vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
    
    if (vm.grayStack == NULL) exit(1); // Crash if we literally run out of RAM
  }

  vm.grayStack[vm.grayCount++] = object;
}

void markValue(Value value) {
  // Only heap-allocated objects need garbage collection!
  // Numbers, Booleans, and Nil live directly on the stack, so ignore them.
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

static void markRoots() {
  for (Value* slot = vm.stack; slot < vm.stackTop; slot++) {
    markValue(*slot);
  }

  for (int i = 0; i < vm.frameCount; i++) {
    markObject((Obj*)vm.frames[i].closure);
  }

  for (ObjUpvalue* upvalue = vm.openUpvalues; upvalue != NULL; upvalue = upvalue->next) {
    markObject((Obj*)upvalue);
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
#endif

  // Phase 1: Mark the roots
  markRoots();

  // Phase 2: Trace References
  // Phase 3: Sweep

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
#endif
}