#ifndef asharp_object_h
#define asharp_object_h

#include "common.h"
#include "chunk.h"

// 1. MACROS
#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
#define IS_STRING(value)  isObjType(value, OBJ_STRING)
#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

// 2. FORWARD DECLARATIONS (These match value.h)
typedef struct Obj Obj;
typedef struct ObjFunction ObjFunction;
typedef Value (*NativeFn)(int argCount, Value* args);

// 3. ENUM
typedef enum {
  OBJ_CLOSURE,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_STRING,
  OBJ_UPVALUE
} ObjType;

// 4. STRUCT DEFINITIONS
// Note: We use 'struct Name' here to match the typedefs in value.h

struct Obj {
  ObjType type;
  struct Obj* next;
};

// FIX 1: Added 'struct ObjString' tag
struct ObjString {
  Obj obj;
  int length;
  char* chars;
  uint32_t hash;
};

// FIX 2: Added 'struct ObjFunction' tag
struct ObjFunction {
  Obj obj;
  int arity;
  int upvalueCount;
  Chunk chunk;
  ObjString* name;
};

// FIX 3: Added 'struct ObjNative' tag (Optional, but good for consistency)
typedef struct {
  Obj obj;
  NativeFn function;
} ObjNative;

// FIX 4: Added 'struct ObjClosure' tag (Optional, but likely needed later)
typedef struct {
  Obj obj;
  ObjFunction* function;
} ObjClosure;


// 5. FUNCTIONS & MACROS
static inline bool isObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjString* copyString(const char* chars, int length);
void printObject(Value value);

ObjString* takeString(char* chars, int length);

ObjFunction* newFunction();
#define AS_FUNCTION(value)   ((ObjFunction*)AS_OBJ(value))
#define IS_FUNCTION(value)   isObjType(value, OBJ_FUNCTION)

ObjNative* newNative(NativeFn function);
#define AS_NATIVE(value)     (((ObjNative*)AS_OBJ(value))->function)
#define IS_NATIVE(value)     isObjType(value, OBJ_NATIVE)

ObjClosure* newClosure(ObjFunction* function);
#define AS_CLOSURE(value)    ((ObjClosure*)AS_OBJ(value))
#define IS_CLOSURE(value)    isObjType(value, OBJ_CLOSURE)

#endif