#include "all.h"

fn u64 writeU64ToBufferLE(u8* buffer, u64 value) {
  buffer[0] = (u8)(value & 0xFF);
  buffer[1] = (u8)((value >> 8) & 0xFF);
  buffer[2] = (u8)((value >> 16) & 0xFF);
  buffer[3] = (u8)((value >> 24) & 0xFF);
  buffer[4] = (u8)((value >> 32) & 0xFF);
  buffer[5] = (u8)((value >> 40) & 0xFF);
  buffer[6] = (u8)((value >> 48) & 0xFF);
  buffer[7] = (u8)((value >> 56) & 0xFF);
  return 8;// number of bytes written
}

fn u64 writeU32ToBufferLE(u8* buffer, u32 value) {
  buffer[0] = (u8)(value & 0xFF);
  buffer[1] = (u8)((value >> 8) & 0xFF);
  buffer[2] = (u8)((value >> 16) & 0xFF);
  buffer[3] = (u8)((value >> 24) & 0xFF);
  return 4;// number of bytes written
}

fn u64 writeI32ToBufferLE(u8* buffer, i32 value) {
  buffer[0] = (u8)(value & 0xFF);
  buffer[1] = (u8)((value >> 8) & 0xFF);
  buffer[2] = (u8)((value >> 16) & 0xFF);
  buffer[3] = (u8)((value >> 24) & 0xFF);
  return 4;// number of bytes written
}

fn u64 writeU16ToBufferLE(u8* buffer, u16 value) {
  buffer[0] = (u8)(value & 0xFF);
  buffer[1] = (u8)((value >> 8) & 0xFF);
  return 2;// number of bytes written
}

fn u64 readU64FromBufferLE(u8 *buffer) {
    return (u64)buffer[0] |
           ((u64)buffer[1] << 8) |
           ((u64)buffer[2] << 16) |
           ((u64)buffer[3] << 24) |
           ((u64)buffer[4] << 32) |
           ((u64)buffer[5] << 40) |
           ((u64)buffer[6] << 48) |
           ((u64)buffer[7] << 56);
}

fn u32 readU32FromBufferLE(u8 *buffer) {
    return (u32)buffer[0] |
           ((u32)buffer[1] << 8) |
           ((u32)buffer[2] << 16) |
           ((u32)buffer[3] << 24);
}

fn i32 readI32FromBufferLE(u8 *buffer) {
    return (i32)buffer[0] |
           ((i32)buffer[1] << 8) |
           ((i32)buffer[2] << 16) |
           ((i32)buffer[3] << 24);
}

fn u16 readU16FromBufferLE(u8 *buffer) {
    return (u16)buffer[0] |
           ((u16)buffer[1] << 8);
}

