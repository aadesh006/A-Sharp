#ifndef clox_value_h
#define clox_value_h

#include "common.h"

typedef enum{
  VAL_BOOL,
  VAL_NIL,
  VAL_NUMBER,
}ValueType;

//the container
typedef struct{
  ValueType type;
  union{
    bool boolean;
    double number;
  }as; // the data (union shares memory for these fields)
}Value;

//Macros(Tools tp know labels)
//Check the label
#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_NIL(value) ((value).type ==VAL_NIL)
#define IS_NUMBER(value) ((value).type ==VAL_NUMBER)

//Get the data
#define AS_BOOL(value) ((value).as.boolean)
#define AS_NUMBER(value) ((value).as.number)

//Pack data
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL ((Value){VAL_NIL,{.number=0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})

//typedef double Value; //earlier version only supports double

typedef struct {
  int capacity;
  int count;
  Value* values;
} ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif