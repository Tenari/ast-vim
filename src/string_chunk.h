#ifndef STRING_CHUNK_H
#define STRING_CHUNK_H

#include "base/all.h"

#define STRING_CHUNK_PAYLOAD_SIZE (64 - sizeof(StringChunk*))

typedef struct StringChunk {
  struct StringChunk *next; // essentially a header, followed by a fixed maximum str bytes
} StringChunk;

typedef struct StringChunkList {
  StringChunk* first;
  StringChunk* last;
  u64 count;
  u64 total_size;
} StringChunkList;

typedef struct StringArena {
  Arena a;
  StringChunk* first_free_str_chunk;
  Mutex mutex;
} StringArena;

fn StringChunkList allocStringChunkList(StringArena* a, String string);
fn void releaseStringChunkList(StringArena* a, StringChunkList* list);
fn String stringChunkToString(Arena* a, StringChunkList list);
fn void stringChunkListAppend(StringArena* a, StringChunkList* list, String string);
fn void stringChunkListDeleteLast(StringArena* a, StringChunkList* list);
fn StringChunkList stringChunkListInit(StringArena* a);
fn void stringChunkCopyToBuffer(StringChunkList* list, u8* buffer, u32 len);

#endif //STRING_CHUNK_H
