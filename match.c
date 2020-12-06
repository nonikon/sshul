#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
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
    char const *back_pat = 0, *back_str = back_str;

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

#ifdef _WIN32
/* FILETIME to UNIX time */
static inline uint64_t time_win2unix(PFILETIME t)
{
#define WIN_TICK 10000000
#define SEC_TO_UNIX_EPOCH 11644473600LL
    return ((uint64_t)t->dwHighDateTime << 32 | t->dwLowDateTime)
            / WIN_TICK - SEC_TO_UNIX_EPOCH;
}
#endif

/* ckech if equal to "." or ".." */
static inline int is_valid_name(const char* s)
{
    return !(s[0] == '.' && (s[1] == '\0' || (s[1] == '.' && s[2] == '\0')));
}

/* check if equal to "**" */
static inline int is_match_all(const char* s)
{
    return s[0] == '*' && s[1] == '*' && s[2] == '\0';
}

/* match files recursively. */
static void match_files_rec(xstr_t* path, char* pattern,
                match_cb_t cb, unsigned baseoff)
{
    unsigned off;
    char* p;
#ifndef _WIN32
    struct stat s;
#endif

    do {
        /* find file separater or wildcard chars. */
        p = strpbrk(pattern, "/*?[");

        if (!p) {
            /* found nothing, check file exist and break. */
#ifdef _WIN32
            WIN32_FILE_ATTRIBUTE_DATA fattr;
#endif
            off = xstr_size(path);
            xstr_append(path, pattern, -1);

#ifdef _WIN32
            if (GetFileAttributesExA(xstr_data(path), GetFileExInfoStandard, &fattr)
                    && !(fattr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {

                cb(xstr_data(path) + baseoff, 0644, time_win2unix(&fattr.ftLastWriteTime),
                    (uint64_t)fattr.nFileSizeHigh << 32 | fattr.nFileSizeLow);
            }
#else
            if (stat(xstr_data(path), &s) != -1 && S_ISREG(s.st_mode)) {
                cb(xstr_data(path) + baseoff, s.st_mode & 0777, s.st_mtime, s.st_size);
            }
#endif
            xstr_erase(path, off, -1);
            break;
        }

        if (p[0] != '/') {
            /* found a wildcard char, match files/dirs and break. */
#ifdef _WIN32
            WIN32_FIND_DATAA fdata;
            HANDLE fh;
#else
            struct dirent* ent;
            DIR* dir;
#endif

#ifdef _WIN32
            xstr_push_back(path, '*');
            fh = FindFirstFileA(xstr_data(path), &fdata);
            xstr_pop_back(path);
#else
            dir = opendir(xstr_data(path));
#endif

#ifdef _WIN32
            if (fh == INVALID_HANDLE_VALUE) {
#else
            if (!dir) {
#endif
                /* 'path' not valid, break. */
                break;
            }

            if (is_match_all(pattern)) {
                /* pattern equal to "**", match file recursively and break. */
#ifdef _WIN32
                do {
                    if (!is_valid_name(fdata.cFileName)) continue;

                    off = xstr_size(path);
                    xstr_append(path, fdata.cFileName, -1);

                    if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                        xstr_push_back(path, '/');
                        match_files_rec(path, pattern, cb, baseoff);
                    } else {
                        cb(xstr_data(path) + baseoff, 0644, time_win2unix(&fdata.ftLastWriteTime),
                            (uint64_t)fdata.nFileSizeHigh << 32 | fdata.nFileSizeLow);
                    }

                    xstr_erase(path, off, -1);
                }
                while (FindNextFileA(fh, &fdata));
                FindClose(fh);
#else
                while (!!(ent = readdir(dir))) {

                    if (!is_valid_name(ent->d_name)) continue;

                    off = xstr_size(path);
                    xstr_append(path, ent->d_name, -1);

                    if (stat(xstr_data(path), &s) != -1) {
                        if (S_ISDIR(s.st_mode)) {
                            xstr_push_back(path, '/');
                            match_files_rec(path, pattern, cb, baseoff);
                        } else if (S_ISREG(s.st_mode)) {
                            cb(xstr_data(path) + baseoff, s.st_mode & 0777,
                                s.st_mtime, s.st_size);
                        }
                    }

                    xstr_erase(path, off, -1);
                }
                closedir(dir);
#endif
                break;
            }

            /* find next file separater. */
            p = strchr(p, '/');

            if (!p) {
                /* file separater not found, match file and break. */
#ifdef _WIN32
                do {
                    if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                            && glob_match(pattern, fdata.cFileName)) {

                        off = xstr_size(path);
                        xstr_append(path, fdata.cFileName, -1);

                        cb(xstr_data(path) + baseoff, 0644, time_win2unix(&fdata.ftLastWriteTime),
                            (uint64_t)fdata.nFileSizeHigh << 32 | fdata.nFileSizeLow);

                        xstr_erase(path, off, -1);
                    }
                }
                while (FindNextFileA(fh, &fdata));
#else
                while (!!(ent = readdir(dir))) {

                    if (is_valid_name(ent->d_name)
                            && glob_match(pattern, ent->d_name)) {

                        off = xstr_size(path);
                        xstr_append(path, ent->d_name, -1);

                        if (stat(xstr_data(path), &s) != -1
                                && S_ISREG(s.st_mode)) {
                            cb(xstr_data(path) + baseoff, s.st_mode & 0777,
                                s.st_mtime, s.st_size);
                        }

                        xstr_erase(path, off, -1);
                    }
                }
#endif
            } else {
                /* file separater found, match directory recursively and break. */
                p[0] = '\0';
#ifdef _WIN32
                do {
                    if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                            && is_valid_name(fdata.cFileName)
                            && glob_match(pattern, fdata.cFileName)) {

                        off = xstr_size(path);

                        xstr_append(path, fdata.cFileName, -1);
                        xstr_push_back(path, '/');

                        match_files_rec(path, p + 1, cb, baseoff);

                        xstr_erase(path, off, -1);
                    }
                }
                while (FindNextFileA(fh, &fdata));
#else
                while (!!(ent = readdir(dir))) {

                    if (is_valid_name(ent->d_name)
                            && glob_match(pattern, ent->d_name)) {

                        off = xstr_size(path);

                        xstr_append(path, ent->d_name, -1);
                        xstr_push_back(path, '/');

                        if (stat(xstr_data(path), &s) != -1
                                && S_ISDIR(s.st_mode)) {
                            match_files_rec(path, p + 1, cb, baseoff);
                        }

                        xstr_erase(path, off, -1);
                    }
                }
#endif
                p[0] = '/';
            }

#ifdef _WIN32
            FindClose(fh);
#else
            closedir(dir);
#endif
            break;
        }

        /* found a file separater, append dir name to 'path' and continue. */
        xstr_append(path, pattern, p - pattern);
        xstr_push_back(path, '/');

        pattern = p + 1;
    }
    while (1);
}

void match_files(const char* path, const char* pattern, match_cb_t cb)
{
    xstr_t _path;
    xstr_t _pattern;

    xstr_init(&_path, 128);
    xstr_init_with(&_pattern, pattern, -1);

    xstr_append(&_path, path, -1);
    if (xstr_back(&_path) != '/') {
        xstr_push_back(&_path, '/');
    }

    match_files_rec(&_path, xstr_data(&_pattern),
        cb, xstr_size(&_path));

    xstr_destroy(&_path);
    xstr_destroy(&_pattern);
}
