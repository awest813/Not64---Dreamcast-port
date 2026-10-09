#ifndef DC_STORAGE_H
#define DC_STORAGE_H
/* Commit a closed temporary file without deleting the previous save on failure. */
int dc_storage_commit(const char *temporary, const char *destination);
#endif
