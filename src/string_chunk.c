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

fn void stringChunkListAppend(StringArena* a, StringChunkList* list, String string) {
  u64 capacity = list->count * STRING_CHUNK_PAYLOAD_SIZE;
  u64 remaining_space = capacity - list->total_size;
  u64 bytes_in_last_chunk = (list->total_size % STRING_CHUNK_PAYLOAD_SIZE);
  if (string.length < remaining_space) {
    MemoryCopy(((u8*)list->last)+sizeof(StringChunk*)+(bytes_in_last_chunk), string.bytes, string.length);
    list->total_size += string.length;
  } else {
    u64 string_offset = 0;
    u64 bytes_left = string.length;
    u64 bytes_to_copy = Min(bytes_left, remaining_space);
    if (remaining_space > 0) {
      // fill up the last chunk
      MemoryCopy(((u8*)(list->last+1))+(bytes_in_last_chunk), string.bytes, bytes_to_copy);
      bytes_left -= bytes_to_copy;
      string_offset += bytes_to_copy;
      list->total_size += bytes_to_copy;
    }

    // then figure out how many more chunks we need
    u64 needed_chunks = (bytes_left + (STRING_CHUNK_PAYLOAD_SIZE-1)) / STRING_CHUNK_PAYLOAD_SIZE;
    lockMutex(&a->mutex); {
      for (u32 i = 0; i < needed_chunks; i++) {
        StringChunk* chunk = a->first_free_str_chunk;
        if (chunk == NULL) {
          chunk = (StringChunk*)arenaAlloc(&a->a, sizeof(StringChunk)+STRING_CHUNK_PAYLOAD_SIZE);
        } else {
          a->first_free_str_chunk = a->first_free_str_chunk->next;
        }
        chunk->next = NULL; // makes sure we don't have a pointer to any other free_str_chunks
        bytes_to_copy = Min(bytes_left, STRING_CHUNK_PAYLOAD_SIZE);
        // ryan's impl used chunk+1 which seems like a bug but what do I know he had a working demo
        MemoryCopy(chunk+1, string.bytes+string_offset, bytes_to_copy);
        QueuePush(list->first, list->last, chunk);
        list->count += 1;
        list->total_size += bytes_to_copy;
        bytes_left -= bytes_to_copy;
        string_offset += bytes_to_copy;
      }
    } unlockMutex(&a->mutex);
  }
}

fn void stringChunkListDeleteLast(StringArena* a, StringChunkList* list) {
  if (list->total_size == 0) return;

  u64 capacity = list->count * STRING_CHUNK_PAYLOAD_SIZE;
  u64 remaining_space = capacity - list->total_size;
  u64 bytes_in_last_chunk = STRING_CHUNK_PAYLOAD_SIZE - remaining_space;

  bool theres_only_one_chunk = list->total_size <= STRING_CHUNK_PAYLOAD_SIZE;
  bool last_chunk_only_has_one_byte = bytes_in_last_chunk == 1;

  if (theres_only_one_chunk) {
    ptr char_to_delete = ((char*)(list->last + 1) + (list->total_size-1));
    char_to_delete[0] = '\0';
  } else if (last_chunk_only_has_one_byte) {
    // remove the last chunk when it's not the only chunk
    StringChunk* second_to_last_chunk = list->first;
    while (second_to_last_chunk->next != list->last && second_to_last_chunk->next != NULL) {
      second_to_last_chunk = second_to_last_chunk->next;
    }
    second_to_last_chunk->next = NULL;
    lockMutex(&a->mutex); {
      list->last->next = a->first_free_str_chunk;
      a->first_free_str_chunk = list->last;
    } unlockMutex(&a->mutex);
    list->last = second_to_last_chunk;
    list->count -= 1;
  } else {
    ptr char_to_delete = ((char*)(list->last + 1) + ((list->total_size-1) % STRING_CHUNK_PAYLOAD_SIZE));
    char_to_delete[0] = '\0';
  }
  list->total_size -= 1;
}

fn StringChunkList stringChunkListInit(StringArena* a) {
  StringChunkList result = {0};
  lockMutex(&a->mutex); {
    StringChunk* chunk = a->first_free_str_chunk;
    if (chunk == NULL) {
      chunk = (StringChunk*)arenaAlloc(&a->a, sizeof(StringChunk)+STRING_CHUNK_PAYLOAD_SIZE);
    } else {
      a->first_free_str_chunk = a->first_free_str_chunk->next;
    }
    chunk->next = NULL; // makes sure we don't have a pointer to any other free_str_chunks
    MemoryZero(chunk+1, STRING_CHUNK_PAYLOAD_SIZE);
    QueuePush(result.first, result.last, chunk);
    result.count += 1;
  } unlockMutex(&a->mutex);
  return result;
}

fn void stringChunkCopyToBuffer(StringChunkList* list, u8* buffer, u32 len) {
  assert(list->total_size <= len);

  StringChunk* chunk = list->first;
  for (u32 i = 0; i < list->total_size; i++) {
    if (i > 0 && i % STRING_CHUNK_PAYLOAD_SIZE == 0) {
      chunk = chunk->next;
    }
    buffer[i] = *((char*)(chunk + 1) + (i%STRING_CHUNK_PAYLOAD_SIZE));
  }
}
