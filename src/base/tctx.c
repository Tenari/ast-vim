#include "all.h"

void tctxInit(ThreadContext* ctx) {
	arenaInit(&ctx->arena);
	osThreadContextSet(ctx);
}

void tctxFree(ThreadContext* ctx) {
	arenaFree(&ctx->arena);
	osThreadContextSet(ctx);
}

fn ThreadContext *tctxSelected(void) {
  return (ThreadContext*)osThreadContextGet();
}

ScratchMem tctxScratchGet(ThreadContext* ctx) {
	if (!ctx->free_list) {
		ScratchMem scratch = {0};

		scratch.arena.memory = arenaAlloc(&ctx->arena, M_SCRATCH_SIZE);
		scratch.arena.max = M_SCRATCH_SIZE;
		scratch.arena.alloc_position = 0;
		scratch.arena.commit_position = M_SCRATCH_SIZE;
		scratch.arena.static_size = true;

		ctx->max_created++;
		return scratch;
	} else {
		ScratchMem scratch = {0};

		scratch.arena.memory = (u8*) ctx->free_list;
		scratch.arena.max = M_SCRATCH_SIZE;
		scratch.arena.alloc_position = 0;
		scratch.arena.commit_position = M_SCRATCH_SIZE;
		scratch.arena.static_size = true;

		ctx->free_list = ctx->free_list->next;
		return scratch;
	}
}

void tctxScratchReset(ThreadContext* ctx, ScratchMem* scratch) {
	scratch->arena.alloc_position = 0;
}

void tctxScratchReturn(ThreadContext* ctx, ScratchMem* scratch) {
	ScratchFreeListNode* prev_head = ctx->free_list;
	ctx->free_list = (ScratchFreeListNode*) scratch->arena.memory;
	ctx->free_list->next = prev_head;
}

fn LaneCtx tctxSetLaneCtx(LaneCtx lane_ctx) {
  ThreadContext *tctx = tctxSelected();
  LaneCtx restore = tctx->lane_ctx;
  tctx->lane_ctx = lane_ctx;
  return restore;
}

fn void tctxLaneBarrierWait(void *broadcast_ptr, u64 broadcast_size, u64 broadcast_src_lane_idx) {
  ThreadContext *tctx = tctxSelected();

  // broadcasting -> copy to broadcast memory on source lane
  u64 broadcast_size_clamped = Min(broadcast_size, sizeof(tctx->lane_ctx.broadcast_memory[0]));
  if(broadcast_ptr != 0 && LaneIdx() == broadcast_src_lane_idx) {
    MemoryCopy(tctx->lane_ctx.broadcast_memory, broadcast_ptr, broadcast_size_clamped);
  }

  // all cases: barrier
  osBarrierWait(tctx->lane_ctx.barrier);

  // broadcasting -> copy from broadcast memory on destination lanes
  if(broadcast_ptr != 0 && LaneIdx() != broadcast_src_lane_idx)
  {
    MemoryCopy(broadcast_ptr, tctx->lane_ctx.broadcast_memory, broadcast_size_clamped);
  }

  // broadcasting -> barrier on all lanes
  if(broadcast_ptr != 0)
  {
    osBarrierWait(tctx->lane_ctx.barrier);
  }
}
