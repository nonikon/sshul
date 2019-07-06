#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "xlist.h"
#include "xstring.h"

typedef struct options
{
    xstr_t*  remote_host;
    int      remote_port;
    xstr_t*  remote_user;
    xstr_t*  remote_passwd;
    xstr_t*  remote_path;

    xlist_t* local_files; // element type 'xstr_t*'
    xstr_t*  local_path;
    xstr_t*  stats_path;
} options_t;

options_t* config_load(const char* file);
void config_free(options_t* opts);

#endif // _CONFIG_H_