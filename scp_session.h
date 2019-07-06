#ifndef _SCP_SESSION_H_
#define _SCP_SESSION_H_

#include <libssh2.h>

typedef LIBSSH2_SESSION scp_session_t;

scp_session_t* scp_session_open(
    const char* host, int port, const char* user, const char* passwd);

int scp_session_send_file(
    scp_session_t* s, const char* local, const char* remote);

void scp_session_close(scp_session_t* s);

#endif // _SCP_SESSION_H_