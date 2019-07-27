#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ssh_session.h"

#define READ_BUF_SIZE   8192

ssh_session_t* ssh_session_open(
    const char* host, int port, const char* user, const char* passwd)
{
    ssh_session_t* session = NULL;
    struct sockaddr_in sin;
    char* errmsg;
    int sock;

/*
    if (libssh2_init(0) != 0) {
        return NULL;
    } */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "create socket failed: %s.\n", strerror(errno));
        return NULL;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);

    // todo, resolve ip address
    if(connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        fprintf(stderr, "connect [%s] failed: %s.\n", host, strerror(errno));
        goto error;
    }

    session = libssh2_session_init();
    if (!session) {
        goto error;
    }

    if (libssh2_session_handshake(session, sock)) {
        libssh2_session_last_error(session, &errmsg, NULL, 0);
        fprintf(stderr, "ssh2_session_handshake failed: %s.\n", errmsg);
        goto error;
    }
    if (libssh2_userauth_password(session, user, passwd)) {
        libssh2_session_last_error(session, &errmsg, NULL, 0);
        fprintf(stderr, "ssh2_userauth_password failed: %s.\n",errmsg);
        goto error;
    }

    return session;
error:
    ssh_session_close(session);
    close(sock);

    return NULL;
}

void ssh_session_close(ssh_session_t* s)
{
    if (s) {
        libssh2_session_disconnect(s, "Normal Shutdown");
        libssh2_session_free(s);
    }

    // libssh2_exit();
}

sftp_session_t* sftp_session_new(ssh_session_t* s)
{
    return libssh2_sftp_init(s);
}

void sftp_session_free(sftp_session_t* s)
{
    libssh2_sftp_shutdown(s);
}

int sftp_send_file(sftp_session_t* s, const char* local, const char* remote)
{
    LIBSSH2_SFTP_HANDLE* hdl;
    struct stat f;
    FILE* fp;
    char* buf;
    int nr, nw = -1;

    if (stat(local, &f) != 0) {
        fprintf(stderr, "can't stat file: %s.\n", strerror(errno));
        return -1;
    }

    hdl = libssh2_sftp_open(s, remote,
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
            f.st_mode & 0777);
    if (!hdl) {
        fprintf(stderr, "ssh2_sftp_open failed: code %d.\n",
            (int)libssh2_sftp_last_error(s));
        // create dir when code = 2, todo
        return -1;
    }

    fp = fopen(local, "r");
    if (fp) {
        buf = malloc(READ_BUF_SIZE);

        while ((nr = fread(buf, 1, READ_BUF_SIZE, fp)) > 0) {
            nw = libssh2_sftp_write(hdl, buf, nr);

            if (nw != nr) {
                fprintf(stderr, "ssh2_sftp_write failed [%d/%d]: code %d.\n",
                    nw, nr, (int)libssh2_sftp_last_error(s));
                break;
            }
        }
        free(buf);
        fclose(fp);
    }

    libssh2_sftp_close_handle(hdl);

    return nw > 0 ? 0 : -1;
}

int sftp_make_dir(sftp_session_t* s, const char* remote)
{
    return libssh2_sftp_mkdir(s, remote, 0755);
}

int scp_send_file(ssh_session_t* s, const char* local, const char* remote)
{
    LIBSSH2_CHANNEL* ch;
    struct stat f;
    FILE* fp;
    char* errmsg;
    char* buf;
    int nr, nw = -1;

    if (stat(local, &f) != 0) {
        fprintf(stderr, "can't stat file: %s.\n", strerror(errno));
        return -1;
    }

    ch = libssh2_scp_send(s, remote,
            f.st_mode & 0777, (unsigned long)f.st_size);
    if (!ch) {
        libssh2_session_last_error(s, &errmsg, NULL, 0);
        fprintf(stderr, "ssh2_scp_send failed: %s.\n", errmsg);
        return -1;
    }

    fp = fopen(local, "r");
    if (fp) {
        buf = malloc(READ_BUF_SIZE);

        while ((nr = fread(buf, 1, READ_BUF_SIZE, fp)) > 0) {
            nw = libssh2_channel_write(ch, buf, nr);

            if (nw != nr) {
                libssh2_session_last_error(s, &errmsg, NULL, 0);
                fprintf(stderr, "ssh2_channel_write failed [%d/%d]: %s.\n", nw, nr, errmsg);
                break;
            }
        }
        free(buf);
        fclose(fp);
    }

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);

    return nw > 0 ? 0 : -1;
}
