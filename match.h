#ifndef _MATCH_H_
#define _MATCH_H_

#include <stdint.h>

typedef void (*match_cb_t)(const char* file,
                int mode, uint64_t mtime, uint64_t size);

/**
 * search files in 'path' which matchs 'pattern'.
 * 'cb' is called for every matched file.
 * 'pattern' is shell-style pattern string, e.g. "*.[ch]".
 * for compatibility, '\' is not recognized as file separator on windows.
 */
void match_files(const char* path, const char* pattern, match_cb_t cb);

#endif // _MATCH_H_