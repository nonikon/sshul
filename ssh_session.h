#ifndef _SSH_SESSION_H_
#define _SSH_SESSION_H_

#include <libssh2.h>
#include <libssh2_sftp.h>

typedef LIBSSH2_SESSION ssh_t;
typedef LIBSSH2_SFTP    sftp_t;

ssh_t* ssh_session_open(const char* host,
        int port, const char* user, const char* passwd);
void ssh_session_close(ssh_t* s);

sftp_t* sftp_session_new(ssh_t* s);
void sftp_session_free(sftp_t* s);

/* upload a file via SCP. */
int scp_send_file(ssh_t* s, const char* local, const char* remote,
        int mode, unsigned long size);
/* upload a file via SFTP (auto create missing dirs). */
int sftp_send_file(sftp_t* s, const char* local, const char* remote,
        int mode, unsigned long size);

#endif // _SSH_SESSION_H_