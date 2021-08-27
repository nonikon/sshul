#include <string.h>
#include <stdio.h>

#include "config.h"
#include "json_wrapper.h"

#define CONFIG_FILE_MAXSZ   2048

static void on_xstr_free(void* pvalue)
{
    xstr_destroy(pvalue);
}

static void init_config(config_t* cfg)
{
    xstr_init(&cfg->remote_host, 36);
    cfg->remote_port = 22;
    xstr_init(&cfg->remote_user, 16);
    xstr_init(&cfg->remote_passwd, 16);
    xstr_init(&cfg->remote_path, 128);
    xstr_init(&cfg->local_path, 128);
    xstr_init(&cfg->db_path, 128);
    cfg->use_sftp = 1; /* default use 'sftp' */
    cfg->disable = 0;
    xlist_init(&cfg->local_files, sizeof(xstr_t), on_xstr_free);
}

static void destroy_config(void* v)
{
    config_t* cfg = v;

    xstr_destroy(&cfg->remote_host);
    xstr_destroy(&cfg->remote_user);
    xstr_destroy(&cfg->remote_passwd);
    xstr_destroy(&cfg->remote_path);
    xstr_destroy(&cfg->local_path);
    xstr_destroy(&cfg->db_path);
    xlist_destroy(&cfg->local_files);
}

static int set_local_files(xlist_t* files, json_value* jarr)
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

static int set_config(config_t* cfg, json_value* jobj)
{
    char* name;
    json_value* value;
    int i;

    for (i = 0; i < json_get_object_length(jobj); ++i) {
        name = json_get_object_name(jobj, i);
        value = json_get_object_value(jobj, i);

        if (!strcmp(name, "remote_host")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->remote_host,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_port")) {
            if (json_get_type(value) != json_integer) {
                return -1;
            }
            cfg->remote_port = json_get_int(value);
        } else if (!strcmp(name, "remote_user")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->remote_user,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_passwd")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->remote_passwd,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "remote_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->remote_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "local_files")) {
            if (json_get_type(value) != json_array) {
                return -1;
            }
            set_local_files(&cfg->local_files, value);
        } else if (!strcmp(name, "local_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->local_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "db_path")) {
            if (json_get_type(value) != json_string) {
                return -1;
            }
            xstr_assign(&cfg->db_path,
                json_get_string(value), json_get_string_length(value));
        } else if (!strcmp(name, "use_sftp")) {
            if (json_get_type(value) != json_boolean) {
                return -1;
            }
            cfg->use_sftp = json_get_bool(value);
        } else if (!strcmp(name, "disable")) {
            if (json_get_type(value) != json_boolean) {
                return -1;
            }
            cfg->disable = json_get_bool(value);
        }
    }

    if (xstr_empty(&cfg->remote_host) || xstr_empty(&cfg->remote_user)
            || xstr_empty(&cfg->remote_passwd) || xstr_empty(&cfg->remote_path)
            || xlist_empty(&cfg->local_files) || xstr_empty(&cfg->local_path)
            || xstr_empty(&cfg->db_path))
        return -1;

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

    fp = fopen(file, "rb");
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

int configs_load(xlist_t* cfgs, const char* file)
{
    int i;
    char* buf = file_full_read(file, &i);
    json_value* jval;

    if (!buf) return -1;

    jval = json_parse(buf, i);
    free(buf);

    if (!jval) {
        fprintf(stderr, "unable to parse data as a json.\n");
        return -1;
    }

    xlist_init(cfgs, sizeof(config_t), destroy_config);

    if (json_get_type(jval) == json_object) {
        config_t* cfg = xlist_alloc_back(cfgs);

        init_config(cfg);
        if (set_config(cfg, jval) != 0) {
            goto error;
        }
    } else if (json_get_type(jval) == json_array) {
        for (i = 0; i < json_get_array_length(jval); ++i) {
            config_t* cfg = xlist_alloc_back(cfgs);

            init_config(cfg);
            if (set_config(cfg, json_get_array_value(jval, i)) != 0) {
                goto error;
            }
        }
    } else {
        goto error;
    }

    json_value_free(jval);
    return 0;
error:
    fprintf(stderr, "invalid json config file.\n");
    configs_destroy(cfgs);
    json_value_free(jval);
    return -1;
}

void configs_destroy(xlist_t* opts)
{
    xlist_destroy(opts);
}
