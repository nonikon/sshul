#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

#include "config.h"
#include "ssh_session.h"
#include "match.h"
#include "version.h"
#include "xstring.h"

#define DEFAULT_CONFIG_FILE "sshul.json"

enum {
    ACT_NONE,
    ACT_LIST,
    ACT_UPDOWN,
};

#define CFG_TEMPLATE \
    "[{\n" \
    "\t\"label\": \"\"\n" \
    "\t,\"remote_host\": \"192.168.1.1\"\n" \
    "\t,\"remote_port\": 22\n" \
    "\t,\"remote_user\": \"root\"\n" \
    "\t,\"remote_passwd\": \"123456\"\n" \
    "\t,\"remote_path\": \"/tmp\"\n" \
    "\t,\"local_path\": \".\"\n" \
    "\t,\"ignore_files\": [ \"*.o\", \".git/\", \".vscode/\", \"build/\", \"sshul.json\" ]\n" \
    "\t,\"follow_link\": false\n" \
    "\t,\"use_compress\": false\n" \
    "}]\n"

static int generate_config_file(const char* file)
{
    FILE* fp = fopen(file, "wx");

    if (!fp) {
        fprintf(stderr, "open file (%s) failed (%s).\n", file, strerror(errno));
        return 1;
    }
    fprintf(stdout, "write template config file to [%s].\n", file);

    if (!fwrite(CFG_TEMPLATE, sizeof(CFG_TEMPLATE) - 1, 1, fp)) {
        fprintf(stderr, "write failed (%s).\n", strerror(errno));
        fclose(fp);
        return 1;
    }
    fclose(fp);
    return 0;
}

#ifdef _WIN32
static int cd_to_filedir(const char* file)
{
    char* p;

    if ((p = strrchr(file, '/')) || (p = strrchr(file, '\\'))) {
        char* dir = _strdup(file);
        dir[p - file] = '\0';

        if (!SetCurrentDirectoryA(dir)) {
            fprintf(stderr, "can't cd to [%s].\n", dir);
            free(dir);
            return 1;
        }
        free(dir);
    }

    return 0;
}

static int check_local_dir(char* path, int create)
{
    DWORD attrs = GetFileAttributesA(path);

    if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
            fprintf(stderr, "%s is not a local dir.\n", path);
            return -1;
        }
        return 0;
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND) {
        fprintf(stderr, "can't stat local %s (%d).\n", path, GetLastError());
        return -1;
    }
    if (!create) {
        fprintf(stderr, "local %s not exists.\n", path);
        return -1;
    }
    if (!CreateDirectoryA(path, 0) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "create local dir (%s) failed (%d).\n", path, GetLastError());
        return -1;
    }
    return 0;
}
#else

static int cd_to_filedir(const char* file)
{
    char* p;

    if ((p = strrchr(file, '/'))) {
        char* dir = strdup(file);
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

static int check_local_dir(const char* path, int create)
{
    struct stat statbuf;

    if (stat(path, &statbuf) == 0) {
        if (!S_ISDIR(statbuf.st_mode)) {
            fprintf(stderr, "%s is not a local dir.\n", path);
            return -1;
        }
        return 0;
    }
    if (errno != ENOENT) {
        fprintf(stderr, "can't stat local %s (%s).\n", path, strerror(errno));
        return -1;
    }
    if (!create) {
        fprintf(stderr, "local %s not exists.\n", path);
        return -1;
    }
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "create local dir (%s) failed (%s).\n", path, strerror(errno));
        return -1;
    }
    return 0;
}
#endif

static int check_remote_dir(const char* path, int create, sftp_t* sftp)
{
    LIBSSH2_SFTP_ATTRIBUTES attrs;

    if (libssh2_sftp_stat(sftp, path, &attrs) == 0) {
        if (!LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
            fprintf(stderr, "%s is not a remote dir.\n", path);
            return -1;
        }
        return 0;
    }
    if (libssh2_sftp_last_error(sftp) != LIBSSH2_FX_NO_SUCH_FILE) {
        fprintf(stderr, "can't stat remote %s (%d).\n",
            path, (int)libssh2_sftp_last_error(sftp));
        return -1;
    }
    if (!create) {
        fprintf(stderr, "remote %s not exists.\n", path);
        return -1;
    }
    if (libssh2_sftp_mkdir(sftp, path, 0755) != 0
            && libssh2_sftp_last_error(sftp) != LIBSSH2_FX_FILE_ALREADY_EXISTS) {
        fprintf(stderr, "create remote dir (%s) failed (%d).\n",
            path, (int)libssh2_sftp_last_error(sftp));
        return -1;
    }
    return 0;
}

static const char* get_ftype_str(int mode)
{
    switch (mode & LIBSSH2_SFTP_S_IFMT) {
    case LIBSSH2_SFTP_S_IFREG:
        return "REG";
    case LIBSSH2_SFTP_S_IFDIR:
        return "DIR";
    case LIBSSH2_SFTP_S_IFLNK:
        return "LNK";
    default: /* other type is not supported */
        return NULL;
    }
}

static void do_list(xlist_t* items)
{
    for (xlist_iter_t i = xlist_begin(items);
            i != xlist_end(items); i = xlist_iter_next(i)) {
        file_item_t* item = xlist_iter_value(i);
        const char* type = get_ftype_str(item->mode);

        if (type) {
            if (item->is_newer) {
                fprintf(stdout, item->is_exist ? "\033[31m[OVR %s]\033[0m %s\n"
                    : "\033[32m[NEW %s]\033[0m %s\n", type, item->file);
            } else {
                fprintf(stdout, "\033[90m[IGN %s]\033[0m %s\n", type, item->file);
            }
        } else {
            fprintf(stdout, "\033[90m[IGN UNN]\033[0m %s\n", item->file);
        }
    }
}

static void do_updown(xlist_t* items, config_t* cfg, sftp_t* sftp, int reverse, int prompt)
{
    xstr_t local;
    xstr_t remote;
    size_t ol;
    size_t or;

    if (prompt) {
        size_t n = 0;

        for (xlist_iter_t i = xlist_begin(items);
                i != xlist_end(items); i = xlist_iter_next(i)) {
            file_item_t* item = xlist_iter_value(i);
            const char* type = get_ftype_str(item->mode);

            if (type && item->is_newer) {
                fprintf(stdout, item->is_exist ? "\033[31m[OVR %s]\033[0m %s\n"
                    : "\033[32m[NEW %s]\033[0m %s\n", type, item->file);
                ++n;
            }
        }
        if (n > 0) {
            char input[8] = { 0 };

            fprintf(stdout, "The above files will be %s, continue? (Y/n):",
                reverse ? "downloaded" : "uploaded");
            fgets(input, sizeof(input), stdin);
            if (input[0] != 'y' && input[0] != 'Y') {
                fprintf(stdout, "exit\n");
                return;
            }
        }
    }

    if (reverse) {
        if (check_local_dir(cfg->local_path, 1) != 0) {
            return;
        }
    } else {
        if (check_remote_dir(cfg->remote_path, 1, sftp) != 0) {
            return;
        }
    }

    xstr_init_ex(&local, 512);
    xstr_append(&local, cfg->local_path);
    xstr_push_back(&local, '/');

    xstr_init_ex(&remote, 512);
    xstr_append(&remote, cfg->remote_path);
    xstr_push_back(&remote, '/');

    ol = xstr_size(&local);
    or = xstr_size(&remote);

    for (xlist_iter_t i = xlist_begin(items);
            i != xlist_end(items); i = xlist_iter_next(i)) {
        file_item_t* item = xlist_iter_value(i);
        const char* type = get_ftype_str(item->mode);

        if (type && item->is_newer) {
            xstr_assign_at(&local, ol, item->file);
            xstr_assign_at(&remote, or, item->file);

            if (reverse) {
                fprintf(stdout, item->is_exist
                            ? "\033[31m [DOWNLD]\033[0m \033[s---- %s \033[?25l\033[31m"
                            : "\033[32m [DOWNLD]\033[0m \033[s---- %s \033[?25l\033[31m", item->file);
                sftp_recv_file(sftp, xstr_data(&local), xstr_data(&remote),
                    item->mode, item->is_exist, item->mtime, item->size);
            } else {
                fprintf(stdout, item->is_exist
                            ? "\033[31m [UPLOAD]\033[0m \033[s---- %s \033[?25l\033[31m"
                            : "\033[32m [UPLOAD]\033[0m \033[s---- %s \033[?25l\033[31m", item->file);
                sftp_send_file(sftp, xstr_data(&local), xstr_data(&remote),
                    item->mode, item->is_exist, item->mtime, item->size);
            }
            fprintf(stdout, "\033[0m\033[?25h\n");
        }
    }

    xstr_destroy(&local);
    xstr_destroy(&remote);
}

static void process_config(config_t* cfg, int action, int reverse, int prompt)
{
    xlist_t* items;
    ssh_t* scp;
    sftp_t* sftp;

    fprintf(stderr, "[%s] %s [%s@%s:%s]\n", cfg->local_path,
        reverse ? "<-" : "->", cfg->remote_user, cfg->remote_host, cfg->remote_path);

    scp = ssh_session_open(cfg->remote_host, cfg->remote_port, cfg->use_compress,
                cfg->remote_user, cfg->remote_passwd);
    if (!scp) {
        fprintf(stderr, "ssh_session_open failed.\n");
        return;
    }

    sftp = sftp_session_new(scp);
    if (!sftp) {
        fprintf(stderr, "sftp_session_new failed.\n");
        ssh_session_close(scp);
        return;
    }

    if (reverse) {
        /* iterate remote directory to get download list */
        items = iterate_directory(cfg->remote_path, cfg->ignore_files, cfg->follow_link, sftp);
        /* download mode, <items> is remote file list, check local files status */
        iterate_directory_setextra(items, cfg->local_path, cfg->follow_link, NULL);
    } else {
        /* iterate local directory to get upload list */
        items = iterate_directory(cfg->local_path, cfg->ignore_files, cfg->follow_link, NULL);
        /* upload mode, <items> is local file list, check remote files status */
        iterate_directory_setextra(items, cfg->remote_path, cfg->follow_link, sftp);
    }

    switch (action) {
    case ACT_LIST:
        do_list(items);
        break;
    case ACT_UPDOWN:
        do_updown(items, cfg, sftp, reverse, prompt);
        break;
    }

    iterate_directory_free(items);

    sftp_session_free(sftp);
    ssh_session_close(scp);
}

static void usage(const char* s)
{
    fprintf(stderr, "sshul " VERSION_STRING ", libssh2 " LIBSSH2_VERSION
        ", usage: %s [OPTION]... [CFG_FILE][:LABEL]\n"
        "[OPTION]:\n"
        "  -l   list all matched file.\n"
        "  -x   upload or download the newer files.\n"
        "  -r   switch to download mode (default is upload).\n"
        "  -y   automatic yes to prompts.\n"
        "  -t   generate template config file (" DEFAULT_CONFIG_FILE ").\n"
        "  -v   show version message.\n"
        "  -h   show this help message.\n", s);

    fprintf(stderr, "[CFG_FILE] keys:\n"
        "  label         - label of current config. (default: \"\")\n"
        "  remote_host   - ssh server ip address.\n"
        "  remote_port   - ssh server port. (default: 22)\n"
        "  remote_user   - ssh user name.\n"
        "  remote_passwd - ssh user password.\n"
        "  remote_path   - the remote path which remote files in.\n"
        "  local_path    - the local path which local files in.\n"
        "  ignore_files  - the file PATTERNs which used to filter remote or local files.\n"
        "  follow_link   - follow symbolic link. (default: false)\n"
        "  use_compress  - enable compress. (default: false)\n");

    fprintf(stderr, "[PATTERN] example:\n"
        "  dir/*.[ch] dir/*/file.c di?/*.c dir/*.[a-z]\n");
}

int main(int argc, char** argv)
{
    const char* file = DEFAULT_CONFIG_FILE;
    const char* label = "";
    int action = ACT_NONE;
    int reverse = 0;
    int prompt = 1;
#ifdef _WIN32
    WSADATA wsData;
    WSAStartup(MAKEWORD(2, 2), &wsData);
#endif

    for (int i = 1; i < argc; ++i) {
        char* opt = argv[i];

        if (opt[0] != '-') {
            char* p = strrchr(opt, ':');
            if (p) {
                label = p + 1;
                p[0] = '\0';
                if (opt != p) {
                    file = opt;
                }
            } else {
                file = opt;
            }
            continue;;
        }
        while (*++opt) {
            switch (opt[0]) {
            case 'l': action = ACT_LIST; continue;
            case 'x': action = ACT_UPDOWN; continue;
            case 'r': reverse = 1; continue;
            case 'y': prompt = 0; continue;
            case 't':
                return generate_config_file(file);
            case 'v':
                fprintf(stderr, "sshul " VERSION_STRING ", libssh2 " LIBSSH2_VERSION "\n");
                return 1;
            case 'h':
                usage(argv[0]);
                return 1;
            default:
                fprintf(stderr, "invalid option [-%c].\n", opt[0]);
                return 1;
            }
        }
    }

    if (action != ACT_NONE) {
        xlist_t* cfgs;

        /* load configs to 'cfgs' from 'file' */
        if (!(cfgs = configs_load(file))) {
            fprintf(stderr, "config load failed, exit.\n");
            return 1;
        }
        /* cd to config file's path */
        if (cd_to_filedir(file) != 0) {
            return 1;
        }

        for (xlist_iter_t i = xlist_begin(cfgs);
                i != xlist_end(cfgs); i = xlist_iter_next(i)) {
            config_t* cfg = xlist_iter_value(i);

            if (!strcmp(cfg->label, label)) {
                process_config(cfg, action, reverse, prompt);
            }
        }

        configs_destroy(cfgs);
        return 0;
    }

    usage(argv[0]);
    return 1;
}
