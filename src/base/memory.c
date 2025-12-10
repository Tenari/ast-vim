#include "all.h"

fn u64 alignForward(u64 pointer, u64 align) {
	u64 p, modulo;
	assert(isPowerOfTwo(align));
	p = pointer;
	// Same as (p % a) but faster as 'a' is a power of two
	modulo = p & (align-1);
	if (modulo != 0) {
		// If 'p' address is not aligned, push the address to the
		// next value which is aligned
		p += align - modulo;
	}
	return p;
}

fn void* arenaAlloc(Arena* arena, u64 size) {
  void* memory = 0;
  size = alignForward(size, DEFAULT_ALIGNMENT);
  if (arena->alloc_position + size > arena->commit_position) {
    if (!arena->static_size) {
      u64 commit_size = size;

      commit_size += ARENA_COMMIT_SIZE - 1;
      commit_size -= commit_size % ARENA_COMMIT_SIZE;

      if (arena->commit_position < arena->max) {
        osMemoryCommit(arena->memory + arena->commit_position, commit_size);
        arena->commit_position += commit_size;
      } else {
        assert(0 && "Arena is out of memory");
      }
    } else {
      assert(0 && "Static-Size Arena is out of memory");
    }
  }

  memory = arena->memory + arena->alloc_position;
  arena->alloc_position += size;
  return memory;
}

fn void* arenaAllocArraySized(Arena* arena, u64 elem_size, u64 count) {
    return arenaAlloc(arena, elem_size * count);
}

fn void arenaDealloc(Arena* arena, u64 size) {
  if (size > arena->alloc_position) size = arena->alloc_position;
  arena->alloc_position -= size;
}

fn void arenaInit(Arena* arena) {
  MemoryZeroStruct(arena, Arena);
  arena->max = ARENA_MAX;
  arena->memory = osMemoryReserve(arena->max);
  arena->alloc_position = 0;
  arena->commit_position = 0;
  arena->static_size = false;
}

// WARNING: segfault problems with this approach
fn void arenaInitStatic(Arena* arena, u64 max) {
  MemoryZeroStruct(arena, Arena);
  arena->max = max;
  arena->memory = osMemoryReserve(arena->max);
  osMemoryCommit(arena->memory, max + (max % ARENA_COMMIT_SIZE));
  arena->alloc_position = 0;
  arena->commit_position = 0;
  arena->static_size = true;
}


fn void arenaClear(Arena* a) {
  a->alloc_position = 0;
}

fn void arenaFree(Arena* a) {
  osMemoryRelease(a->memory, a->max);
}

ScratchMem scratchGet(void) {
	ThreadContext* ctx = (ThreadContext*)osThreadContextGet();
	return tctxScratchGet(ctx);
}

void scratchReset(ScratchMem* scratch) {
	ThreadContext* ctx = (ThreadContext*)osThreadContextGet();
	tctxScratchReset(ctx, scratch);
}

void scratchReturn(ScratchMem* scratch) {
	ThreadContext* ctx = (ThreadContext*)osThreadContextGet();
	tctxScratchReturn(ctx, scratch);
}
