#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H
/* A missing file is fine; its parent must be writable and /data mounted. */
int settings_store_available(const char *path);
#endif
