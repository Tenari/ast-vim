#include <sys/mman.h>
#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include "all.h"

global pthread_barrier_t linux_thread_barrier;

fn Barrier osBarrierAlloc(u64 count) {
  pthread_barrier_init(&linux_thread_barrier, NULL, count);
  Barrier result = {(u64)&linux_thread_barrier};
  return result;
}

fn void osBarrierRelease(Barrier barrier) {
  pthread_barrier_t* addr = (pthread_barrier_t*)barrier.a[0];
  pthread_barrier_destroy(addr);
}

fn void osBarrierWait(Barrier barrier) {
  pthread_barrier_t* addr = (pthread_barrier_t*)barrier.a[0];
  pthread_barrier_wait(addr);
}

// Time
fn u64 osTimeMicrosecondsNow() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((u64)ts.tv_sec * 1000000) + ((u64)ts.tv_nsec / 1000000);
}

#define MICROSECONDS_PER_SECOND 1000000
#define NANOSECONDS_PER_MICROSECOND 1000
fn void osSleepMicroseconds(u32 t) {
  struct timespec ts = { t / MICROSECONDS_PER_SECOND, (t % MICROSECONDS_PER_SECOND)*NANOSECONDS_PER_MICROSECOND };
  nanosleep(&ts, NULL);
}

// Files
fn bool osFileExists(String filename) {
  bool result = access((str)filename.bytes, F_OK) == 0;
  return result;
}

fn String osFileRead(Arena* arena, ptr filepath) {
  struct stat st;
  stat(filepath, &st);
  String result = { st.st_size, st.st_size, 0 };
  result.bytes = arenaAlloc(arena, st.st_size);

  size_t handle = open(filepath, O_RDWR, S_IRUSR | S_IRGRP | S_IROTH);
  read(handle, result.bytes, st.st_size);
  close(handle);

  return result;
}

fn bool osFileCreate(String filename) {
  /*
  M_Scratch scratch = scratch_get();
  string nt = str_copy(&scratch.arena, filename);
  bool result = true;
  size_t handle = open((const char*) nt.str, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
  if (handle == -1) {
      result = false;
  }
  scratch_return(&scratch);
  close(handle);
  return true;
  */
  bool result = true;
  size_t handle = open((str)filename.bytes, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
  if (handle == -1) {
    result = false;
  }
  if (close(handle) == -1) {
    result = false;
  }
  return result;
}

fn bool osFileCreateWrite(String filename, String data) {
  /*
  M_Scratch scratch = scratch_get();
  string nt = str_copy(&scratch.arena, filename);
  b32 result = true;
  size_t handle =
    open((const char*) nt.str, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
  if (handle == -1) result = false;
  write(handle, data.str, data.size);
  close(handle);
  scratch_return(&scratch);
  return result;
  */
  bool result = true;
  size_t handle = open(
    (str)filename.bytes,
    O_RDWR | O_CREAT | O_TRUNC,
    S_IRUSR | S_IRGRP | S_IROTH
  );
  if (handle == -1) result = false;
  write(handle, data.bytes, data.length);
  close(handle);
  return result;
}

fn bool osFileWrite(String filename, String data) {
  /*
    M_Scratch scratch = scratch_get();
    string nt = str_copy(&scratch.arena, filename);
    b32 result = true;
    size_t handle =
        open((const char*) nt.str, O_RDWR | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
    if (handle == -1) result = false;
    write(handle, data.str, data.size);
    close(handle);
  */
  bool result = true;
  size_t handle = open((str) filename.bytes, O_RDWR | O_TRUNC, S_IRUSR | S_IRGRP | S_IROTH);
  if (handle == -1) result = false;
  write(handle, data.bytes, data.length);
  close(handle);
  return result;
}


// Misc
fn void osDebugPrint(bool debug_mode, const char * format, ... ) {
  if (debug_mode) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}
