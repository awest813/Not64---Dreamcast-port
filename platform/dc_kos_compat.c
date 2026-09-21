/* Older libgcc/libstdc++ objects call an out-of-line mutex_lock. Current KOS
 * headers inline it; otherwise the linker can select newlib's weak no-op
 * stub and the matching KOS mutex_unlock asserts during frame registration. */
#define mutex_lock kos_header_mutex_lock
#include <kos/mutex.h>
#undef mutex_lock

int mutex_lock(mutex_t *mutex)
{
    return mutex_lock_timed(mutex, 0);
}
