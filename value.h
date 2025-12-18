#ifndef clox_common_h
#define clox_common_h

#include "common.h"

typedef double Value;

//Vector to store the double values
typedef struct{
    int capacity;
    int count;
    Value* values;
}ValueArray;

void initValueArray(ValueArray* array);
void writeValueArray(ValueArray* array, Value value);
void freeValueArray(ValueArray* array);
void printValue(Value value);

#endif