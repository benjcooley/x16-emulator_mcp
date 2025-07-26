// Commander X16 Emulator
// Copyright (c) 2019 Michael Steil
// Copyright (c) 2020 Frank van den Hoef
// All rights reserved. License: 2-clause BSD

#include "utils.h"
#include "logging.h"

#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif

// Helper function to create a single directory; already existing dir okay
static int maybe_mkdir(const char* path, mode_t mode)
{
    struct stat st;
    errno = 0;

    // Try to make the directory
    if (mkdir(path, mode) == 0)
        return 0;

    // If it fails for any reason but EEXIST, fail
    if (errno != EEXIST)
        return -1;

    // Check if the existing path is a directory
    if (stat(path, &st) != 0)
        return -1;

    // If not, fail with ENOTDIR
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }

    errno = 0;
    return 0;
}

// Robust recursive directory creation (mkdir -p equivalent)
// Based on proven implementation from https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950
int create_directory_recursive(const char *path)
{
    char *_path = NULL;
    char *p;
    int result = -1;
    mode_t mode = 0755; // Use 0755 instead of 0777 for better security
    
    errno = 0;

    // Copy string so it's mutable
    _path = strdup(path);
    if (_path == NULL)
        goto out;

    // Iterate the string
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            // Temporarily truncate
            *p = '\0';

            if (maybe_mkdir(_path, mode) != 0) {
                goto out;
            }

            *p = '/';
        }
    }

    if (maybe_mkdir(_path, mode) != 0) {
        goto out;
    }

    result = 0;

out:
    free(_path);
    return result;
}
