#include <string.h>
#include <stdio.h>

#include "config.h"
#include "json_wrapper.h"

#define CONFIG_FILE_MAXSZ   2048

static void _on_xstr_free(void* pvalue)
{
    xstr_destroy(pvalue);
}

static void options_init(options_t* o)
{
    xstr_init(&o->remote_host, 36);
    o->remote_port = 22;
    xstr_init(&o->remote_user, 16);
    xstr_init(&o->remote_passwd, 16);
    xstr_init(&o->remote_path, 128);
    xstr_init(&o->local_path, 128);
    xstr_init(&o->db_path, 128);
    o->use_sftp = 1; /* default use 'sftp' */
    xlist_init(&o->local_files, sizeof(xstr_t), _on_xstr_free);
}

static void options_destroy(options_t* o)
{
    xstr_destroy(&o->remote_host);
    xstr_destroy(&o->remote_user);
    xstr_destroy(&o->remote_passwd);
    xstr_destroy(&o->remote_path);
    xstr_destroy(&o->local_path);
    xstr_destroy(&o->db_path);
    xlist_destroy(&o->local_files);
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
    char* buf;
    FILE* fp;
    int nr;

    buf = malloc(CONFIG_FILE_MAXSZ);
    if (!buf) {
        fprintf(stderr, "out of memory.\n");
        return NULL;
    }

    fp = fopen(file, "r");
    if (!fp) {
        fprintf(stderr, "open config file [%s] failed.\n", file);
        goto err;
    }

    nr = fread(buf, 1, CONFIG_FILE_MAXSZ, fp);
    fclose(fp);

    if (nr <= 0) {
        fprintf(stderr, "failed read config file [%s].\n", file);
        goto err;
    }
    if (nr == CONFIG_FILE_MAXSZ) {
        fprintf(stderr, "config file [%s] too large.\n", file);
        goto err;
    }

    buf[*blen = nr] = '\0';
    return buf;
err:
    free(buf);
    return NULL;
}

int config_load(options_t* opts, const char* file)
{
    int len;
    char* buf = file_full_read(file, &len);
    json_value* jval;

    if (!buf) return -1;

    jval = json_parse(buf, len);
    free(buf);

    if (!jval) {
        fprintf(stderr, "unable to parse data as a json.\n");
        return -1;
    }

    options_init(opts);

    if (set_options(opts, jval) != 0
            || check_options(opts) != 0) {
        options_destroy(opts);
        json_value_free(jval);
        return -1;
    }

    json_value_free(jval);
    return 0;
}

void config_destroy(options_t* opts)
{
    options_destroy(opts);
}
