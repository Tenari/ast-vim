#include "all.h"

fn void mainThreadBaseEntryPoint(i32 argc, char **argv) {
  Thread* async_threads = 0;
  u64 lane_broadcast_val = 0;
  {
    u32 num_main_threads = 1;
    u32 num_async_threads = 12;//TODO: os_get_system_info()->logical_processor_count;
    u32 num_main_threads_clamped = Min(num_async_threads, num_main_threads);
    num_async_threads -= num_main_threads_clamped;
    String num_async_threads_string = cmd_line_string(&cmdline, str8_lit("async_thread_count"));
    if(num_async_threads_string.size != 0)
    {
      try_u64_from_str8_c_rules(num_async_threads_string, &num_async_threads);
    }
    num_async_threads = Max(1, num_async_threads);
    Barrier barrier = barrier_alloc(num_async_threads);
    LaneCtx *lane_ctxs = push_array(scratch.arena, LaneCtx, num_async_threads);
    async_threads_count = num_async_threads;
    async_threads = push_array(scratch.arena, Thread, async_threads_count);
    for EachIndex(idx, num_async_threads)
    {
      lane_ctxs[idx].lane_idx = idx;
      lane_ctxs[idx].lane_count = async_threads_count;
      lane_ctxs[idx].barrier = barrier;
      lane_ctxs[idx].broadcast_memory = &lane_broadcast_val;
      async_threads[idx] = thread_launch(async_thread_entry_point, &lane_ctxs[idx]);
    }
  }

  //- rjf: call into entry point
  entry_point(&cmdline);

  //- rjf: join async threads
  ins_atomic_u32_inc_eval(&global_async_exit);
  cond_var_broadcast(async_tick_start_cond_var);
  for EachIndex(idx, async_threads_count)
  {
    thread_join(async_threads[idx], max_U64);
  }
}

fn void asyncThreadEntryPoint(void *params) {
}

