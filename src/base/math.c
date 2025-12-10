#include "all.h"

fn Range1u64 range1u64Create(u64 min, u64 max) {
  Range1u64 result = {
    .min = min,
    .max = max
  };
  if (result.min > result.max) {
    result.max = min;
    result.min = max;
  }
  return result;
}

fn Range1u64 mRangeFromNIdxMCount(u64 n_idx, u64 n_count, u64 m_count) {
  u64 main_idxes_per_lane = m_count / n_count;
  u64 leftover_idxes_count = m_count - main_idxes_per_lane * n_count;
  u64 leftover_idxes_before_this_lane_count = Min(n_idx, leftover_idxes_count);
  u64 lane_base_idx = n_idx*main_idxes_per_lane + leftover_idxes_before_this_lane_count;
  u64 lane_base_idx__clamped = Min(lane_base_idx, m_count);
  u64 lane_opl_idx = lane_base_idx__clamped + main_idxes_per_lane + ((n_idx < leftover_idxes_count) ? 1 : 0);
  u64 lane_opl_idx__clamped = Min(lane_opl_idx, m_count);
  Range1u64 result = range1u64Create(lane_base_idx__clamped, lane_opl_idx__clamped);
  return result;
}
