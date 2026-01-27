#ifndef asharp_object_h
#define asharp_object_h

#include "common.h"
#include "value.h"
#include "chunk.h"

#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
#define IS_STRING(value)  isObjType(value, OBJ_STRING)
#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

//typedef struct sChunk Chunk; //old code
typedef Value (*NativeFn)(int argCount, Value* args);

typedef enum {
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
} ObjType;

struct Obj {
  ObjType type;
  struct Obj* next;
};

typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

typedef struct {
  Obj obj;
  int arity;
  int upvalueCount; //Remaining to implement
  Chunk chunk;
  ObjString* name;
} ObjFunction;

ObjFunction* newFunction();
#define AS_FUNCTION(value)   ((ObjFunction*)AS_OBJ(value))
#define IS_FUNCTION(value)   isObjType(value, OBJ_FUNCTION)
ObjNative* newNative(NativeFn function);

#define AS_NATIVE(value) \
    (((ObjNative*)AS_OBJ(value))->function)

#define IS_NATIVE(value)  isObjType(value, OBJ_NATIVE)

struct ObjString {
  Obj obj;
  int length;
  char* chars;
  uint32_t hash;
};

static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

//Critical line
ObjString* copyString(const char* chars, int length);
void printObject(Value value);

#endif