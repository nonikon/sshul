#ifndef _SSH_SESSION_H_
#define _SSH_SESSION_H_

#include <libssh2.h>
#include <libssh2_sftp.h>

typedef LIBSSH2_SESSION ssh_session_t;
typedef LIBSSH2_SFTP sftp_session_t;

ssh_session_t* ssh_session_open(
    const char* host, int port, const char* user, const char* passwd);
void ssh_session_close(ssh_session_t* s); // 's' can be NULL

sftp_session_t* sftp_session_new(ssh_session_t* s); // 's' can be NULL
void sftp_session_free(sftp_session_t* s); // 's' can be NULL

// upload a file via SCP.
int scp_send_file(ssh_session_t* s, const char* local, const char* remote);
// upload a file via SFTP (auto create missing dirs).
int sftp_send_file(sftp_session_t* s, const char* local, const char* remote);

#endif // _SSH_SESSION_H_