#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#endif

#include "match.h"
#include "xstring.h"

/* glob_match() is from Linux kernel (lib/glob.c). */

/**
 * glob_match - Shell-style pattern matching, like !fnmatch(pat, str, 0)
 * @pat: Shell-style pattern to match, e.g. "*.[ch]".
 * @str: String to match.  The pattern must match the entire string.
 *
 * Perform shell-style glob matching, returning true (1) if the match
 * succeeds, or false (0) if it fails.  Equivalent to !fnmatch(@pat, @str, 0).
 *
 * Pattern metacharacters are ?, *, [ and \.
 * (And, inside character classes, !, - and ].)
 *
 * This is small and simple implementation intended for device blacklists
 * where a string is matched against a number of patterns.  Thus, it
 * does not preprocess the patterns.  It is non-recursive, and run-time
 * is at most quadratic: strlen(@str)*strlen(@pat).
 *
 * An example of the worst case is glob_match("*aaaaa", "aaaaaaaaaa");
 * it takes 6 passes over the pattern before matching the string.
 *
 * Like !fnmatch(@pat, @str, 0) and unlike the shell, this does NOT
 * treat / or leading . specially; it isn't actually used for pathnames.
 *
 * Note that according to glob(7) (and unlike bash), character classes
 * are complemented by a leading !; this does not support the regex-style
 * [^a-z] syntax.
 *
 * An opening bracket without a matching close is matched literally.
 */
static int glob_match(char const *pat, char const *str)
{
    /*
     * Backtrack to previous * on mismatch and retry starting one
     * character later in the string.  Because * matches all characters
     * (no exception for /), it can be easily proved that there's
     * never a need to backtrack multiple levels.
     */
    char const *back_pat = NULL, *back_str = NULL;

    /*
     * Loop over each token (character or class) in pat, matching
     * it against the remaining unmatched tail of str.  Return false
     * on mismatch, or true after matching the trailing nul bytes.
     */
    for (;;) {
        unsigned char c = *str++;
        unsigned char d = *pat++;

        switch (d) {
        case '?':   /* Wildcard: anything but nul */
            if (c == '\0')
                return 0;
            break;
        case '*':   /* Any-length wildcard */
            if (*pat == '\0')   /* Optimize trailing * case */
                return 1;
            back_pat = pat;
            back_str = --str;   /* Allow zero-length match */
            break;
        case '[': { /* Character class */
            int match = 0, inverted = (*pat == '!');
            char const *class = pat + inverted;
            unsigned char a = *class++;

            /*
             * Iterate over each span in the character class.
             * A span is either a single character a, or a
             * range a-b.  The first span may begin with ']'.
             */
            do {
                unsigned char b = a;

                if (a == '\0')  /* Malformed */
                    goto literal;

                if (class[0] == '-' && class[1] != ']') {
                    b = class[1];

                    if (b == '\0')
                        goto literal;

                    class += 2;
                    /* Any special action if a > b? */
                }
                match |= (a <= c && c <= b);
            } while ((a = *class++) != ']');

            if (match == inverted)
                goto backtrack;
            pat = class;
            }
            break;
        case '\\':
            d = *pat++;
            /*FALLTHROUGH*/
        default:    /* Literal character */
literal:
            if (c == d) {
                if (d == '\0')
                    return 1;
                break;
            }
backtrack:
            if (c == '\0' || !back_pat)
                return 0;   /* No point continuing */
            /* Try again from last *, one character later in str. */
            pat = back_pat;
            str = ++back_str;
            break;
        }
    }
}

static int is_ignored(const char* path, char* const ignores[])
{
    while (ignores[0]) {
        if (glob_match(ignores[0], path)) {
            return 1;
        }
        ++ignores;
    }
    return 0;
}

/* check if equal to "." or ".." */
static inline int is_valid_name(const char* s)
{
    return !(s[0] == '.' && (s[1] == '\0' || (s[1] == '.' && s[2] == '\0')));
}

#ifdef _WIN32

static inline int fattr2mode(DWORD attrs)
{
    int mode = 0644;

    if (attrs & FILE_ATTRIBUTE_DIRECTORY) {
        mode |= LIBSSH2_SFTP_S_IFDIR;
    } else {
        mode |= LIBSSH2_SFTP_S_IFREG;
    }
    return mode;
}

/* FILETIME to UNIX time */
static inline time_t filetime2time(FILETIME t)
{
    return (time_t)(((uint64_t)t.dwHighDateTime << 32 | t.dwLowDateTime)
            / 10000000 - 11644473600LL);
}

static void new_file_item(xlist_t* items, const char* file,
        const WIN32_FILE_ATTRIBUTE_DATA* fattrs)
{
    file_item_t* item = xlist_alloc_back(items);

    item->file = _strdup(file);
    item->mode = fattr2mode(fattrs->dwFileAttributes);
    item->mtime = filetime2time(fattrs->ftLastWriteTime);
    item->size = (uint64_t)fattrs->nFileSizeHigh << 32 | fattrs->nFileSizeLow;
}

static void iterate_local_directory(xlist_t* items, xstr_t* path, size_t baseoff,
        char* const ignores[])
{
    WIN32_FIND_DATAA fdata;
    HANDLE fh;
    size_t off;

    xstr_push_back(path, '*');
    fh = FindFirstFileA(xstr_data(path), &fdata);
    xstr_pop_back(path);

    if (fh == INVALID_HANDLE_VALUE) {
        return;
    }
    off = xstr_size(path);
    do {
        if (is_valid_name(fdata.cFileName)) {
            xstr_append(path, fdata.cFileName);

            if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                xstr_push_back(path, '/');

                if (!is_ignored(xstr_data(path) + baseoff, ignores)) {
                    new_file_item(items, xstr_data(path) + baseoff,
                        (WIN32_FILE_ATTRIBUTE_DATA*)&fdata);
                    iterate_local_directory(items, path, baseoff, ignores);
                }
            } else if (!is_ignored(xstr_data(path) + baseoff, ignores)) {

                new_file_item(items, xstr_data(path) + baseoff,
                    (WIN32_FILE_ATTRIBUTE_DATA*)&fdata);
            }

            xstr_erase_after(path, off);
        }
    }
    while (FindNextFileA(fh, &fdata));

    FindClose(fh);
}

#else
static void new_file_item(xlist_t* items, const char* file, const struct stat* st)
{
    file_item_t* item = xlist_alloc_back(items);

    item->file = strdup(file);
    item->mode = st->st_mode;
    item->mtime = st->st_mtime;
    item->size = st->st_size;
}

static void iterate_local_directory(xlist_t* items, xstr_t* path, size_t baseoff,
        char* const ignores[], int (*statcb)(const char*, struct stat*))
{
    DIR* dir;
    struct dirent* ent;
    struct stat st;
    size_t off;

    dir = opendir(xstr_data(path));
    if (!dir) {
        return;
    }

    off = xstr_size(path);
    while (!!(ent = readdir(dir))) {
        if (is_valid_name(ent->d_name)) {
            xstr_append(path, ent->d_name);

            if (statcb(xstr_data(path), &st) == 0) {
                if (S_ISDIR(st.st_mode)) {
                    xstr_push_back(path, '/');

                    if (!is_ignored(xstr_data(path) + baseoff, ignores)) {
                        new_file_item(items, xstr_data(path) + baseoff, &st);
                        iterate_local_directory(items, path, baseoff, ignores, statcb);
                    }
                } else if (!is_ignored(xstr_data(path) + baseoff, ignores)) {

                    new_file_item(items, xstr_data(path) + baseoff, &st);
                }
            }

            xstr_erase_after(path, off);
        }
    }

    closedir(dir);
}
#endif

static void new_remote_file_item(xlist_t* items, const char* file,
        const LIBSSH2_SFTP_ATTRIBUTES* attrs)
{
    file_item_t* item = xlist_alloc_back(items);
#ifdef _WIN32
    item->file = _strdup(file);
#else
    item->file = strdup(file);
#endif
    item->mode = attrs->permissions;
    item->mtime = attrs->mtime;
    item->size = attrs->filesize;
}

static void iterate_remote_directory(xlist_t* items, xstr_t* path, size_t baseoff,
        char* const ignores[], int follnk, LIBSSH2_SFTP* sftp)
{
    LIBSSH2_SFTP_HANDLE* dir;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    char name[512];
    size_t off;

    dir = libssh2_sftp_opendir(sftp, xstr_data(path));
    if (!dir) {
        return;
    }

    off = xstr_size(path);
    while (libssh2_sftp_readdir(dir, name, sizeof(name), &attrs) > 0) {
        if (is_valid_name(name)) {
            xstr_append(path, name);

            if (!LIBSSH2_SFTP_S_ISLNK(attrs.permissions) || !follnk
                    || libssh2_sftp_stat_ex(sftp, xstr_data(path), xstr_size(path),
                            LIBSSH2_SFTP_STAT, &attrs) == 0) {
                if (LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
                    xstr_push_back(path, '/');

                    if (!is_ignored(xstr_data(path) + baseoff, ignores)) {
                        new_remote_file_item(items, xstr_data(path) + baseoff, &attrs);
                        iterate_remote_directory(items, path, baseoff, ignores, follnk, sftp);
                    }
                } else if (!is_ignored(xstr_data(path) + baseoff, ignores)) {

                    new_remote_file_item(items, xstr_data(path) + baseoff, &attrs);
                }
            }

            xstr_erase_after(path, off);
        }
    }

    libssh2_sftp_closedir(dir);
}

static void free_file_item(void* v)
{
    file_item_t* item = v;

    free(item->file);
}

static int cmp_file_item(void* l, void* r)
{
    return strcmp(((file_item_t*)l)->file, ((file_item_t*)r)->file);
}

xlist_t* iterate_directory(const char* _path, char* const ignores[], int follnk, LIBSSH2_SFTP* sftp)
{
    xlist_t* items = xlist_new(sizeof(file_item_t), free_file_item);
    xstr_t path;

    xstr_init_ex(&path, 512);
    xstr_append(&path, _path);

    if (xstr_back(&path) != '/') {
        xstr_push_back(&path, '/');
    }
    if (sftp) {
        iterate_remote_directory(items, &path, xstr_size(&path), ignores, follnk, sftp);
    } else {
#ifdef _WIN32
        iterate_local_directory(items, &path, xstr_size(&path), ignores);
#else
        iterate_local_directory(items, &path, xstr_size(&path), ignores,
            follnk ? stat : lstat);
#endif
    }
    xlist_msort(items, cmp_file_item);

    xstr_destroy(&path);
    return items;
}

static inline int file_type_equal(int t1, int t2)
{
    return (t1 & LIBSSH2_SFTP_S_IFMT) == (t2 & LIBSSH2_SFTP_S_IFMT);
}

void iterate_directory_setextra(xlist_t* items, const char* _path, int follnk, LIBSSH2_SFTP* sftp)
{
    xstr_t path;
    size_t off;

    xstr_init_ex(&path, 512);
    xstr_append(&path, _path);
    xstr_push_back(&path, '/');

    off = xstr_size(&path);
    if (sftp) {
        LIBSSH2_SFTP_ATTRIBUTES attrs;

        for (xlist_iter_t i = xlist_begin(items);
                i != xlist_end(items); i = xlist_iter_next(i)) {
            file_item_t* item = xlist_iter_value(i);

            xstr_assign_at(&path, off, item->file);
            if (libssh2_sftp_stat_ex(sftp, xstr_data(&path), xstr_size(&path),
                    follnk ? LIBSSH2_SFTP_STAT : LIBSSH2_SFTP_LSTAT, &attrs) < 0) {
                item->is_newer = libssh2_sftp_last_error(sftp) == LIBSSH2_FX_NO_SUCH_FILE;
                item->is_exist = 0;
            } else {
                item->is_newer = file_type_equal(attrs.permissions, item->mode)
                        && attrs.mtime < item->mtime;
                item->is_exist = 1;
            }
        }
    } else {
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA fattrs;
#else
        struct stat statbuf;
#endif
        for (xlist_iter_t i = xlist_begin(items);
                i != xlist_end(items); i = xlist_iter_next(i)) {
            file_item_t* item = xlist_iter_value(i);

            xstr_assign_at(&path, off, item->file);
            /* check remote file's mtime */
#ifdef _WIN32
            if (GetFileAttributesExA(xstr_data(&path), GetFileExInfoStandard, &fattrs)) {
                item->is_newer = file_type_equal(fattr2mode(fattrs.dwFileAttributes), item->mode)
                        && filetime2time(fattrs.ftLastWriteTime) < item->mtime;
                item->is_exist = 1;
            } else {
                DWORD e = GetLastError();
                item->is_newer = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND);
                item->is_exist = 0;
            }
#else
            if ((follnk ? stat(xstr_data(&path), &statbuf)
                        : lstat(xstr_data(&path), &statbuf)) < 0) {
                item->is_newer = errno == ENOENT;
                item->is_exist = 0;
            } else {
                item->is_newer = file_type_equal(statbuf.st_mode, item->mode)
                        && statbuf.st_mtime < item->mtime;
                item->is_exist = 1;
            }
#endif
        }
    }

    xstr_destroy(&path);
}

void iterate_directory_free(xlist_t* items)
{
    xlist_free(items);
}
