#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "md5.h"
#include "config.h"
#include "db.h"
#include "ssh_session.h"
#include "match.h"
#include "version.h"

// #define DONT_UPLOAD
#define DEFAULT_CONFIG_FILE "sshul.json"

static struct {
    config_t*   cfg;
    ssh_t*      scp;
    sftp_t*     sftp;
    db_t*       db;
    match_cb_t  mcb;
} G;

static void _do_list_all(const char* file,
        int mode, uint64_t mtime, uint64_t size)
{
    fprintf(stdout, "%s\n", file);
}

static void _do_list_upload(const char* file,
        int mode, uint64_t mtime, uint64_t size)
{
    if (db_check(G.db, file, mtime))
        fprintf(stdout, "%s\n", file);
}

static void _do_init(const char* file,
        int mode, uint64_t mtime, uint64_t size)
{
    if (db_check(G.db, file, mtime)) {
        fprintf(stdout, "init [%s].\n", file);
        db_update(G.db, file, mtime);
    }
}

static void _do_upload(const char* file,
        int mode, uint64_t mtime, uint64_t size)
{
    xstr_t* l = &G.cfg->local_path;
    xstr_t* r = &G.cfg->remote_path;
    unsigned ol = xstr_size(l);
    unsigned or = xstr_size(r);

    if (db_check(G.db, file, mtime)) {
        fprintf(stdout, "upload [%s].\n", file);

#ifndef DONT_UPLOAD
        if (!G.scp) {
            /* build ssh session */
            G.scp = ssh_session_open(
                xstr_data(&G.cfg->remote_host), G.cfg->remote_port,
                xstr_data(&G.cfg->remote_user), xstr_data(&G.cfg->remote_passwd));
            if (!G.scp) {
                fprintf(stderr, "ssh_session_open failed.\n");
                return;
            }
            /* create sftp session */
            if (G.cfg->use_sftp) {
                G.sftp = sftp_session_new(G.scp);
                if (!G.sftp) {
                    fprintf(stderr, "sftp_session_new failed, use scp instead.\n");
                }
            }
        }

        xstr_push_back(l, '/');
        xstr_append(l, file, -1);
        xstr_push_back(r, '/');
        xstr_append(r, file, -1);

        if ((G.sftp ? sftp_send_file(G.sftp, xstr_data(l), xstr_data(r), mode, size)
                : scp_send_file(G.scp, xstr_data(l), xstr_data(r), mode, size)) != -1) {
#endif
            db_update(G.db, file, mtime);
#ifndef DONT_UPLOAD
        }

        xstr_erase(l, ol, -1);
        xstr_erase(r, or, -1);
#endif
    }
}

#define CFG_TEMPLATE                            \
    "[{\n"                                      \
    "    \"remote_host\": \"192.168.1.1\",\n"   \
    "    \"remote_port\": 22,\n"                \
    "    \"remote_user\": \"root\",\n"          \
    "    \"remote_passwd\": \"123456\",\n"      \
    "    \"remote_path\": \"/tmp\",\n"          \
    "    \"local_path\": \".\",\n"              \
    "    \"local_files\": [ \"**\" ]\n"         \
    "}]\n"

static int generate_config_file(const char* file)
{
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd < 0) {
        fprintf(stderr, "open file [%s] failed: %s.\n",
            file, strerror(errno));
        return 1;
    }

    fprintf(stdout, "write template config file to [%s].\n", file);

    if (write(fd, CFG_TEMPLATE, sizeof(CFG_TEMPLATE) - 1)
            != sizeof(CFG_TEMPLATE) - 1) {
        fprintf(stderr, "write failed: %s.\n", strerror(errno));
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
#undef CONFIG_TEMPLATE

static int cd_to_filedir(const char* file)
{
    char* p;

#ifdef _WIN32
    if ((p = strrchr(file, '/')) ||
        (p = strrchr(file, '\\'))) {
        char* dir = _strdup(file);
#else
    if ((p = strrchr(file, '/'))) {
        char* dir = strdup(file);
#endif
        dir[p - file] = '\0';

        if (chdir(dir) != 0) {
            fprintf(stderr, "can't cd to [%s].\n", dir);
            free(dir);
            return 1;
        }

        free(dir);
    }

    return 0;
}

#define DB_FILE_PREFIX  "sshul-db-"
static void generate_db_file_name(xstr_t* s, config_t* o)
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

    if (xstr_empty(&o->db_path)) {
        /* get system temp path */
#ifdef _WIN32
        char name[256];
        int nlen = GetTempPathA(sizeof(name), name);

        if (nlen == 0) {
            strcpy(name, ".");
            nlen = 1;
        } else {
            char* p;
            for (p = name; p[0]; ++p) {
                if (p[0] == '\\') p[0] = '/';
            }
            if (name[nlen - 1] == '/') {
                name[--nlen] = '\0';
            }
        }
        xstr_assign(s, name, nlen);
#else
        xstr_assign(s, "/tmp", 4);
#endif
    } else {
        xstr_assign_str(s, &o->db_path);
    }

    xstr_push_back(s, '/');
    xstr_append(s, DB_FILE_PREFIX, sizeof(DB_FILE_PREFIX) - 1);
    xstr_append(s, str, sizeof(str));
}
#undef DB_FILE_PREFIX

static void process_config(config_t* cfg, int rmdb)
{
    xstr_t buf;
    xlist_iter_t it;

    if (cfg->disable) return;

    xstr_init(&buf, 64);
    generate_db_file_name(&buf, cfg);

    if (rmdb) {
        /* remove db file */
        if (unlink(xstr_data(&buf)) != 0)
            fprintf(stderr, "rm [%s] failed.\n", xstr_data(&buf));
        else
            fprintf(stderr, "rm [%s] done.\n", xstr_data(&buf));
        goto end;
    }

    fprintf(stderr, "[%s@%s:%s]\n", xstr_data(&cfg->remote_user),
        xstr_data(&cfg->remote_host), xstr_data(&cfg->remote_path));

    G.db = db_open(xstr_data(&buf));
    G.cfg = cfg;

    for (it = xlist_begin(&cfg->local_files);
            it != xlist_end(&cfg->local_files); it = xlist_iter_next(it)) {
        match_files(xstr_data(&cfg->local_path),
            xstr_data((xstr_t*)xlist_iter_value(it)), G.mcb);
    }

    db_close(xstr_data(&buf), G.db);

    if (G.sftp) {
        sftp_session_free(G.sftp);
        G.sftp = NULL;
    }
    if (G.scp) {
        ssh_session_close(G.scp);
        G.scp = NULL;
    }
end:
    xstr_destroy(&buf);
}

static void usage(const char* s)
{
    fprintf(stderr, "sshul v%s, usage: %s [option]...\n"
        "[option]:\n"
        "  -l      - list the files to be uploaded.\n"
        "  -a      - list all matched file.\n"
        "  -u      - upload the files to be uploaded.\n"
        "  -i      - initialize the db file.\n"
        "  -c      - remove the db file.\n"
        "  -t      - generate template config file.\n"
        "  -f file - set config file. (default: " DEFAULT_CONFIG_FILE ")\n"
        "  -v      - show version message.\n"
        "  -h      - show this help message.\n", VERSION_STRING, s);
    fprintf(stderr, "[config]:\n"
        "  remote_host   - ssh server ip address.\n"
        "  remote_port   - ssh server port. (default: 22)\n"
        "  remote_user   - ssh user name.\n"
        "  remote_passwd - ssh user password.\n"
        "  remote_path   - the remote path which upload files to.\n"
        "  local_path    - the local path which local files in.\n"
        "  local_files   - the files (pattern) to be uploaded.\n"
        "  db_path       - the local path which save db file to. (default: system temporary path)\n"
        "  use_sftp      - use sftp or scp. (default: true)\n"
        "  disable       - disable this config. (default: false)\n");
    fprintf(stderr, "[pattern] example:\n"
        "  dir/*.[ch]  dir/*/file.c  dir/**  di?/*.c dir/*.[a-z]\n");
}

int main(int argc, char** argv)
{
    const char* file = DEFAULT_CONFIG_FILE;
    int flag = 0;
    int i;

    G.mcb = NULL;

    for (i = 1; i < argc; ++i) {
        char opt;
        char* arg;

        if (argv[i][0] != '-' || argv[i][1] == '\0') {
            fprintf(stderr, "wrong args [%s].\n", argv[i]);
            usage(argv[0]);
            return 1;
        }

        opt = argv[i][1];

        switch (opt) {
        case 'l': G.mcb = _do_list_upload; continue;
        case 'u': G.mcb = _do_upload; continue;
        case 'a': G.mcb = _do_list_all; continue;
        case 'i': G.mcb = _do_init; continue;
        case 'c': flag = 1; continue;
        case 't':
            return generate_config_file(file);
        case 'v':
            fprintf(stderr, "sshul v" VERSION_STRING "\n");
            return 1;
        case 'h':
            usage(argv[0]);
            return 1;
        }

        arg = argv[i][2] ? argv[i] + 2 : (++i < argc ? argv[i] : NULL);

        if (arg) switch (opt) {
        case 'f': file = arg; continue;
        }

        fprintf(stderr, "invalid option [-%c].\n", opt);
        usage(argv[0]);
        return 1;
    }

    if (G.mcb || flag) {
        xlist_t cfgs;
        xlist_iter_t iter;
#ifdef _WIN32
        /* init winsocks */
        WSADATA wsData;
        WSAStartup(MAKEWORD(2, 2), &wsData);
#endif

        /* load configs to 'cfgs' from 'file' */
        if (configs_load(&cfgs, file) != 0) {
            fprintf(stderr, "config load failed, exit.\n");
            return 1;
        }

        /* cd to config file's path */
        if (cd_to_filedir(file) != 0)
            return 1;

        iter = xlist_begin(&cfgs);

        while (iter != xlist_end(&cfgs)) {
            process_config(xlist_iter_value(iter), flag);
            iter = xlist_iter_next(iter);
        }

        configs_destroy(&cfgs);
        return 0;
    }

    usage(argv[0]);
    return 1;
}