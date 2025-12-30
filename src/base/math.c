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

void u32Swap(u32* a, u32* b) {
  int t = *a;
  *a = *b;
  *b = t;
}

u32 u32ArrPartition(u32 arr[], u32 low, u32 high) {
  u32 pivot = arr[high];
  u32 i = low - 1;

  for (u32 j = low; j < high; j++) {
    if (arr[j] < pivot) {
      i++;
      u32Swap(&arr[i], &arr[j]);
    }
  }
  u32Swap(&arr[i + 1], &arr[high]);
  return i + 1;
}

fn void u32Quicksort(u32 arr[], u32 low, u32 high) {
  if (low < high) {
    u32 pi = u32ArrPartition(arr, low, high);
    if (pi != 0) {
      u32Quicksort(arr, low, pi - 1);
    }
    u32Quicksort(arr, pi + 1, high);
  }
}

fn void u32ReverseArray(u32 arr[], u32 size) {
  u32 start = 0;
  u32 end = size - 1;
  while (start < end) {
    u32 temp = arr[start];
    arr[start] = arr[end];
    arr[end] = temp;
    start++;
    end--;
  }
}
