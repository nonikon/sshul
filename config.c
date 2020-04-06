#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "config.h"
#include "json_wrapper.h"

#define CONFIG_FILE_MAXSZ   2048

static void _on_xstr_free(void* pvalue)
{
    xstr_destroy(pvalue);
}

static options_t* options_new()
{
    options_t* o = malloc(sizeof(options_t));

    xstr_init(&o->remote_host, 36);
    o->remote_port = 22;
    xstr_init(&o->remote_user, 16);
    xstr_init(&o->remote_passwd, 16);
    xstr_init(&o->remote_path, 128);
    xstr_init(&o->local_path, 128);
    xstr_init(&o->db_path, 128);
    o->use_sftp = 1; /* default use 'sftp' */
    xlist_init(&o->local_files, sizeof(xstr_t), _on_xstr_free);

    return o;
}

static void options_free(options_t* o)
{
    if (!o) return;

    xstr_destroy(&o->remote_host);
    xstr_destroy(&o->remote_user);
    xstr_destroy(&o->remote_passwd);
    xstr_destroy(&o->remote_path);
    xstr_destroy(&o->local_path);
    xstr_destroy(&o->db_path);
    xlist_destroy(&o->local_files);

    free(o);
}

static int set_option_local_files(xlist_t* files, json_value* jarr)
{
    json_value* jval;
    xstr_t file;
    int i;

    for (i = 0; i < json_get_array_length(jarr); ++i) {
        jval = json_get_array_value(jarr, i);

        if (json_get_type(jval) != json_string) {
            return -1;
        }

        xstr_init_with(&file, json_get_string(jval),
            json_get_string_length(jval));

        xlist_push_back(files, &file);
    }

    return 0;
}

static int set_options(options_t* o, json_value* jobj)
{
    char* name;
    json_value* value;
    int i;

    if (json_get_type(jobj) != json_object) {
        return -1;
    }
    for (i = 0; i < json_get_object_length(jobj); ++i) {
        name = json_get_object_name(jobj, i);
        value = json_get_object_value(jobj, i);

        if (!strcmp(name, "remote_host")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->remote_host,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_port")) {
            if (json_get_type(value) != json_integer) {
                return -1;
            }
            o->remote_port = json_get_int(value);
        } else if (!strcmp(name, "remote_user")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->remote_user,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_passwd")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->remote_passwd,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->remote_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "local_files")) {
            if (json_get_type(value) != json_array) {
                return -1;
            }
            set_option_local_files(&o->local_files, value);
        } else if (!strcmp(name, "local_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->local_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "db_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&o->db_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "use_sftp")) {
            if (json_get_type(value) != json_boolean) {
                return -1;
            }
            o->use_sftp = json_get_bool(value);
        }
    }

    return 0;
}

static int check_options(options_t* o)
{
    if (xstr_empty(&o->remote_host)) {
        fprintf(stderr, "'remote_host' can't be empty.\n");
        return -1;
    }
    if (o->remote_port < 1 || o->remote_port > 65535) {
        fprintf(stderr, "wrong 'remote_port': %d.\n", o->remote_port);
        return -1;
    }
    if (xstr_empty(&o->remote_user)) {
        fprintf(stderr, "'remote_user' can't be empty.\n");
        return -1;
    }
    if (xstr_empty(&o->remote_passwd)) {
        fprintf(stderr, "'remote_passwd' can't be empty.\n");
        return -1;
    }
    if (xstr_empty(&o->remote_path)) {
        fprintf(stderr, "'remote_path' can't be empty.\n");
        return -1;
    }
    if (xlist_empty(&o->local_files)) {
        fprintf(stderr, "'local_files' can't be empty.\n");
        return -1;
    }
    if (xstr_empty(&o->local_path)) {
        fprintf(stderr, "'local_path' can't be empty.\n");
        return -1;
    }
    if (xstr_empty(&o->db_path)) {
        fprintf(stderr, "'db_path' can't be empty.\n");
        return -1;
    }

    return 0;
}

static char* file_full_read(const char* file, int* blen)
{
    struct stat s;
    int len;
    char* buf = NULL;
    FILE* fp = NULL;

    if (stat(file, &s) != 0) {
        fprintf(stderr, "config file [%s] not found.\n", file);
        return NULL;
    }

    len = s.st_size;

    if (len > CONFIG_FILE_MAXSZ) {
        fprintf(stderr, "config file [%s] to large.\n", file);
        return NULL;
    }

    buf = malloc(len);

    if (!buf) {
        fprintf(stderr, "failed alloc [%d] bytes memory.\n", len);
        return NULL;
    }

    fp = fopen(file, "r");

    if (!fp) {
        fprintf(stderr, "unable to open config file [%s].\n", file);
        free(buf);
        return NULL;
    }

    if (fread(buf, 1, len, fp) != len) {
        fprintf(stderr, "failed to read config file [%s].\n", file);
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);

    *blen = len;

    return buf;
}

options_t* config_load(const char* file)
{
    options_t* opts;
    int len;
    char* buf = file_full_read(file, &len);
    json_value* jval;

    if (!buf) return NULL;

    jval = json_parse(buf, len);

    free(buf);

    if (!jval) {
        fprintf(stderr, "unable to parse data as a json.\n");
        return NULL;
    }

    opts = options_new();

    if (set_options(opts, jval) != 0
            || check_options(opts) != 0) {
        options_free(opts);
        opts = NULL;
    }

    json_value_free(jval);

    return opts;
}

void config_free(options_t* opts)
{
    options_free(opts);
}
