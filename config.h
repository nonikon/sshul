#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "xlist.h"
#include "xstring.h"

typedef struct {
    xstr_t  remote_host;
    int     remote_port;
    xstr_t  remote_user;
    xstr_t  remote_passwd;
    xstr_t  remote_path;
    xstr_t  local_path;
    xlist_t local_files; // element type 'xstr_t'
    xstr_t  db_path;
    int     use_sftp;
    int     disable;
} config_t;

int configs_load(xlist_t* cfgs, const char* file);
void configs_destroy(xlist_t* cfgs);

#endif // _CONFIG_H_