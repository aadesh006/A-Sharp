#include <stdlib.h>
#include <stdio.h>
#include "memory.h"
#include "vm.h"
#include "object.h"
#include "compiler.h"
#include "table.h"

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

static void markArray(ValueArray* array) {
  for (int i = 0; i < array->count; i++) {
    markValue(array->values[i]);
  }
}

static void blackenObject(Obj* object) {
#ifdef DEBUG_LOG_GC
  printf("%p blacken ", (void*)object);
  printValue(OBJ_VAL(object));
  printf("\n");
#endif

  switch (object->type) {
    case OBJ_CLOSURE: {
      ObjClosure* closure = (ObjClosure*)object;
      markObject((Obj*)closure->function); // Mark the wrapped function
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]); // Mark the upvalues
      }
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markArray(&function->chunk.constants); // Mark everything in the constant pool!
      break;
    }
    case OBJ_UPVALUE:
      markValue(((ObjUpvalue*)object)->closed);
      break;
    case OBJ_NATIVE:
    case OBJ_STRING:
      break; // Strings and Native functions don't contain other objects.
  }
}

static void traceReferences() {
  while (vm.grayCount > 0) {
    Obj* object = vm.grayStack[--vm.grayCount];
    blackenObject(object);
  }
}

static void sweep() {
  Obj* previous = NULL;
  Obj* object = vm.objects;

  while (object != NULL) {
    if (object->isMarked) {

      object->isMarked = false;
      previous = object;
      object = object->next;
    } 
    
    else {
      Obj* unreached = object;
      object = object->next;
      
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm.objects = object;
      }

      freeObject(unreached);
    }
  }
}

void collectGarbage() {
#ifdef DEBUG_LOG_GC
  printf("-- gc begin\n");
  size_t before = vm.bytesAllocated;
#endif

  // Phase 1: Paint the roots
  markRoots();
  
  // Phase 2: Paint the children
  traceReferences();
  
  // Phase 2.5: Clean the string table of unpainted strings
  tableRemoveWhite(&vm.strings);
  
  // Phase 3: Destroy the unpainted objects
  sweep();

#ifdef DEBUG_LOG_GC
  printf("-- gc end\n");
  printf("   collected %zu bytes (from %zu to %zu) next at %zu\n",
         before - vm.bytesAllocated, before, vm.bytesAllocated,
         vm.nextGC);
#endif
}