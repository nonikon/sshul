#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <errno.h>
#include <sys/stat.h>

#include "config.h"
#include "scp_session.h"

#define DEFAULT_CONFIG_FILE "scpul.json"

#define FLAG_LIST_ALL       0x01 // list all matched file
#define FLAG_LIST_UPLOAD    0x02 // list the files to be uploaded
#define FLAG_DO_INIT        0x04 // create all stat files
#define FLAG_DO_CLEAN       0x08 // remove all stat files
#define FLAG_DO_UPLOAD      0x10 // upload the files to be uploaded

static int create_file(const char* path)
{
    int r = open(path, O_RDWR | O_CREAT, 0644);

    if (r != -1) {
        close(r); return 0;
    }
    return -1;
}

static int create_file_r(const char* path)
{
    const char* p;
    xstr_t* name;
    int r = create_file(path);

    if (r != 0 && errno != ENOENT) {
        return -1;
    }

    name = xstr_new(256);
    // create dirs
    while (p = strchr(path, '/')) {
        xstr_append(name, path, ++p - path);
        r = mkdir(xstr_data(name), 0755);
        path = p;
    }
    // create file
    if (*path) { // 'path' not end with '/'
        xstr_append(name, path, -1);
        r = create_file(xstr_data(name));
    }
    xstr_free(name);

    return r;
}

static void expand_files(xlist_t* files)
{
    xstr_t* file;
    xlist_iter_t iter = xlist_begin(files);
    int n = xlist_size(files);
    int i;
    glob_t globbuf;

    while (n--) {
        file = *(xstr_t**)xlist_iter_value(iter);

        glob(xstr_data(file), GLOB_NOSORT, NULL, &globbuf);
        for (i = 0; i < globbuf.gl_pathc; ++i) {
            file = xstr_new_with(globbuf.gl_pathv[i], -1);
            xlist_push_back(files, &file);
        }
        globfree(&globbuf);

        iter = xlist_erase(files, iter);
    }
}

static void proccess_files(options_t* o, int flag)
{
    xlist_iter_t iter = xlist_begin(o->local_files);
    scp_session_t* session = NULL;
    struct stat curr;
    struct stat befo;
    xstr_t* local_f;
    xstr_t* remote_f;
    xstr_t* stat_f;

    if (flag & FLAG_DO_UPLOAD) {
        session = scp_session_open(
            xstr_data(o->remote_host), o->remote_port,
            xstr_data(o->remote_user), xstr_data(o->remote_passwd));
        if (!session) return;
    }

    remote_f = xstr_new(128);
    xstr_append(remote_f, xstr_data(o->remote_path), xstr_size(o->remote_path));
    xstr_push_back(remote_f, '/');

    stat_f = xstr_new(128);
    xstr_append(stat_f, xstr_data(o->stats_path), xstr_size(o->stats_path));
    xstr_push_back(stat_f, '/');

    for (; xlist_iter_valid(o->local_files, iter); iter = xlist_iter_next(iter)) {
        local_f = *(xstr_t**)xlist_iter_value(iter);

        xstr_append_at(remote_f,
            xstr_size(o->remote_path) + 1, xstr_data(local_f), xstr_size(local_f));
        xstr_append_at(stat_f,
            xstr_size(o->stats_path) + 1, xstr_data(local_f), xstr_size(local_f));

        stat(xstr_data(local_f), &curr); // no need to check it's return value
        if (S_ISDIR(curr.st_mode)) {
            continue; // skip a directory
        }

        if (flag & FLAG_LIST_ALL) {
            fprintf(stdout, "%s\n", xstr_data(local_f));
        }
        else if (flag & FLAG_DO_CLEAN) {
            fprintf(stdout, "rmstat [%s].\n", xstr_data(stat_f));
            unlink(xstr_data(stat_f));
        }
        else if (stat(xstr_data(stat_f), &befo) != 0) {
            if (flag & FLAG_LIST_UPLOAD) {
                fprintf(stdout, "%s\n", xstr_data(local_f));
            }
            else if (flag & FLAG_DO_INIT) {
                fprintf(stdout, "stat [%s].\n", xstr_data(local_f));
                create_file_r(xstr_data(stat_f));
            }
            else if (flag & FLAG_DO_UPLOAD) {
                fprintf(stdout, "upload [%s].\n", xstr_data(local_f));
                if (scp_session_send_file(session,
                        xstr_data(local_f), xstr_data(remote_f)) != -1) {
                    create_file_r(xstr_data(stat_f));
                }
            }
        }
        else if (curr.st_mtime > befo.st_mtime) {
            if (flag & FLAG_LIST_UPLOAD) {
                fprintf(stdout, "%s\n", xstr_data(local_f));
            }
            else if (flag & FLAG_DO_INIT) {
                fprintf(stdout, "stat [%s].\n", xstr_data(local_f));
                utime(xstr_data(stat_f), NULL);
            }
            else if (flag & FLAG_DO_UPLOAD) {
                fprintf(stdout, "upload [%s].\n", xstr_data(local_f));
                if (scp_session_send_file(session,
                        xstr_data(local_f), xstr_data(remote_f)) != -1) {
                    utime(xstr_data(stat_f), NULL);
                }
            }
        }
    }

    xstr_free(remote_f);
    xstr_free(stat_f);
    scp_session_close(session);
}

static void usage(const char* name)
{
    fprintf(stderr, "usage: %s [option]\n"
        "\t-l      - list the files to be uploaded.\n"
        "\t-a      - list all matched file.\n"
        "\t-u      - upload the files to be uploaded.\n"
        "\t-i      - create all stat files.\n"
        "\t-c      - remove all stat files.\n"
        "\t-f file - set config file. (default: " DEFAULT_CONFIG_FILE ")\n", name);
}

extern char* optarg;

int main(int argc, char** argv)
{
    options_t* opts;
    const char* cfg_file = DEFAULT_CONFIG_FILE;
    int flag = 0;
    int c;

    while ((c = getopt(argc, argv, "lauicf:")) != -1) {
        switch (c)
        {
        case 'l': flag = FLAG_LIST_UPLOAD; break;
        case 'a': flag = FLAG_LIST_ALL   ; break;
        case 'u': flag = FLAG_DO_UPLOAD  ; break;
        case 'i': flag = FLAG_DO_INIT    ; break;
        case 'c': flag = FLAG_DO_CLEAN   ; break;
        case 'f': cfg_file = optarg      ; break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    if (!flag) {
        usage(argv[0]);
        return 1;
    }

    opts = config_load(cfg_file);
    if (!opts) {
        return 1;
    }

    // cd to config file's path
    char* p = strrchr(cfg_file, '/');
    if (p) {
        char* buf = malloc(p - cfg_file + 1);
        memcpy(buf, cfg_file, p - cfg_file);
        buf[p - cfg_file] = '\0';
        if (chdir(buf) != 0) {
            fprintf(stderr, "can't cd to [%s].\n", buf);
        }
        free(buf);
    }
    // cd to opts->local_path
    if (chdir(xstr_data(opts->local_path)) != 0) {
        fprintf(stderr, "can't cd to [%s].\n", xstr_data(opts->local_path));
    }

    expand_files(opts->local_files);
    proccess_files(opts, flag);

    config_free(opts);
    return 0;
}