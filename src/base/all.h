// good base layer overview https://www.youtube.com/watch?v=bUOOaXf9qIM
#ifndef BASE_ALL_H
#define BASE_ALL_H

///// Context Cracking
// Development Settings
#if !defined(ENABLE_ASSERT)
# define ENABLE_ASSERT 1
#endif
#if !defined(ENABLE_SANITIZER)
# define ENABLE_SANITIZER 0
#endif
#if !defined(ENABLE_MANUAL_PROFILE)
# define ENABLE_MANUAL_PROFILE 0
#endif
#if !defined(ENABLE_AUTO_PROFILE)
# define ENABLE_AUTO_PROFILE 0
#endif

#if defined(ENABLE_ANY_PROFILE)
# error user should not configure ENABLE_ANY_PROFILE
#endif

#if ENABLE_MANUAL_PROFILE || ENABLE_AUTO_PROFILE
# define ENABLE_ANY_PROFILE 1
#else
# define ENABLE_ANY_PROFILE 0
#endif

//  Untangle Compiler, OS & Architecture
#if defined(__clang__)
# define COMPILER_CLANG 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define ARCH_X64 1
// TODO verify this works on clang
# elif defined(__i386__)
#  define ARCH_X86 1
// TODO verify this works on clang
# elif defined(__arm__)
#  define ARCH_ARM 1
// TODO verify this works on clang
# elif defined(__aarch64__)
#  define ARCH_ARM64 1
# else
#  error missing ARCH detection
# endif

#elif defined(_MSC_VER)
# define COMPILER_CL 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# else
#  error missing OS detection
# endif

# if defined(_M_AMD64)
#  define ARCH_X64 1
# elif defined(_M_I86)
#  define ARCH_X86 1
# elif defined(_M_ARM)
#  define ARCH_ARM 1
// TODO ARM64?
# else
#  error missing ARCH detection
# endif

#elif defined(__GNUC__)
# define COMPILER_GCC 1

# if defined(_WIN32)
#  define OS_WINDOWS 1
# elif defined(__gnu_linux__)
#  define OS_LINUX 1
# elif defined(__APPLE__) && defined(__MACH__)
#  define OS_MAC 1
# else
#  error missing OS detection
# endif

# if defined(__amd64__)
#  define ARCH_X64 1
# elif defined(__i386__)
#  define ARCH_X86 1
# elif defined(__arm__)
#  define ARCH_ARM 1
# elif defined(__aarch64__)
#  define ARCH_ARM64 1
# else
#  error missing ARCH detection
# endif

#else
# error no context cracking for this compiler
#endif

#if !defined(COMPILER_CL)
# define COMPILER_CL 0
#endif
#if !defined(COMPILER_CLANG)
# define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_GCC)
# define COMPILER_GCC 0
#endif
#if !defined(OS_WINDOWS)
# define OS_WINDOWS 0
#endif
#if !defined(OS_LINUX)
# define OS_LINUX 0
#endif
#if !defined(OS_MAC)
# define OS_MAC 0
#endif
#if !defined(ARCH_X64)
# define ARCH_X64 0
#endif
#if !defined(ARCH_X86)
# define ARCH_X86 0
#endif
#if !defined(ARCH_ARM)
# define ARCH_ARM 0
#endif
#if !defined(ARCH_ARM64)
# define ARCH_ARM64 0
#endif

// Language
#if defined(__cplusplus)
# define LANG_CXX 1
#else
# define LANG_C 1
#endif

#if !defined(LANG_CXX)
# define LANG_CXX 0
#endif
#if !defined(LANG_C)
# define LANG_C 0
#endif

// Profiler
#if !defined(PROFILER_SPALL)
# define PROFILER_SPALL 0
#endif

// Determine Intrinsics Mode
#if OS_WINDOWS
# if COMPILER_CL || COMPILER_CLANG
#  define INTRINSICS_MICROSOFT 1
# endif
#endif

#if !defined(INTRINSICS_MICROSOFT)
# define INTRINSICS_MICROSOFT 0
#endif

// Setup Pointer Size Macro
#if ARCH_X64 || ARCH_ARM64
# define ARCH_ADDRSIZE 64
#else
# define ARCH_ADDRSIZE 32
#endif

///// MACROS
#define global   static
#define internal static
#define fn       static

#define arrayLen(a) (sizeof(a)/sizeof(*(a)))

#define intFromPtr(p) (U64)((U8*)p - (U8*)0)
#define ptrFromInt(n) (void*)((U8*)0 + (n))

#define stmnt(S) do{ S }while(0)

#if !defined(assertBreak)
# define assertBreak() (*(volatile int*)0 = 0)
#endif

#if ENABLE_ASSERT
# define assert(c) stmnt( if (!(c)){ assertBreak(); } )
#else
# define assert(c)
#endif

#define ToBool(x) ((x) != 0)

#define KB(x) ((x) << 10)
#define MB(x) ((x) << 20)
#define GB(x) ((x) << 30)
#define TB(x) ((u64)(x) << 40llu)

#include <string.h>
#define MemoryCopy(d,s,z) memmove((d), (s), (z))
#define MemoryCopyStruct(d,s) MemoryCopy((d),(s), Min(sizeof(*(d)) , sizeof(*(s))))
#define MemoryZero(d,z) memset((d), 0, (z))
#define MemoryZeroStruct(d,s) MemoryZero((d),sizeof(s))
#define Min(a,b) (((a)<(b))?(a):(b))
#define Max(a,b) (((a)>(b))?(a):(b))
#define Abs(x) (((x)<0)?((x)*-1):(x))

#define SetFlag(flags, bit)     ((flags) |= (1ULL << (bit)))
#define ClearFlag(flags, bit)   ((flags) &= ~(1ULL << (bit)))
#define ToggleFlag(flags, bit)  ((flags) ^= (1ULL << (bit)))
#define CheckFlag(flags, bit)   (((flags) >> (bit)) & 1ULL)

#define QueuePush(f,l,n) (((f)==NULL) ? ((f)=(l)=(n)) : ((l)->next=(n),(l)=(n),(n)->next = NULL))

#define DEFAULT_ALIGNMENT sizeof(void*)
#define isPowerOfTwo(x) ((x & (x-1)) == 0)

#define XYToPos(x, y, w) ((u32)(((u32)(x)) + (((u32)(y)) * (w))))

#if COMPILER_MSVC
#  define thread_static __declspec(thread)
#elif COMPILER_CLANG || COMPILER_GCC
#  define thread_static __thread
#else
#  error thread_static not defined for this compiler.
#endif

// only valid if there's an in-scope variable bool `debug_mode`
#define dbg(fmt, ...) osDebugPrint(debug_mode, fmt, ##__VA_ARGS__)

///// TYPES
// integer types
typedef unsigned char         u8;
typedef signed char           i8;
typedef unsigned short        u16;
typedef short                 i16;
typedef unsigned int          u32;
typedef int                   i32;
typedef unsigned long long    u64;
typedef signed long long      i64;
typedef char *                usize;
typedef char *                ptr;
typedef const char*           str;

// Floating point types
typedef float                 f32;
typedef double                f64;

//typedef long double f80; only returning 8 bytes on my mac for some reason
/*
  printf("u8 %d\n", (int)sizeof(u8) * 8);
  printf("i8 %d\n", (int)sizeof(i8) * 8);
  printf("u16 %d\n", (int)sizeof(u16) * 8);
  printf("i16 %d\n", (int)sizeof(i16) * 8);
  printf("u32 %d\n", (int)sizeof(u32) * 8);
  printf("i32 %d\n", (int)sizeof(i32) * 8);
  printf("u64 %d\n", (int)sizeof(u64) * 8);
  printf("i64 %d\n", (int)sizeof(i64) * 8);
  printf("usize %d\n", (int)sizeof(usize) * 8);

  printf("f32 %d\n", (int)sizeof(f32) * 8);
  printf("f64 %d\n", (int)sizeof(f64) * 8);
  printf("f80 %d\n", (int)sizeof(f80) * 8);
*/

// Boolean types
typedef u8  b8;
typedef u32 b32;
#ifndef bool
#  define bool b8
#endif

#define true 1
#define false 0

// Structs
typedef struct Arena {
  u8* memory;
  u64 max;
  u64 alloc_position;
  u64 commit_position;
  b8 static_size;
} Arena;

typedef struct PtrArray {
  u32 length;
  u32 capacity;
  ptr* items;
} PtrArray;

typedef struct u8List {
  u32 length;
  u32 capacity;
  u8* items;
} u8List;

typedef struct String {
  u32 length;
  u32 capacity;
  ptr bytes;
} String;

typedef struct StringUTF16Const {
	u16* string;
	u64 size;
} StringUTF16Const;

typedef enum Utf8Character {
  Utf8CharacterAscii,
  Utf8CharacterTwoByte,
  Utf8CharacterThreeByte,
  Utf8CharacterFourByte,
  Utf8Character_Count,
} Utf8Character;

typedef struct Box {
  u32 x;
  u32 y;
  u32 height;
  u32 width;
} Box;

typedef struct Dim2 {
  u16 height;
  u16 width;
} Dim2;

typedef struct Pos2 {
  u16 x;
  u16 y;
} Pos2;

typedef union Range1u32 Range1u32;
union Range1u32
{
  struct
  {
    u32 min;
    u32 max;
  };
  u32 v[2];
};

typedef union Range1i32 Range1i32;
union Range1i32
{
  struct
  {
    i32 min;
    i32 max;
  };
  i32 v[2];
};

typedef union Range1u64 Range1u64;
union Range1u64
{
  struct
  {
    u64 min;
    u64 max;
  };
  u64 v[2];
};

typedef union Range1i64 Range1i64;
union Range1i64
{
  struct
  {
    i64 min;
    i64 max;
  };
  i64 v[2];
};

typedef union Range1f32 Range1f32;
union Range1f32
{
  struct
  {
    f32 min;
    f32 max;
  };
  f32 v[2];
};

#if OS_WINDOWS
#  define WIN32_LEAN_AND_MEAN
#  include <Windows.h>
#  include <winsock2.h>
#  include <iphlpapi.h>
#  include <ws2tcpip.h>
#  include <timeapi.h>
#  include <conio.h>
  typedef struct {
	  DWORD input_mode;
	  DWORD output_mode;
	} TermIOs;
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <netdb.h>
#  include <arpa/inet.h>
#  include <net/if.h>
#  include <ifaddrs.h>
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <pthread.h>
	typedef struct termios TermIOs;
#endif

typedef struct Barrier {
  u64 a[1];
} Barrier;

typedef struct CondVar {
  u64 a[1];
} CondVar;

typedef struct Thread {
  pthread_t thread;
} Thread;

typedef struct Mutex {
  pthread_mutex_t mutex;
} Mutex;

typedef struct Cond {
  pthread_cond_t cond;
} Cond;

#define M_SCRATCH_SIZE KB(16)
typedef struct ScratchFreeListNode ScratchFreeListNode;
struct ScratchFreeListNode {
	ScratchFreeListNode* next;
	u32 index;
};

typedef struct ScratchMem {
	Arena arena;
	u32 index;
	u64 pos;
} ScratchMem;

typedef struct LaneCtx {
  u64 lane_idx;
  u64 lane_count;
  Barrier barrier;
  u64 *broadcast_memory;
} LaneCtx;

typedef struct ThreadContext {
	Arena arena; // scratch
	u32 max_created;
	ScratchFreeListNode* free_list;
  LaneCtx lane_ctx;
} ThreadContext;

///// HARDCODED GLOBALS
global const u64 MAX_u64 = 0xffffffffffffffffull;
global const u32 MAX_u32 = 0xffffffff;
global const u16 MAX_u16 = 0xffff;
global const u8  MAX_u8  = 0xff;

///// CUSTOM ENTRY POINT
/* TODO?
fn void mainThreadBaseEntryPoint(i32 argc, char **argv);
fn void asyncThreadEntryPoint(void *params);
fn void supplement_thread_base_entry_point(void (*entry_point)(void *params), void *params);
fn u64 update_tick_idx(void);
fn b32 update(void);
*/

///// MATH
fn Range1u64 range1u64Create(u64 min, u64 max);
fn Range1u64 mRangeFromNIdxMCount(u64 n_idx, u64 n_count, u64 m_count);
fn void u32Quicksort(u32 arr[], u32 low, u32 high);
fn void u32ReverseArray(u32 arr[], u32 size);

///// MEMORY (Arenas)
#define ARENA_MAX GB(1)
// this was an evil bug to figure out
#if defined(OS_MAC)
#  define ARENA_COMMIT_SIZE KB(16)
#else
#  define ARENA_COMMIT_SIZE KB(8)
#endif

fn void* arenaAlloc(Arena* arena, u64 size);
fn void* arenaAllocZero(Arena* arena, u64 size);
fn void  arenaDealloc(Arena* arena, u64 size);
fn void  arenaDeallocTo(Arena* arena, u64 pos);
fn void* arenaRaise(Arena* arena, void* ptr, u64 size);
fn void* arenaAllocArraySized(Arena* arena, u64 elem_size, u64 count);
#define arenaAllocArray(arena, elem_type, count) arenaAllocArraySized(arena, sizeof(elem_type), count)

fn void arenaInit(Arena* arena);
fn void arenaInitSized(Arena* arena, u64 max);
fn void arenaClear(Arena* arena);
fn void arenaFree(Arena* arena);

ScratchMem scratchGet(void);
void scratchReset(ScratchMem* scratch);
void scratchReturn(ScratchMem* scratch);

///// STRINGS
#define ASCII_TAB       (9)
#define ASCII_LINE_FEED (10)
#define ASCII_RETURN    (13)
#define ASCII_ESCAPE    (27)
#define ASCII_DEL       (127)
#define ASCII_BACKSPACE (8)

fn bool stringsEq(String* a, String* b);
fn bool cStringEqString(str a, String* b);
fn Utf8Character classifyUtf8Character(u8 c);
fn bool isUtf8Ascii(u8 c);
fn bool isUtf8TwoByte(u8 c);
fn bool isUtf8ThreeByte(u8 c);
fn bool isUtf8FourByte(u8 c);
fn u8 lowerAscii(u8 c);
fn u8 upperAscii(u8 c);
fn StringUTF16Const str16FromStr8(Arena* a, String string);
fn bool isAlphaUnderscoreSpace(u8 c);
fn bool isSimplePrintable(u8 c);

///// OS-wrapped apis
void osInit();
void* osThreadContextGet();
void osThreadContextSet(void* ctx);

fn Barrier osBarrierAlloc(u64 count);
fn void osBarrierRelease(Barrier barrier);
fn void osBarrierWait(Barrier barrier);

// Memory
fn void* osMemoryReserve(u64 size);
fn void  osMemoryCommit(void* memory, u64 size);
fn void  osMemoryDecommit(void* memory, u64 size);
fn void  osMemoryRelease(void* memory, u64 size);
fn u64   osTimeMicrosecondsNow();
fn void  osSleepMicroseconds(u32 t);

fn bool osFileExists(String filename);
fn String osFileRead(Arena* arena, ptr filepath);
fn bool osFileCreate(String filename);
fn bool osFileCreateWrite(String filename, String data);
fn bool osFileWrite(String filename, String data);

fn void osDebugPrint(bool debug_mode, const char* format, ...);

TermIOs osStartTUI(bool blocking);
fn void osEndTUI(TermIOs old_terminal_attributes);
fn Dim2 osGetTerminalDimensions();
void osBlitToTerminal(ptr writeable_output_ansi_string, i64 count);
void osReadConsoleInput(u8* buffer, u32 len);

bool osInitNetwork();
i32 osLanIPAddress();

bool osThreadJoin(Thread handle, u64 endt_us);

///// Basic THREAD synchronization apis
Thread spawnThread(void * (*threadFn)(void *), void* thread_arg);
Mutex newMutex();
Cond newCond();
void lockMutex(Mutex* m);
void unlockMutex(Mutex* m);
void signalCond(Cond* cond);
void waitForCondSignal(Cond* cond, Mutex* mutex);

///// Multi-Core by Default ThreadContext stuff
void tctxInit(ThreadContext* ctx);
void tctxFree(ThreadContext* ctx);
fn ThreadContext *tctxSelected(void);

ScratchMem tctxScratchGet(ThreadContext* ctx);
void tctxScratchReset(ThreadContext* ctx, ScratchMem* scratch);
void tctxScratchReturn(ThreadContext* ctx, ScratchMem* scratch);

fn LaneCtx tctxSetLaneCtx(LaneCtx lane_ctx);
fn void tctxLaneBarrierWait(void *broadcast_ptr, u64 broadcast_size, u64 broadcast_src_lane_idx);
#define LaneIdx() (tctxSelected()->lane_ctx.lane_idx)
#define LaneCount() (tctxSelected()->lane_ctx.lane_count)
#define LaneFromTaskIdx(idx) ((idx)%LaneCount())
#define LaneCtx(ctx) tctxSetLaneCtx((ctx))
#define LaneSync() tctxLaneBarrierWait(0, 0, 0)
#define LaneSyncu64(pointer, src_lane_idx) tctxLaneBarrierWait((pointer), sizeof(*(pointer)), (src_lane_idx))
#define LaneRange(count) mRangeFromNIdxMCount(LaneIdx(), LaneCount(), (count))


#endif// BASE_ALL_H
