#include "all.h"

global pthread_key_t linux_thread_context_key;
// ThreadContext
void osInit() {
  pthread_key_create(&linux_thread_context_key, NULL);
}

void* osThreadContextGet() {
  return pthread_getspecific(linux_thread_context_key);
}

void osThreadContextSet(void* ctx) {
  pthread_setspecific(linux_thread_context_key, ctx);
}

bool osThreadJoin(Thread handle, u64 endt_us) {
  /*
  if(MemoryIsZeroStruct(&handle)) { return 0; }
  OS_LNX_Entity *entity = (OS_LNX_Entity *)handle.u64[0];
  int join_result = pthread_join(entity->thread.handle, 0);
  B32 result = (join_result == 0);
  os_lnx_entity_release(entity);
  return result;
  */
  i32 join_result = pthread_join(handle.thread, NULL);
  bool result = join_result == 0;
  return result;
}

// Memory
fn void* osMemoryReserve(u64 size) {
  void* result = mmap(((void*)0), size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if(result == MAP_FAILED) {
    result = 0;
  }
  return result;
}

fn void osMemoryCommit(void* memory, u64 size) {
  i32 result = mprotect(memory, size, PROT_READ | PROT_WRITE);
  assert(result == 0 && "osMemoryCommit() failed");
}

fn void osMemoryDecommit(void* memory, u64 size) {
    mprotect(memory, size, PROT_NONE);
}

fn void osMemoryRelease(void* memory, u64 size) {
    munmap(memory, size);
}

// TUI
TermIOs osStartTUI(bool blocking) {
  // set up the TUI incantations
  printf("\033[?1049h"); // go to alternate buffer
  TermIOs terminal_attributes, old_terminal_attributes;
  tcgetattr(STDOUT_FILENO, &terminal_attributes);
  old_terminal_attributes = terminal_attributes;
  terminal_attributes.c_lflag &= ~(ICANON | ECHO); // dont echo keypresses, dont wait for carriage return
  tcsetattr(STDOUT_FILENO, TCSANOW, &terminal_attributes);
  if (!blocking) {
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK); // non-blocking input mode
  }
  fflush(stdout);
  return old_terminal_attributes;
}

fn void osEndTUI(TermIOs old_terminal_attributes) {
  tcsetattr(STDOUT_FILENO, TCSANOW, &old_terminal_attributes);

  // cleanup terminal TUI incantations
  printf("\033[?1049l");
	fflush(stdout);
}

fn Dim2 osGetTerminalDimensions() {
  Dim2 result = {0};
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
      //perror("ioctl TIOCGWINSZ failed");
      //exit(1);
      return result;
  }
  result.width = ws.ws_col;
  result.height = ws.ws_row;
  return result;
}

void osBlitToTerminal(ptr writeable_output_ansi_string, i64 count) {
  int flags = fcntl(STDOUT_FILENO, F_GETFL);
  fcntl(STDOUT_FILENO, F_SETFL, flags & ~O_NONBLOCK);

  u64 total = 0;
  while (total < count) {
    i64 written_bytes = write(STDOUT_FILENO, writeable_output_ansi_string + total, count - total);
    /* TODO handle this error
    if (written_bytes < 0) {
        if (errno == EINTR) continue;  // Interrupted, retry
        return -1;  // Real error
    }
    */
    total += written_bytes;
  }
  fcntl(STDOUT_FILENO, F_SETFL, flags);
}

bool osInitNetwork() { return true; }

void osReadConsoleInput(u8* buffer, u32 len) {
	MemoryZero(buffer, len); // reset the input so it's not contaminated by last keystroke
	read(STDIN_FILENO, buffer, len);
}

#define LOCALHOST_127 16777343
i32 osLanIPAddress() { // returns as HOST byte-order
  i32 result = 0;
  struct ifaddrs *ifaddr, *ifa;
  if (getifaddrs(&ifaddr) != -1) {
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == NULL) continue;
      
      int family = ifa->ifa_addr->sa_family;
      
      if (family == AF_INET) {
          struct sockaddr_in addr = *(struct sockaddr_in*)ifa->ifa_addr;
          if (addr.sin_addr.s_addr != LOCALHOST_127) {
            freeifaddrs(ifaddr);
            //printf("%s %d %d", inet_ntoa(addr.sin_addr), addr.sin_addr.s_addr, ntohl(addr.sin_addr.s_addr));
            return ntohl(addr.sin_addr.s_addr);
          }
          /*
          char host[NI_MAXHOST];
          int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                             host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
          
          if (s == 0) {
              printf("Interface: %s\tAddress: %s\n", ifa->ifa_name, host);
          }
          */
      }
    }
    
    freeifaddrs(ifaddr);
  }
  return result;
}
