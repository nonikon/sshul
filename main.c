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
#include "ssh_session.h"

#define DEFAULT_CONFIG_FILE "sshul.json"

#define FLAG_LIST_ALL       0x01 // list all matched file
#define FLAG_LIST_UPLOAD    0x02 // list the files to be uploaded
#define FLAG_DO_INIT        0x04 // create all stat files
#define FLAG_DO_CLEAN       0x08 // remove all stat files
#define FLAG_DO_UPLOAD      0x10 // upload the files to be uploaded
#define FLAG_GEN_CONFIG     0x20 // generate template config file

/* create an empty file */
static int create_file(const char* path)
{
    int r = open(path, O_RDWR | O_CREAT, 0644);

    if (r < 0) return -1;

    close(r);
    return 0;
}

/* create an empty file and missing directory */
static int create_file_r(const char* path)
{
    xstr_t name;
    const char* p;
    int r = create_file(path);

    if (r != 0 && errno != ENOENT) {
        return -1;
    }

    xstr_init(&name, 128);

    /* create missing dirs */
    while ((p = strchr(path, '/')) != NULL) {
        xstr_append(&name, path, ++p - path);
        r = mkdir(xstr_data(&name), 0755);
        path = p;
    }

    /* create file */
    if (*path) { /* 'path' not end with '/' */
        xstr_append(&name, path, -1);
        r = create_file(xstr_data(&name));
    }

    xstr_destroy(&name);

    return r;
}

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

static void proccess_files(options_t* o, int flag)
{
    ssh_session_t* ssh = NULL;
    sftp_session_t* sftp = NULL;
    xlist_iter_t iter;

    xstr_t* local_f;
    xstr_t* remote_f;
    xstr_t* stat_f;

    struct stat curr;
    struct stat befo;

    if (flag & FLAG_DO_UPLOAD) {
        ssh = ssh_session_open(
            xstr_data(&o->remote_host), o->remote_port,
            xstr_data(&o->remote_user), xstr_data(&o->remote_passwd));

        if (!ssh) return;

        if (o->use_sftp) {
            sftp = sftp_session_new(ssh);
            if (!sftp) {
                fprintf(stderr, "sftp_session_new failed, use scp instead.\n");
            }
        }
    }

    remote_f = xstr_new(128);
    xstr_append_str(remote_f, &o->remote_path);
    xstr_push_back(remote_f, '/');

    stat_f = xstr_new(128);
    xstr_append_str(stat_f, &o->stats_path);
    xstr_push_back(stat_f, '/');

    for (iter = xlist_begin(&o->local_files);
            iter != xlist_end(&o->local_files);
            iter = xlist_iter_next(iter)) {

        local_f = xlist_iter_value(iter);

        xstr_assign_str_at(remote_f,
            xstr_size(&o->remote_path) + 1, local_f);
        xstr_assign_str_at(stat_f,
            xstr_size(&o->stats_path) + 1, local_f);

        stat(xstr_data(local_f), &curr); /* no need to check it's return value */

        /* skip a directory */
        if (S_ISDIR(curr.st_mode)) continue;

        if (flag & FLAG_LIST_ALL) {
            fprintf(stdout, "%s\n", xstr_data(local_f));
        } else if (flag & FLAG_DO_CLEAN) {
            fprintf(stdout, "rmstat [%s].\n", xstr_data(stat_f));
            unlink(xstr_data(stat_f));
        } else if (stat(xstr_data(stat_f), &befo) != 0) {
            if (flag & FLAG_LIST_UPLOAD) {
                fprintf(stdout, "%s\n", xstr_data(local_f));
            } else if (flag & FLAG_DO_INIT) {
                fprintf(stdout, "stat [%s].\n", xstr_data(local_f));
                create_file_r(xstr_data(stat_f));
            } else if (flag & FLAG_DO_UPLOAD) {
                fprintf(stdout, "upload [%s].\n", xstr_data(local_f));

                if (-1 != (sftp
                    ? sftp_send_file(sftp,
                        xstr_data(local_f), xstr_data(remote_f))
                    : scp_send_file(ssh,
                        xstr_data(local_f), xstr_data(remote_f)))) {
                    create_file_r(xstr_data(stat_f));
                }
            }
        } else if (curr.st_mtime > befo.st_mtime) {
            if (flag & FLAG_LIST_UPLOAD) {
                fprintf(stdout, "%s\n", xstr_data(local_f));
            } else if (flag & FLAG_DO_INIT) {
                fprintf(stdout, "stat [%s].\n", xstr_data(local_f));
                utime(xstr_data(stat_f), NULL);
            } else if (flag & FLAG_DO_UPLOAD) {
                fprintf(stdout, "upload [%s].\n", xstr_data(local_f));

                if (-1 != (sftp
                    ? sftp_send_file(sftp,
                        xstr_data(local_f), xstr_data(remote_f))
                    : scp_send_file(ssh,
                        xstr_data(local_f), xstr_data(remote_f)))) {
                    utime(xstr_data(stat_f), NULL);
                }
            }
        }
    }

    xstr_free(remote_f);
    xstr_free(stat_f);
    sftp_session_free(sftp);
    ssh_session_close(ssh);
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
    "    \"stats_path\": \"/tmp/.stats\",\n"            \
    "    \"use_sftp\": true\n"                          \
    "}\n"

static int generate_config_file(const char* file)
{
    int fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0644);

    if (fd < 0) {
        fprintf(stderr, "failed open file [%s]: %s.\n",
            file, strerror(errno));
        return -1;
    }

    fprintf(stdout, "write template config file to [%s].\n", file);

    if (write(fd, CONFIG_TEMPLATE, sizeof(CONFIG_TEMPLATE) - 1) < 0) {
        fprintf(stderr, "write failed: %s.\n", strerror(errno));
    }

    close(fd);
    return 0;
}
#undef CONFIG_TEMPLATE

static void usage(const char* name)
{
    fprintf(stderr, "usage: %s [option]\n"
        "    -l      - list the files to be uploaded.\n"
        "    -a      - list all matched file.\n"
        "    -u      - upload the files to be uploaded.\n"
        "    -i      - create all stat files.\n"
        "    -c      - remove all stat files.\n"
        "    -t      - generate template config file.\n"
        "    -f file - set config file. (default: " DEFAULT_CONFIG_FILE ")\n", name);
}

extern char* optarg;

int main(int argc, char** argv)
{
    options_t* opts;
    const char* cfg_file = DEFAULT_CONFIG_FILE;
    int flag = 0;
    int c;

    while ((c = getopt(argc, argv, "lauictf:")) != -1) {
        switch (c)
        {
        case 'l': flag = FLAG_LIST_UPLOAD; break;
        case 'a': flag = FLAG_LIST_ALL   ; break;
        case 'u': flag = FLAG_DO_UPLOAD  ; break;
        case 'i': flag = FLAG_DO_INIT    ; break;
        case 'c': flag = FLAG_DO_CLEAN   ; break;
        case 't': flag = FLAG_GEN_CONFIG ; break;
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
    if (flag & FLAG_GEN_CONFIG) {
        generate_config_file(cfg_file);
        return 0;
    }

    opts = config_load(cfg_file);

    if (!opts) return 1;

    char* p = strrchr(cfg_file, '/');

    /* cd to config file's path */
    if (p) {
        char* buf = malloc(p - cfg_file + 1);
        memcpy(buf, cfg_file, p - cfg_file);
        buf[p - cfg_file] = '\0';

        if (chdir(buf) != 0) {
            fprintf(stderr, "can't cd to [%s].\n", buf);
        }
        free(buf);
    }

    /* cd to opts->local_path */
    if (chdir(xstr_data(&opts->local_path)) != 0) {
        fprintf(stderr, "can't cd to [%s].\n",
            xstr_data(&opts->local_path));
    }

    expand_files(&opts->local_files);
    proccess_files(opts, flag);

    config_free(opts);
    return 0;
}