#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#include "config.h"
#include "json_wrapper.h"

#define DEFAULT_LOCAL_PATH  "."
#define DEFAULT_STATS_PATH  ".stats"

static void _on_xstr_free(void* pvalue)
{
    xstr_free(*(xstr_t**)pvalue);
}

static options_t* options_new()
{
    options_t* o = malloc(sizeof(options_t));

    o->remote_host = xstr_new(36);
    o->remote_port = 22;
    o->remote_user = xstr_new(16);
    o->remote_passwd = xstr_new(16);
    o->remote_path = xstr_new(128);

    o->local_path = xstr_new(128);
    o->stats_path = xstr_new(128);
    o->use_sftp = 1; // default use 'sftp'
    o->local_files = xlist_new(sizeof(xstr_t*), _on_xstr_free);

    xstr_append(o->local_path,
        DEFAULT_LOCAL_PATH, sizeof(DEFAULT_LOCAL_PATH) - 1);
    xstr_append(o->stats_path,
        DEFAULT_STATS_PATH, sizeof(DEFAULT_STATS_PATH) - 1);

    return o;
}

static void options_free(options_t* o)
{
    if (o) {
        xstr_free(o->remote_host);
        xstr_free(o->remote_user);
        xstr_free(o->remote_passwd);
        xstr_free(o->remote_path);
        xstr_free(o->local_path);
        xstr_free(o->stats_path);
        xlist_free(o->local_files);
        free(o);
    }
}

static int set_option_local_files(xlist_t* files, json_value* jarr)
{
    json_value* jval;
    xstr_t* file;
    int i;

    for (i = 0; i < json_get_array_length(jarr); ++i) {
        jval = json_get_array_value(jarr, i);

        if (json_get_type(jval) != json_string) {
            return -1;
        }

        file = xstr_new_with(json_get_string(jval), json_get_string_length(jval));
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
            xstr_assign(o->remote_host,
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
            xstr_assign(o->remote_user,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_passwd")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(o->remote_passwd,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(o->remote_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "local_files")) {
            if (json_get_type(value) != json_array) {
                return -1;
            }
            set_option_local_files(o->local_files, value);
        } else if (!strcmp(name, "local_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(o->local_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "stats_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(o->stats_path,
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

#if 0
static void json_test(json_value* jobj)
{
    char* name;
    json_value* jval;

    if (json_get_type(jobj) != json_object) {
        return;
    }

    for (int i = 0; i < json_get_object_length(jobj); ++i) {
        name = json_get_object_name(jobj, i);
        jval = json_get_object_value(jobj, i);

        switch (json_get_type(jval))
        {
        case json_integer:
            printf("integer: [%s:%ld]\n", name, json_get_int(jval));
            break;
        case json_boolean:
            printf("bool: [%s:%d]\n", name, json_get_bool(jval));
            break;
        case json_double:
            printf("double: [%s:%lf]\n", name, json_get_double(jval));
            break;
        case json_string:
            printf("string: [%s:%s]\n", name, json_get_string(jval));
            break;
        case json_array:
            printf("array: [%s:[", name);
            for (int j = 0; j < json_get_array_length(jval); ++j) {
                printf("%s,", json_get_string(json_get_array_value(jval, j))); // todo
            }
            printf("]]\n");
            break;
        default:
            printf("default type\n");
            break;
        }
    }
}
#endif

options_t* config_load(const char* file)
{
    options_t* opts;
    struct stat s;
    char* buf;
    int len;
    json_value* jval;
    FILE* fp;

    if (stat(file, &s) != 0) {
        fprintf(stderr, "config file [%s] not found.\n", file);
        return NULL;
    }

    len = s.st_size;
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

    jval = json_parse(buf, len);
    if (!jval) {
        fprintf(stderr, "unable to parse data as a json.\n");
        free(buf);
        return NULL;
    }
    free(buf);

    // json_test(jval);
    opts = options_new();
    if (set_options(opts, jval) != 0) {
        options_free(opts);
        opts = NULL;
    }
    // todo, check opts

    json_value_free(jval);

    return opts;
}

void config_free(options_t* opts)
{
    options_free(opts);
}
