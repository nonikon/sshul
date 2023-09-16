#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "json_wrapper.h"

#define CONFIG_FILE_MAXSZ   2048

static char* const __dummy_label = ""; 
static char* __dummy_ignoref;

static void init_config(config_t* cfg)
{
    memset(cfg, 0, sizeof(config_t));
    cfg->remote_port = 22;
    // cfg->follow_link = 0;
    // cfg->use_compress = 0;
}

static void destroy_config(void* v)
{
    config_t* cfg = v;

    if (cfg->label != __dummy_label) {
        free(cfg->label);
    }
    free(cfg->remote_host);
    free(cfg->remote_user);
    free(cfg->remote_passwd);
    free(cfg->remote_path);
    free(cfg->local_path);

    if (cfg->ignore_files && cfg->ignore_files != &__dummy_ignoref) {
        for (int i = 0; cfg->ignore_files[i]; ++i) {
            free(cfg->ignore_files[i]);
        }
        free(cfg->ignore_files);
    }
}

static char* jstrdup(const json_value* js)
{
    char* str = malloc(json_get_string_length(js) + 1);
    memcpy(str, json_get_string(js), json_get_string_length(js) + 1);
    return str;
}

static int set_ignore_files(config_t* cfg, json_value* jarr)
{
    const unsigned n = json_get_array_length(jarr);

    cfg->ignore_files = malloc((n + 1) * sizeof(char*));
    memset(cfg->ignore_files, 0, (n + 1) * sizeof(char*));

    for (unsigned i = 0; i < n; ++i) {
        json_value* jval = json_get_array_value(jarr, i);

        if (json_get_type(jval) != json_string) {
            fprintf(stderr, "invalid config value in <ignore_files>.\n");
            return -1;
        }
        cfg->ignore_files[i] = jstrdup(jval);
    }
    return 0;
}

static int set_config(config_t* cfg, json_value* jobj)
{
    for (unsigned i = 0; i < json_get_object_length(jobj); ++i) {
        char* name = json_get_object_name(jobj, i);
        json_value* value = json_get_object_value(jobj, i);

         if (!strcmp(name, "label")) {
            if (json_get_type(value) != json_string) {
                fprintf(stderr, "invalid config value for <label>.\n");
                return -1;
            }
            cfg->label = jstrdup(value);
        } else if (!strcmp(name, "remote_host")) {
            if (json_get_type(value) != json_string || !json_get_string_length(value)) {
                fprintf(stderr, "invalid config value for <remote_host>.\n");
                return -1;
            }
            cfg->remote_host = jstrdup(value);
        } else if (!strcmp(name, "remote_port")) {
            if (json_get_type(value) != json_integer) {
                fprintf(stderr, "invalid config value for <remote_port>.\n");
                return -1;
            }
            cfg->remote_port = (int)json_get_int(value);
        } else if (!strcmp(name, "remote_user")) {
            if (json_get_type(value) != json_string || !json_get_string_length(value)) {
                fprintf(stderr, "invalid config value for <remote_user>.\n");
                return -1;
            }
            cfg->remote_user = jstrdup(value);
        } else if (!strcmp(name, "remote_passwd")) {
            if (json_get_type(value) != json_string || !json_get_string_length(value)) {
                fprintf(stderr, "invalid config value for <remote_passwd>.\n");
                return -1;
            }
            cfg->remote_passwd = jstrdup(value);
        } else if (!strcmp(name, "remote_path")) {
            if (json_get_type(value) != json_string || !json_get_string_length(value)) {
                fprintf(stderr, "invalid config value for <remote_path>.\n");
                return -1;
            }
            cfg->remote_path = jstrdup(value);
        } else if (!strcmp(name, "ignore_files")) {
            if (json_get_type(value) != json_array) {
                fprintf(stderr, "invalid config value for <ignore_files>.\n");
                return -1;
            }
            if (set_ignore_files(cfg, value) != 0) {
                return -1;
            }
        } else if (!strcmp(name, "local_path")) {
            if (json_get_type(value) != json_string || !json_get_string_length(value)) {
                fprintf(stderr, "invalid config value for <local_path>.\n");
                return -1;
            }
            cfg->local_path = jstrdup(value);
        } else if (!strcmp(name, "follow_link")) {
            if (json_get_type(value) != json_boolean) {
                fprintf(stderr, "invalid config value for <follow_link>.\n");
                return -1;
            }
            cfg->follow_link = json_get_bool(value);
        } else if (!strcmp(name, "use_compress")) {
            if (json_get_type(value) != json_boolean) {
                fprintf(stderr, "invalid config value for <use_compress>.\n");
                return -1;
            }
            cfg->use_compress = json_get_bool(value);
        } else {
            fprintf(stderr, "unkown config key <%s>.\n", name);
            return -1;
        }
    }

    if (!cfg->remote_host || !cfg->remote_user || !cfg->remote_passwd
            || !cfg->remote_path || !cfg->local_path) {
        fprintf(stderr, "need more config item.\n");
        return -1;
    }

    if (!cfg->label) {
        cfg->label = __dummy_label;
    }
    if (!cfg->ignore_files) {
        cfg->ignore_files = &__dummy_ignoref;
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

    fp = fopen(file, "rb");
    if (!fp) {
        fprintf(stderr, "open config file (%s) failed.\n", file);
        goto err;
    }

    nr = fread(buf, 1, CONFIG_FILE_MAXSZ, fp);
    fclose(fp);

    if (nr <= 0) {
        fprintf(stderr, "failed read config file (%s).\n", file);
        goto err;
    }
    if (nr == CONFIG_FILE_MAXSZ) {
        fprintf(stderr, "config file (%s) too large.\n", file);
        goto err;
    }

    buf[*blen = nr] = '\0';
    return buf;
err:
    free(buf);
    return NULL;
}

xlist_t* configs_load(const char* file)
{
    json_settings sets = { 0, json_enable_comments };
    xlist_t* cfgs;
    int sz;
    char* buf = file_full_read(file, &sz);
    json_value* jval;

    if (!buf) {
        return NULL;
    }

    jval = json_parse_ex (&sets, buf, sz, 0);
    free(buf);

    if (!jval) {
        fprintf(stderr, "unable to parse data as a json.\n");
        return NULL;
    }

    cfgs = xlist_new(sizeof(config_t), destroy_config);

    if (json_get_type(jval) == json_object) {
        config_t* cfg = xlist_alloc_back(cfgs);

        init_config(cfg);
        if (set_config(cfg, jval) != 0) {
            goto error;
        }
    } else if (json_get_type(jval) == json_array) {
        for (unsigned i = 0; i < json_get_array_length(jval); ++i) {
            config_t* cfg = xlist_alloc_back(cfgs);

            init_config(cfg);
            if (set_config(cfg, json_get_array_value(jval, i)) != 0) {
                goto error;
            }
        }
    } else {
        fprintf(stderr, "invalid config item.\n");
        goto error;
    }

    json_value_free(jval);
    return cfgs;
error:
    configs_destroy(cfgs);
    json_value_free(jval);
    return NULL;
}

void configs_destroy(xlist_t* cfgs)
{
    xlist_free(cfgs);
}
