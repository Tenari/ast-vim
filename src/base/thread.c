#include "all.h"

Thread spawnThread(void * (*threadFn)(void *), void* thread_arg) {
  pthread_t thread;
  pthread_create(&thread, NULL, threadFn, thread_arg);
  Thread result = { thread };
  return result;
}

Mutex newMutex() {
  Mutex result = { PTHREAD_MUTEX_INITIALIZER };
  return result;
}

Cond newCond() {
  Cond result = { 0 };
  pthread_cond_init(&result.cond, NULL);
  return result;
}

void lockMutex(Mutex* m) {
  pthread_mutex_lock(&m->mutex);
}

void unlockMutex(Mutex* m) {
  pthread_mutex_unlock(&m->mutex);
}

void signalCond(Cond* cond) {
  pthread_cond_signal(&cond->cond);
}

void waitForCondSignal(Cond* cond, Mutex* mutex) {
  pthread_cond_wait(&cond->cond, &mutex->mutex);
}

