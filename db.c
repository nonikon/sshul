#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "db.h"

typedef struct {
    char*       path;
    uint64_t    mtime;
    uint32_t    len;    // strlen(path) + 1
    uint32_t    mod;    // modified ?
    int         fpos;   // ftell() of mtime
} file_info_t;

static unsigned _hash_cb(void* v)
{
    return xhash_string_hash(((file_info_t*)v)->path);
}
static int _equal_cb(void* l, void* r)
{
    return strcmp(((file_info_t*)l)->path,
                ((file_info_t*)r)->path) == 0;
}
static void _destr_cb(void* v)
{
    free(((file_info_t*)v)->path);
}

int db_check(db_t* db, const char* fname, uint64_t t)
{
    file_info_t* pf = xhash_get_ex(db, &fname);

    return pf == XHASH_INVALID || t > pf->mtime;
}

int db_update(db_t* db, const char* fname, uint64_t t)
{
    file_info_t* pf = xhash_get_ex(db, &fname);

    if (pf == XHASH_INVALID) {
        file_info_t f;

        /* not exist, add */
        f.len   = strlen(fname) + 1;
        f.path  = malloc(f.len);
        f.mtime = t;
        f.mod   = 1;
        f.fpos  = -1;
        memcpy(f.path, fname, f.len);
        xhash_put(db, &f);

        return 2;
    }

    if (t > pf->mtime) {
        /* exist and newer, update */
        pf->mtime = t;
        pf->mod   = 1;

        return 1;
    }

    return 0;
}

db_t* db_open(const char* file)
{
    xhash_t* db = xhash_new(-1, sizeof(file_info_t),
                    _hash_cb, _equal_cb, _destr_cb);
    file_info_t f;
#ifdef _WIN32
    int fd = open(file, O_RDONLY | O_BINARY);
#else
    int fd = open(file, O_RDONLY);
#endif

    if (fd < 0) return db;

    // read and check header, TODO

    do {
        if (read(fd, &f.len, sizeof(f.len)) != sizeof(f.len)) {
            break;
        }
        if (f.len > 1024) {
            fprintf(stderr, "wrong data in db file, break.\n");
            break;
        }

        f.path = malloc(f.len);

        if (read(fd, f.path, f.len) != f.len ||
                f.path[f.len - 1] != '\0' ||
                read(fd, &f.mtime, sizeof(f.mtime)) != sizeof(f.mtime)) {
            fprintf(stderr, "broken db file, break.\n");
            free(f.path);
            break;
        }

        f.mod = 0;
        f.fpos = (int)lseek(fd, 0, SEEK_CUR) - sizeof(f.mtime);

        xhash_put(db, &f);
    } while (1);

    return db;
}

void db_close(const char* file, db_t* db)
{
#ifdef _WIN32
    int fd = open(file, O_WRONLY | O_CREAT | O_BINARY, 0644);
#else
    int fd = open(file, O_WRONLY | O_CREAT, 0644);
#endif
    int need_seek = 1;

    if (fd < 0) {
        fprintf(stderr, "open db file [%s] failed: %s.\n",
            file, strerror(errno));
        return;
    }

    // write header, TODO

    /* -------------------------------------------- */
    /* |  len  |  path (null-terminate)  |  mtime | */
    /* -------------------------------------------- */
    /* |   4   |          (len)          |    8   | bytes */
    /* -------------------------------------------- */

    for (xhash_iter_t iter = xhash_begin(db);
            iter != xhash_end(db); iter = xhash_iter_next(db, iter)) {

        file_info_t* f = xhash_iter_data(iter);

        /* write only when 'f->mod' is true */
        if (!f->mod) continue;

        if (f->fpos > 0) {
            /* db file already contains this file, just update mtime */
            if (lseek(fd, f->fpos, SEEK_SET) < 0) {
                fprintf(stderr, "seek to %d failed: %s.\n",
                    f->fpos, strerror(errno));
                break;
            }
            if (write(fd , &f->mtime, sizeof(f->mtime)) != sizeof(f->mtime)) {
                fprintf(stderr, "write db file failed.\n");
                break;
            }
            need_seek = 1;
        } else {
            if (need_seek) {
                if (lseek(fd, 0, SEEK_END) < 0) {
                    fprintf(stderr, "seek to end failed: %s.\n", strerror(errno));
                    break;
                }
                need_seek = 0;
            }
            if (write(fd, &f->len, sizeof(f->len)) != sizeof(f->len) ||
                    write(fd, f->path, f->len) != f->len ||
                    write(fd, &f->mtime, sizeof(f->mtime)) != sizeof(f->mtime)) {
                fprintf(stderr, "write db file failed.\n");
                break;
            }
        }
    }

    close(fd);
    xhash_free(db);
}