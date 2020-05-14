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

#define VERSION_STRING      "1.0.0"

// #define DONT_UPLOAD
#define DEFAULT_CONFIG_FILE "sshul.json"

enum {
    ACT_NONE,
    ACT_SHOW_VER,
    ACT_GEN_CONFIG,
    ACT_LIST_ALL,
    ACT_LIST_UPLOAD,
    ACT_DO_INIT,
    ACT_DO_CLEAN,
    ACT_DO_UPLOAD,
};

static struct {
    char*       cfg;
    options_t   opts;
    int         act;
    ssh_t*      scp;
    sftp_t*     sftp;
    db_t*       db;
} G;

static void process_file(const char* local, const char* remote,
        time_t mtime, int mode, unsigned long size)
{
    if (G.act == ACT_LIST_ALL) {
        fprintf(stdout, "%s\n", local);
        return;
    }

    if (!db_check(G.db, local, mtime)) {
        /* need do nothing, return */
        return;
    }

    if (G.act == ACT_LIST_UPLOAD) {
        fprintf(stdout, "%s\n", local);
        return;
    }
    if (G.act == ACT_DO_UPLOAD) {
        fprintf(stdout, "upload [%s].\n", local);
#ifndef DONT_UPLOAD
        if ((G.sftp ? sftp_send_file(G.sftp, local, remote, mode, size)
                : scp_send_file(G.scp, local, remote, mode, size)) != -1) {
#endif
            db_update(G.db, local, mtime);
#ifndef DONT_UPLOAD
        }
#endif
        return;
    }
    if (G.act == ACT_DO_INIT) {
        fprintf(stdout, "init [%s].\n", local);
        db_update(G.db, local, mtime);
        return;
    }

    /* never reach here */
    fprintf(stderr, "fatal error.\n");
}

static void process_files()
{
    struct stat s;
    glob_t globbuf;
    xstr_t remotef;

    xstr_init(&remotef, 128);
    xstr_append_str(&remotef, &G.opts.remote_path);
    xstr_push_back(&remotef, '/');

    for (xlist_iter_t it = xlist_begin(&G.opts.local_files);
            it != xlist_end(&G.opts.local_files); it = xlist_iter_next(it)) {

        /* TODO, win32 support */
        glob(xstr_data((xstr_t*)xlist_iter_value(it)),
                GLOB_NOSORT, NULL, &globbuf);

        for (int i = 0; i < globbuf.gl_pathc; ++i) {
            /* no need to check 'stat' return value */
            stat(globbuf.gl_pathv[i], &s);
            /* skip a directory */
            if (S_ISDIR(s.st_mode)) {
                continue;
            }
            xstr_assign_at(&remotef, xstr_size(&G.opts.remote_path) + 1,
                globbuf.gl_pathv[i], -1);
            process_file(globbuf.gl_pathv[i], xstr_data(&remotef),
                s.st_mtime, s.st_mode, s.st_size);
        }

        globfree(&globbuf);
    }

    xstr_destroy(&remotef);
}

#define CONFIG_TEMPLATE                                 \
    "{\n"                                               \
    "    \"remote_host\": \"192.168.1.1\",\n"           \
    "    \"remote_port\": 22,\n"                        \
    "    \"remote_user\": \"root\",\n"                  \
    "    \"remote_passwd\": \"123456\",\n"              \
    "    \"remote_path\": \"/tmp\",\n"                  \
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

static int parse_args(int argc, char** argv)
{
    G.cfg = DEFAULT_CONFIG_FILE;
    G.act = ACT_NONE;

    /* same as what linux 'getopt' do */
    for (int i = 1; i < argc; ++i) {
        char* arg = argv[i];

        if (arg[0] != '-') {
            /* arg only */
            return 1;
        } else {
            ++arg;
            while (arg[0]) {
                /* opt with an arg */
                if (arg[0] == 'f') {
                    if (arg[1]) {
                        arg = arg + 1;
                    } else if (++i < argc) {
                        arg = argv[i];
                    } else {
                        fprintf(stderr, "Broken args [-%c].\n", arg[0]);
                        return 1;
                    }
                    G.cfg = arg;
                    break;
                }
                /* opt only */
                if (arg[0] == 'l') {
                    G.act = ACT_LIST_UPLOAD;
                } else if (arg[0] == 'u') {
                    G.act = ACT_DO_UPLOAD;
                } else if (arg[0] == 'a') {
                    G.act = ACT_LIST_ALL;
                } else if (arg[0] == 'i') {
                    G.act = ACT_DO_INIT;
                } else if (arg[0] == 'c') {
                    G.act = ACT_DO_CLEAN;
                } else if (arg[0] == 't') {
                    G.act = ACT_GEN_CONFIG;
                } else if (arg[0] == 'v') {
                    G.act = ACT_SHOW_VER;
                } else if (arg[0] == 'h') {
                    G.act = ACT_NONE;
                } else {
                    fprintf(stderr, "Unknown option [-%c].\n", arg[0]);
                    return 1;
                }
                ++arg;
            }
        }
    }

    return 0;
}

int main(int argc, char** argv)
{
    if (parse_args(argc, argv) != 0 || G.act == ACT_NONE) {
        fprintf(stderr, "Usage: %s [option]\n"
            "    -l      - list the files to be uploaded.\n"
            "    -a      - list all matched file.\n"
            "    -u      - upload the files to be uploaded.\n"
            "    -i      - initialize the db file.\n"
            "    -c      - remove the db file.\n"
            "    -t      - generate template config file.\n"
            "    -f file - set config file. (default: " DEFAULT_CONFIG_FILE ")\n"
            "    -v      - show version message.\n"
            "    -h      - show this help message.\n", argv[0]);
        return 1;
    }

    if (G.act == ACT_SHOW_VER) {
        fprintf(stderr, "sshul v" VERSION_STRING ".\n");
        return 1;
    }

    if (G.act == ACT_GEN_CONFIG) {
        return generate_config_file(G.cfg);
    }

    if (config_load(&G.opts, G.cfg) != 0) {
        fprintf(stderr, "config load failed, exit.\n");
        return 1;
    }

    xstr_t* buf = xstr_new(64);
    char* p = strrchr(G.cfg, '/');
    int rc = 1;

    /* cd to config file's path */
    if (p) {
        xstr_assign(buf, G.cfg, p - G.cfg);

        if (chdir(xstr_data(buf)) != 0) {
            fprintf(stderr, "can't cd to [%s].\n", xstr_data(buf));
            goto end;
        }
    }
    /* cd to opts->local_path */
    if (chdir(xstr_data(&G.opts.local_path)) != 0) {
        fprintf(stderr, "can't cd to [%s].\n", xstr_data(&G.opts.local_path));
        goto end;
    }

    generate_db_file_name(buf, &G.opts);

    if (G.act == ACT_DO_CLEAN) {
        /* remove db file */
        rc = unlink(xstr_data(buf)) < 0 ? 1 : 0;
        if (rc)
            fprintf(stderr, "rm [%s] failed.\n", xstr_data(buf));
        else
            fprintf(stderr, "rm [%s] done.\n", xstr_data(buf));
        goto end;
    }

#ifndef DONT_UPLOAD
    if (G.act == ACT_DO_UPLOAD) {
#ifdef _WIN32
        /* init winsocks */
        WSADATA wsData;
        WSAStartup(MAKEWORD(2, 2), &wsData);
#endif
        /* build ssh session */
        G.scp = ssh_session_open(
            xstr_data(&G.opts.remote_host), G.opts.remote_port,
            xstr_data(&G.opts.remote_user), xstr_data(&G.opts.remote_passwd));
        if (!G.scp) {
            fprintf(stderr, "build ssh session failed.\n");
            goto end;
        }
        /* create sftp session */
        if (G.opts.use_sftp) {
            G.sftp = sftp_session_new(G.scp);
            if (!G.sftp) {
                fprintf(stderr, "sftp_session_new failed, use scp instead.\n");
            }
        }
    }
#endif

    rc = 0;

    G.db = db_open(xstr_data(buf));
    process_files();
    db_close(xstr_data(buf), G.db);

    sftp_session_free(G.sftp);
    ssh_session_close(G.scp);
end:
    xstr_free(buf);
    config_destroy(&G.opts);
    return rc;
}