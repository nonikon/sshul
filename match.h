#ifndef _MATCH_H_
#define _MATCH_H_

#include <stdint.h>
#include <time.h>
#include <libssh2_sftp.h>

#include "xlist.h"

typedef struct {
    char* file;
    int mode;
    time_t mtime;
    uint64_t size;
    /* extra */
    int is_newer;
    int is_exist;
} file_item_t;

/* <ignores> is shell-style pattern strings, e.g. "*.[ch]", "*.?", "*.[a-z]".
 * for compatibility, '\' is not recognized as file separator on Windows.
 */
xlist_t* iterate_directory(const char* path, char* const ignores[],
        int follnk, LIBSSH2_SFTP* sftp);
void iterate_directory_setextra(xlist_t* items, const char* path,
        int follnk, LIBSSH2_SFTP* sftp);
void iterate_directory_free(xlist_t* items);

#endif // _MATCH_H_