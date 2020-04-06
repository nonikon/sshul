#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <glob.h>
#include <errno.h>
#include <sys/stat.h>

#include "md5.h"
#include "config.h"
#include "db.h"
#include "ssh_session.h"

// #define DONT_UPLOAD
#define DEFAULT_CONFIG_FILE "sshul.json"

enum {
    ACT_NONE,
    ACT_GEN_CONFIG,
    ACT_LIST_ALL,
    ACT_LIST_UPLOAD,
    ACT_DO_INIT,
    ACT_DO_CLEAN,
    ACT_DO_UPLOAD,
};

/* expand files which contains wildcard and so on */
static void expand_files(xlist_t* files)
{
    xstr_t file;
    xlist_iter_t iter = xlist_begin(files);
    int n = xlist_size(files);
    int i;
    glob_t globbuf;

    while (n--) {
        glob(xstr_data((xstr_t*)xlist_iter_value(iter)),
            GLOB_NOSORT, NULL, &globbuf);

        for (i = 0; i < globbuf.gl_pathc; ++i) {
            xstr_init_with(&file, globbuf.gl_pathv[i], -1);
            xlist_push_back(files, &file);
        }
    
        globfree(&globbuf);

        iter = xlist_erase(files, iter);
    }
}

static int process_files(options_t* o, xhash_t* db, int act)
{
#ifndef DONT_UPLOAD
    ssh_session_t* ssh = NULL;
    sftp_session_t* sftp = NULL;
    xstr_t* remote_f = NULL;
#endif
    struct stat s;

#ifndef DONT_UPLOAD
    if (act == ACT_DO_UPLOAD) {
        /* build ssh session */
        ssh = ssh_session_open(
            xstr_data(&o->remote_host), o->remote_port,
            xstr_data(&o->remote_user), xstr_data(&o->remote_passwd));

        if (!ssh) {
            fprintf(stderr, "build ssh session failed, exit.\n");
            return 1;
        }

        if (o->use_sftp) {
            sftp = sftp_session_new(ssh);

            if (!sftp) {
                fprintf(stderr, "sftp_session_new failed, use scp instead.\n");
            }
        }

        remote_f = xstr_new(128);
        xstr_append_str(remote_f, &o->remote_path);
        xstr_push_back(remote_f, '/');
    }
#endif

    for (xlist_iter_t iter = xlist_begin(&o->local_files);
            iter != xlist_end(&o->local_files); iter = xlist_iter_next(iter)) {

        xstr_t* local_f = xlist_iter_value(iter);

        /* no need to check 'stat' return value */
        stat(xstr_data(local_f), &s);

        /* skip a directory */
        if (S_ISDIR(s.st_mode)) continue;

        switch (act)
        {
        case ACT_LIST_ALL:
            fprintf(stdout, "%s\n", xstr_data(local_f));
            break;
        case ACT_LIST_UPLOAD:
            if (!db_check(db, xstr_data(local_f), s.st_mtime))
                break;
            fprintf(stdout, "%s\n", xstr_data(local_f));
            break;
        case ACT_DO_INIT:
            if (!db_check(db, xstr_data(local_f), s.st_mtime))
                break;
            fprintf(stdout, "init [%s].\n", xstr_data(local_f));
            db_update(db, xstr_data(local_f), s.st_mtime);
            break;
        case ACT_DO_UPLOAD:
            if (!db_check(db, xstr_data(local_f), s.st_mtime))
                break;
            fprintf(stdout, "upload [%s].\n", xstr_data(local_f));
#ifndef DONT_UPLOAD
            xstr_assign_str_at(remote_f,
                xstr_size(&o->remote_path) + 1, local_f);

            if (-1 != (sftp
                ? sftp_send_file(sftp,
                    xstr_data(local_f), xstr_data(remote_f))
                : scp_send_file(ssh,
                    xstr_data(local_f), xstr_data(remote_f)))) {
#endif
                db_update(db, xstr_data(local_f), s.st_mtime);
#ifndef DONT_UPLOAD
            }
#endif
            break;
        default:
            /* never reach here */
            fprintf(stderr, "fatal error.\n");
            break;
        }
    }

#ifndef DONT_UPLOAD
    xstr_free(remote_f);
    sftp_session_free(sftp);
    ssh_session_close(ssh);
#endif
    return 0;
}

#define CONFIG_TEMPLATE                                 \
    "{\n"                                               \
    "    \"remote_host\": \"192.168.1.1\",\n"           \
    "    \"remote_port\": 22,\n"                        \
    "    \"remote_user\": \"root\",\n"                  \
    "    \"remote_passwd\": \"123456\",\n"              \
    "    \"remote_path\": \"/tmp/test\",\n"             \
    "    \"local_path\": \".\",\n"                      \
    "    \"local_files\": [\n"                          \
    "        \"*.c\", \"*.h\", \".vscode/*.json\"\n"    \
    "    ],\n"                                          \
    "    \"db_path\": \"/tmp\",\n"                      \
    "    \"use_sftp\": true\n"                          \
    "}\n"
static int generate_config_file(const char* file)
{
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd < 0) {
        fprintf(stderr, "failed open file [%s]: %s.\n",
            file, strerror(errno));
        return 1;
    }

    fprintf(stdout, "write template config file to [%s].\n", file);

    if (write(fd, CONFIG_TEMPLATE, sizeof(CONFIG_TEMPLATE) - 1)
            != sizeof(CONFIG_TEMPLATE) - 1) {
        fprintf(stderr, "write failed: %s.\n", strerror(errno));
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
#undef CONFIG_TEMPLATE

#define DB_FILE_PREFIX  "sshul-db-"
static void generate_db_file_name(xstr_t* s, options_t* o)
{
    md5_t ctx;
    uint8_t digest[16];
    char str[32];
    int i;

    md5_init(&ctx);
    md5_update(&ctx, xstr_data(&o->remote_host),
        xstr_size(&o->remote_host) + 1);
    md5_update(&ctx, &o->remote_port, sizeof(o->remote_port));
    md5_update(&ctx, xstr_data(&o->remote_path),
        xstr_size(&o->remote_path) + 1);
    md5_update(&ctx, xstr_data(&o->local_path),
        xstr_size(&o->local_path) + 1);
    md5_final(digest, &ctx);

    for (i = 0; i < 16; ++i) {
        str[i * 2] = "0123456789abcdef"[digest[i] >> 4];
        str[i * 2 + 1] = "0123456789abcdef"[digest[i] & 15];
    }

    xstr_assign_str(s, &o->db_path);
    xstr_push_back(s, '/');
    xstr_append(s, DB_FILE_PREFIX, sizeof(DB_FILE_PREFIX) - 1);
    xstr_append(s, str, sizeof(str));
}
#undef DB_FILE_PREFIX

static int usage(const char* name)
{
    fprintf(stderr, "usage: %s [option]\n"
        "    -l      - list the files to be uploaded.\n"
        "    -a      - list all matched file.\n"
        "    -u      - upload the files to be uploaded.\n"
        "    -i      - initialize the db file.\n"
        "    -c      - remove the db file.\n"
        "    -t      - generate template config file.\n"
        "    -f file - set config file. (default: " DEFAULT_CONFIG_FILE ")\n"
        "    -h      - show this help message.\n", name);

    return 1;
}

extern char* optarg;

int main(int argc, char** argv)
{
    char* cfg_file = DEFAULT_CONFIG_FILE;
    int act = ACT_NONE;
    int r;

    while ((r = getopt(argc, argv, "lauictf:h")) != -1) {
        switch (r)
        {
        case 'l': act = ACT_LIST_UPLOAD ; break;
        case 'a': act = ACT_LIST_ALL    ; break;
        case 'u': act = ACT_DO_UPLOAD   ; break;
        case 'i': act = ACT_DO_INIT     ; break;
        case 'c': act = ACT_DO_CLEAN    ; break;
        case 't': act = ACT_GEN_CONFIG  ; break;
        case 'f': cfg_file = optarg     ; break;
        case 'h':
        default:
            return usage(argv[0]);
        }
    }

    if (act == ACT_NONE) {
        return usage(argv[0]);
    }

    if (act == ACT_GEN_CONFIG) {
        return generate_config_file(cfg_file);
    }

    options_t* opts = config_load(cfg_file);

    if (!opts) {
        fprintf(stderr, "config load failed, exit.\n");
        return 1;
    }

    xstr_t* buf = xstr_new(64);
    char* p = strrchr(cfg_file, '/');
    xhash_t* db;

    r = 1;
    /* cd to config file's path */
    if (p) {
        xstr_assign(buf, cfg_file, p - cfg_file);

        if (chdir(xstr_data(buf)) != 0) {
            fprintf(stderr, "can't cd to [%s].\n", xstr_data(buf));
            goto end;
        }
    }

    /* cd to opts->local_path */
    if (chdir(xstr_data(&opts->local_path)) != 0) {
        fprintf(stderr, "can't cd to [%s].\n", xstr_data(&opts->local_path));
        goto end;
    }

    generate_db_file_name(buf, opts);

    if (act == ACT_DO_CLEAN) {
        r = unlink(xstr_data(buf)) < 0 ? 1 : 0;
        if (r)
            fprintf(stderr, "rm [%s] failed.\n", xstr_data(buf));
        else
            fprintf(stderr, "rm [%s] done.\n", xstr_data(buf));
        goto end;
    }

    db = db_open(xstr_data(buf));

    expand_files(&opts->local_files);
    r = process_files(opts, db, act);

    db_close(xstr_data(buf), db);
end:
    xstr_free(buf);
    config_free(opts);
    return r;
}