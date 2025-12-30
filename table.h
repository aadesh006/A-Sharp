#ifndef clox_table_h
#define clox_table_h

#include "common.h"
#include "value.h"

//A key-value pair
typedef struct{
    ObjString* key;
    Value value;
}Entry;

//The Hash Table
typedef struct{
    int count;
    int capacity;
    Entry* entries;
}Table;

void initTable(Table* table);
void freeTable(Table* table);

//Core Operations
bool tableGet(Table* table, ObjString* key, Value* value);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tabelAddAll(Table* from, Table* to);
ObjString* tableFindString(Table* table, const char* chars, int length, uint32_t hash);

#endif