#include "string_chunk.h"

fn StringChunkList allocStringChunkList(StringArena* a, String string) {
  StringChunkList result = {0};
  u64 needed_chunks = (string.length + (STRING_CHUNK_PAYLOAD_SIZE-1)) / STRING_CHUNK_PAYLOAD_SIZE;
  u64 bytes_left = string.length;
  u64 string_offset = 0;
  lockMutex(&a->mutex); {
    for (u32 i = 0; i < needed_chunks; i++) {
      StringChunk* chunk = a->first_free_str_chunk;
      if (chunk == NULL) {
        chunk = (StringChunk*)arenaAlloc(&a->a, sizeof(StringChunk)+STRING_CHUNK_PAYLOAD_SIZE);
      } else {
        a->first_free_str_chunk = a->first_free_str_chunk->next;
      }
      chunk->next = NULL; // makes sure we don't have a pointer to any other free_str_chunks
      u64 bytes_to_copy = Min(bytes_left, STRING_CHUNK_PAYLOAD_SIZE);
      // ryan's impl used chunk+1 which seems like a bug but what do I know he had a working demo
      MemoryCopy(chunk+1, string.bytes+string_offset, bytes_to_copy);
      QueuePush(result.first, result.last, chunk);
      result.count += 1;
      result.total_size += bytes_to_copy;
      bytes_left -= bytes_to_copy;
      string_offset += bytes_to_copy;
    }
  } unlockMutex(&a->mutex);
  return result;
}

fn void releaseStringChunkList(StringArena* a, StringChunkList* list) {
  StringChunk* chunk = list->first;
  lockMutex(&a->mutex); {
    for (StringChunk* next = NULL; chunk != NULL; chunk = next) {
      next = chunk->next;
      chunk->next = a->first_free_str_chunk;
      a->first_free_str_chunk = chunk;
    }
  } unlockMutex(&a->mutex);
  MemoryZeroStruct(list, StringChunkList);
}

fn String stringChunkToString(Arena* a, StringChunkList list) {
  String result = {
    .length = list.total_size,
    .capacity = list.total_size + 1,
    .bytes = arenaAllocArray(a, u8, list.total_size+1),
  };
  // copy the string bytes out of the StringChunkList into the correctly-sized String
  StringChunk* chunk = list.first;
  for (u32 i = 0; i < list.total_size; i++) {
    if (i > 0 && i % STRING_CHUNK_PAYLOAD_SIZE == 0) {
      chunk = chunk->next;
    }
    result.bytes[i] = *((char*)(chunk + 1) + (i%STRING_CHUNK_PAYLOAD_SIZE));
  }
  return result;
}
