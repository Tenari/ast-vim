#include "string.h"
#include "os.h"
#include <userenv.h>
#include <stdio.h>

static u64 w32_ticks_per_sec = 1;
static u32 w32_thread_context_index;

void osInit() {
	LARGE_INTEGER perf_freq = {0};
	if (QueryPerformanceFrequency(&perf_freq)) {
		w32_ticks_per_sec = ((u64)perf_freq.HighPart << 32) | perf_freq.LowPart;
	}
	timeBeginPeriod(1);

	w32_thread_context_index = TlsAlloc();
}

void* osThreadContextGet() {
	return TlsGetValue(w32_thread_context_index);
}

void osThreadContextSet(void* ctx) {
	TlsSetValue(w32_thread_context_index, ctx);
}

// Memory
fn void* osMemoryReserve(u64 size) {
  return VirtualAlloc(0, size, MEM_RESERVE, PAGE_NOACCESS);
}

fn void osMemoryCommit(void* memory, u64 size) {
  VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE);
}

fn void osMemoryDecommit(void* memory, u64 size) {
  VirtualFree(memory, size, MEM_DECOMMIT);
}

fn void osMemoryRelease(void* memory, u64 size) {
  VirtualFree(memory, 0, MEM_RELEASE);
}

// Time
fn u64 osTimeMicrosecondsNow() {
  u64 result = 0;
	LARGE_INTEGER perf_counter = {0};
	if (QueryPerformanceCounter(&perf_counter)) {
		u64 ticks = ((u64)perf_counter.HighPart << 32) | perf_counter.LowPart;
		result = ticks * 1000000 / w32_ticks_per_sec;
	}
	return result;
}

#define MICROSECONDS_PER_MILLISECOND 1000
fn void osSleepMicroseconds(u32 t) {
  Sleep(t / MICROSECONDS_PER_MILLISECOND);
}

// Files
fn bool osFileExists(String filename) {
  assert(false && "Not Implemented");
  ScratchMem scratch = scratchGet();
  StringUTF16Const filename16 = str16FromStr8(&scratch.arena, filename);
  DWORD ret = GetFileAttributesW((WCHAR*)filename16.string);
	scratchReturn(&scratch);
	return (ret != INVALID_FILE_ATTRIBUTES && !(ret & FILE_ATTRIBUTE_DIRECTORY));
}

fn String osFileRead(Arena* arena, ptr filepath) {
  assert(false && "Not Implemented");
	String result = {0};
  return result;
}

fn bool osFileCreate(String filename) {
  assert(false && "Not Implemented");
  return false;
}

fn bool osFileCreateWrite(String filename, String data) {
  assert(false && "Not Implemented");
  bool result = true;
  return result;
}

fn bool osFileWrite(String filename, String data) {
  assert(false && "Not Implemented");
  bool result = true;
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


// TUI
TermIOs osStartTUI(bool blocking) {
	TermIOs old_settings;

	// Windows implementation
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	// Save old console modes
	GetConsoleMode(hStdin, &old_settings.input_mode);
	GetConsoleMode(hStdout, &old_settings.output_mode);

	// Set up alternate screen buffer
	printf("\033[?1049h");
	fflush(stdout);

	// Disable line input and echo (equivalent to ~ICANON and ~ECHO)
	DWORD new_input_mode = old_settings.input_mode;
	new_input_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);

	// Enable virtual terminal processing for ANSI escape sequences
	DWORD new_output_mode = old_settings.output_mode;
	new_output_mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

	SetConsoleMode(hStdin, new_input_mode);
	SetConsoleMode(hStdout, new_output_mode);

	SetConsoleOutputCP(CP_UTF8);

	// Note: Windows console is inherently non-blocking when using 
	// ENABLE_LINE_INPUT disabled. You can check for input with:
	// DWORD events;
	// GetNumberOfConsoleInputEvents(hStdin, &events);
	// Or use PeekConsoleInput() before ReadConsoleInput()

	return old_settings;
}

fn void osEndTUI(TermIOs old_terminal_attributes) {
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

	// Restore console modes
	SetConsoleMode(hStdin, old_terminal_attributes.input_mode);
	SetConsoleMode(hStdout, old_terminal_attributes.output_mode);

  // cleanup terminal TUI incantations
  printf("\033[?1049l");
	fflush(stdout);
}

fn Dim2 osGetTerminalDimensions() {
  Dim2 result = {0};

	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	if (GetConsoleScreenBufferInfo(hStdout, &csbi)) {
		result.width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
		result.height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
	} else {
		exit(1);
	}

  return result;
}

void osBlitToTerminal(ptr writeable_output_ansi_string, i64 count) {
	HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written;
	WriteConsole(hStdout, writeable_output_ansi_string, count, &written, NULL);
	assert(written == count);
}

bool osInitNetwork() {
	WSADATA wsaData;

	if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
		return false;
	}

	if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
		WSACleanup();
		return false;
	}

	return true;
}

void osReadConsoleInput(u8* buffer, u32 len) {
	MemoryZero(buffer, len); // reset the input so it's not contaminated by last keystroke

	if (_kbhit()) {
		buffer[0] = _getch();
		bool first_byte_is_special = buffer[0] == 0 || buffer[0] == 224;
		if (first_byte_is_special && _kbhit()) {
			u8 windows_key = _getch();
			switch (windows_key) {
				case 72: buffer[0] = 27; buffer[1] = 91; buffer[2] = 65; break; // up
				case 75: buffer[0] = 27; buffer[1] = 91; buffer[2] = 68; break; // left
				case 77: buffer[0] = 27; buffer[1] = 91; buffer[2] = 67; break; // right
				case 80: buffer[0] = 27; buffer[1] = 91; buffer[2] = 66; break; // down
				default: buffer[1] = windows_key;
			}
			/*if (_kbhit()) {
				buffer[2] = _getch();
				if (_kbhit()) {
					buffer[3] = _getch();
				}
			}*/
		}
	}
		/*
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	INPUT_RECORD ir;
	DWORD read;
	if (PeekConsoleInput(hStdin, &ir, 1, &read) && read > 0) {
		ReadConsole(hStdin, buffer, len, &read, NULL);
		ReadConsoleInput(hStdin, &ir, 1, &read);
		if (ir.EventType == KEY_EVENT) {
		  buffer[0] = ir.Event.KeyEvent.uChar.AsciiChar;
		}
	}
		*/
}

i32 osLanIPAddress() {
  i32 result = 0;
	/*
  IP_ADAPTER_INFO *pAdapterInfo;
  IP_ADAPTER_INFO *pAdapter = NULL;
  DWORD dwRetVal = 0;
  ULONG ulOutBufLen;

  pAdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
  ulOutBufLen = sizeof(IP_ADAPTER_INFO);

  // Make an initial call to GetAdaptersInfo to get the necessary size
  if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *)malloc(ulOutBufLen);
  }

  if ((dwRetVal = GetAdaptersInfo(pAdapterInfo, &ulOutBufLen)) == NO_ERROR) {
      pAdapter = pAdapterInfo;
      printf("LAN Addresses:\n");

      // TODO: test this on windows
      while (pAdapter) {
          printf("Interface: %s\n", pAdapter->AdapterName);
          printf("Description: %s\n", pAdapter->Description);
          printf("IP Address: %s\n", pAdapter->IpAddressList.IpAddress.String);
          printf("IP Address: %d\n", pAdapter->Address[0] << 24 | pAdapter->Address[1] << 16 | pAdapter->Address[2] << 8 | pAdapter->Address[3]);
          printf("\n");
          pAdapter = pAdapter->Next;
      }
  } else {
      printf("GetAdaptersInfo failed: %ld\n", dwRetVal);
  }

  if (pAdapterInfo) {
    free(pAdapterInfo);
  }
	*/
	WSADATA wsa;
	char hostname[256];
	struct addrinfo hints, *final = NULL, *ptr = NULL;
	struct sockaddr_in *sockaddr_ipv4;

	// Initialize Winsock
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		return 0;
	}

	// Get hostname
	if (gethostname(hostname, sizeof(hostname)) == SOCKET_ERROR) {
		WSACleanup();
		return 0;
	}

	// Set up hints for getaddrinfo
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;      // IPv4
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Get address info
	if (getaddrinfo(hostname, NULL, &hints, &final) != 0) {
		WSACleanup();
		return 0;
	}

	// Loop through results to find a non-loopback address
	for (ptr = final; ptr != NULL; ptr = ptr->ai_next) {
		sockaddr_ipv4 = (struct sockaddr_in *)ptr->ai_addr;
		unsigned long addr = ntohl(sockaddr_ipv4->sin_addr.s_addr);

		// Skip loopback addresses (127.x.x.x)
		if ((addr >> 24) != 127) {
			result = sockaddr_ipv4->sin_addr.s_addr;
			break;
		}
	}

	freeaddrinfo(final);
	WSACleanup();

  return ntohl(result);
}

bool osThreadJoin(Thread handle, u64 endt_us) {
  DWORD sleep_ms = os_w32_sleep_ms_from_endt_us(endt_us);
  OS_W32_Entity *entity = (OS_W32_Entity *)PtrFromInt(handle.u64[0]);
  DWORD wait_result = WAIT_OBJECT_0;
  if(entity != 0)
  {
    wait_result = WaitForSingleObject(entity->thread.handle, sleep_ms);
    CloseHandle(entity->thread.handle);
    os_w32_entity_release(entity);
  }
  return (wait_result == WAIT_OBJECT_0);
}
