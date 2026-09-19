/**
 * POSIX/KOS fileBrowser backend.
 * On hardware, KOS maps SD as /sd. Host stub uses the process cwd.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>

#include "fileBrowser.h"
#include "fileBrowser-kos.h"

#ifdef DC_HOST_STUB
#define KOS_ROM_DIR  "./roms"
#define KOS_SAVE_DIR "./saves"
#else
#define KOS_ROM_DIR  "/sd/not64/roms"
#define KOS_SAVE_DIR "/sd/not64/saves"
#endif

fileBrowser_file topLevel_kos = {
	KOS_ROM_DIR,
	0, 0, 0,
	FILE_BROWSER_ATTR_DIR
};

fileBrowser_file saveDir_kos = {
	KOS_SAVE_DIR,
	0, 0, 0,
	FILE_BROWSER_ATTR_DIR
};

static void ensure_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return;
	mkdir(path, 0755);
}

int fileBrowser_kos_init(fileBrowser_file *f)
{
	if (!f)
		return FILE_BROWSER_ERROR;
	if (f->attr & FILE_BROWSER_ATTR_DIR)
		ensure_dir(f->name);
	f->offset = 0;
	return 0;
}

int fileBrowser_kos_deinit(fileBrowser_file *f)
{
	(void)f;
	return 0;
}

int fileBrowser_kos_readDir(fileBrowser_file *file, fileBrowser_file **dir)
{
	DIR *dp;
	struct dirent *de;
	fileBrowser_file *out = NULL;
	int count = 0;

	if (!file || !dir)
		return FILE_BROWSER_ERROR;

	dp = opendir(file->name);
	if (!dp)
		return FILE_BROWSER_ERROR_NO_FILE;

	while ((de = readdir(dp)) != NULL) {
		fileBrowser_file *tmp;
		char full[FILE_BROWSER_MAX_PATH_LEN];
		struct stat st;

		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
			continue;

		tmp = realloc(out, sizeof(fileBrowser_file) * (count + 1));
		if (!tmp) {
			free(out);
			closedir(dp);
			return FILE_BROWSER_ERROR;
		}
		out = tmp;
		memset(&out[count], 0, sizeof(fileBrowser_file));

		if (snprintf(full, sizeof(full), "%s/%s", file->name, de->d_name) >=
		    (int)sizeof(full)) {
			strncpy(out[count].name, de->d_name, FILE_BROWSER_MAX_PATH_LEN - 1);
		} else {
			strncpy(out[count].name, full, FILE_BROWSER_MAX_PATH_LEN - 1);
		}

		if (stat(out[count].name, &st) == 0) {
			out[count].size = (unsigned int)st.st_size;
			if (S_ISDIR(st.st_mode))
				out[count].attr = FILE_BROWSER_ATTR_DIR;
		}
		count++;
	}
	closedir(dp);
	*dir = out;
	return count;
}

int fileBrowser_kos_readFile(fileBrowser_file *file, void *buffer, unsigned int length)
{
	FILE *fp;
	size_t n;

	if (!file || !buffer)
		return FILE_BROWSER_ERROR;

	fp = fopen(file->name, "rb");
	if (!fp)
		return FILE_BROWSER_ERROR_NO_FILE;

	if (fseek(fp, (long)file->offset, SEEK_SET) != 0) {
		fclose(fp);
		return FILE_BROWSER_ERROR;
	}
	n = fread(buffer, 1, length, fp);
	file->offset += (unsigned int)n;
	fclose(fp);
	return (int)n;
}

int fileBrowser_kos_writeFile(fileBrowser_file *file, void *buffer, unsigned int length)
{
	FILE *fp;
	size_t n;

	if (!file || !buffer)
		return FILE_BROWSER_ERROR;

	fp = fopen(file->name, "r+b");
	if (!fp)
		fp = fopen(file->name, "wb");
	if (!fp)
		return FILE_BROWSER_ERROR;

	if (fseek(fp, (long)file->offset, SEEK_SET) != 0) {
		fclose(fp);
		return FILE_BROWSER_ERROR;
	}
	n = fwrite(buffer, 1, length, fp);
	file->offset += (unsigned int)n;
	fclose(fp);
	return (int)n;
}

int fileBrowser_kos_seekFile(fileBrowser_file *file, unsigned int offset, unsigned int origin)
{
	if (!file)
		return FILE_BROWSER_ERROR;

	switch (origin) {
	case FILE_BROWSER_SEEK_SET:
		file->offset = offset;
		break;
	case FILE_BROWSER_SEEK_CUR:
		file->offset += offset;
		break;
	case FILE_BROWSER_SEEK_END:
		file->offset = file->size + offset;
		break;
	default:
		return FILE_BROWSER_ERROR;
	}
	return 0;
}

void fileBrowser_kos_bind(void)
{
	romFile_topLevel = &topLevel_kos;
	romFile_init = fileBrowser_kos_init;
	romFile_readDir = fileBrowser_kos_readDir;
	romFile_readFile = fileBrowser_kos_readFile;
	romFile_seekFile = fileBrowser_kos_seekFile;
	romFile_deinit = fileBrowser_kos_deinit;

	saveFile_dir = &saveDir_kos;
	saveFile_init = fileBrowser_kos_init;
	saveFile_readFile = fileBrowser_kos_readFile;
	saveFile_writeFile = fileBrowser_kos_writeFile;
	saveFile_deinit = fileBrowser_kos_deinit;
}
