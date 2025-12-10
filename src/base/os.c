#include "all.h"

#if OS_WINDOWS
#  include "win32_os.c"
#elif OS_LINUX
#  include "linux_os.c"
#  include "unix_os.c"
#elif OS_MAC
#  include "pthread_barrier.c"
#  include "mac_os.c"
#  include "unix_os.c"
#endif
