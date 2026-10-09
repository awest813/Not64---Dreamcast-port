#include "dc_storage.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int dc_storage_commit(const char *temporary, const char *destination)
{
    char backup[256];
    struct stat info;
    int had_previous;
    if (!temporary || !destination ||
        snprintf(backup, sizeof(backup), "%s.bak", destination) >= (int)sizeof(backup))
        return -1;
    /* POSIX filesystems can replace the destination in one operation. */
    if (rename(temporary, destination) == 0) return 0;
    had_previous = stat(destination, &info) == 0;
    if (!had_previous) return -1;
    /* FAT may require moving the previous file first. Never overwrite an
       existing recovery copy: it may be the only intact save after a crash. */
    if (stat(backup, &info) == 0 || rename(destination, backup) != 0) return -1;
    if (rename(temporary, destination) != 0) {
        (void)rename(backup, destination);
        return -1;
    }
    (void)remove(backup);
    return 0;
}
