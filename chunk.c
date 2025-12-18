#include <stdlib.h>
#include "chunk.c"
#include "memory.c"

//Initialize the chunk to be completely empty
void initChunk(Chunk* chunk){
    chunk->count =0;
    chunk->capacity =0;
    chunk->code=NULL;
}

//Add a byte to the list
void writeChunk(Chunk*chunk, uint8_t byte){
    //check if we are full
    //if the current count +1 is bigger then capcity
    if(chunk->capacity < chunk->count+1){
        int oldCapacity = chunk->capacity;

        //Calculate the new capacity(Double it)
        chunk->capacity = GROW_CAPACITY(oldCapacity);

        //Pass the opld memory block
        chunk->code = GROW_ARRAY(uint*_t, chunk->code, oldCapacity, chunk->capacity);
    }

    //Write the byte into the slot
    chunk->code[chunk->code] = byte;
    chunk->count++;
}

void freeChunk(Chunk* chunk){
    FREE_ARRAY(uint8_t, chubk->code, chunk->capacity);
    initChunk(chunk);
}