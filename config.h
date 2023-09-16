#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "xlist.h"

typedef struct {
    char* label;
    char* remote_host;
    int remote_port;
    char* remote_user;
    char* remote_passwd;
    char* remote_path;
    char* local_path;
    char** ignore_files; // End with <NULL>
    int follow_link;
    int use_compress;
} config_t;

xlist_t* configs_load(const char* file);
void configs_destroy(xlist_t* cfgs);

#endif // _CONFIG_H_