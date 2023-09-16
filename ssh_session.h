#ifndef _SSH_SESSION_H_
#define _SSH_SESSION_H_

#include <libssh2.h>
#include <libssh2_sftp.h>
#ifdef _WIN32
#include <windows.h>
#endif

typedef LIBSSH2_SESSION ssh_t;
typedef LIBSSH2_SFTP sftp_t;

ssh_t* ssh_session_open(const char* host, int port, int compress,
        const char* user, const char* passwd);
void ssh_session_close(ssh_t* s);

sftp_t* sftp_session_new(ssh_t* s);
void sftp_session_free(sftp_t* s);

/* upload a file via SFTP */
int sftp_send_file(sftp_t* s, const char* local, const char* remote,
        int mode, int exists, time_t mtime, uint64_t size);
/* download a file via SFTP */
int sftp_recv_file(sftp_t* s, const char* local, const char* remote,
        int mode, int exists, time_t mtime, uint64_t size);

#endif // _SSH_SESSION_H_