#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#include "ssh_session.h"

#define READ_BUF_SIZE   8192

#ifndef _WIN32
#define closesocket     close
#endif

static libssh2_socket_t connect_tcp_server(const char* host, int port)
{
    libssh2_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;

    if (sock < 0) {
        fprintf(stderr, "create socket failed: %s.\n",
            strerror(errno));
        return -1;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    /* TODO, resolve ip address */
    sin.sin_addr.s_addr = inet_addr(host);

    if(connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        fprintf(stderr, "connect [%s] failed: %s.\n",
            host, strerror(errno));
        closesocket(sock);
        return -1;
    }

    return sock;
}

ssh_t* ssh_session_open(const char* host, int port,
        const char* user, const char* passwd)
{
    libssh2_socket_t sock = connect_tcp_server(host, port);
    ssh_t* s;
    char* msg;

    if (sock < 0) return NULL;

    s = libssh2_session_init();

    if (!s) goto error;

    if (libssh2_session_handshake(s, sock)) {
        libssh2_session_last_error(s, &msg, NULL, 0);
        fprintf(stderr, "ssh2_session_handshake failed: %s.\n", msg);
        goto error;
    }

    if (libssh2_userauth_password(s, user, passwd)) {
        libssh2_session_last_error(s, &msg, NULL, 0);
        fprintf(stderr, "ssh2_userauth_password failed: %s.\n",msg);
        goto error;
    }

    return s;
error:
    ssh_session_close(s);
    closesocket(sock);

    return NULL;
}

void ssh_session_close(ssh_t* s)
{
    if (s) {
        libssh2_session_disconnect(s, "Normal Shutdown");
        libssh2_session_free(s);
    }
}

sftp_t* sftp_session_new(ssh_t* s)
{
    return libssh2_sftp_init(s);
}

void sftp_session_free(sftp_t* s)
{
    libssh2_sftp_shutdown(s);
}

int sftp_send_file(sftp_t* s, const char* local, const char* remote,
        int mode, unsigned long size)
{
    LIBSSH2_SFTP_HANDLE* hdl;
    char* buf;
    char* pos;
    FILE* fp;
    int retried = 0;
    int nr, nw = -1;

retry:
    hdl = libssh2_sftp_open(s, remote,
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode & 0777);

    if (!hdl) {
        if (retried) return -1;

        if (libssh2_sftp_last_error(s) != LIBSSH2_FX_NO_SUCH_FILE) {
            fprintf(stderr, "ssh2_sftp_open failed: code %d, retried = %d.\n",
                (int)libssh2_sftp_last_error(s), retried);
            return -1;
        }

        pos = strrchr(remote, '/');

        if (pos <= remote) {
            fprintf(stderr, "can't create remote file: %s.\n", remote);
            return -1;
        }

        /* /home/user1/file1 -> /home/user1/ */
        buf = malloc(pos - remote + 2);
        memcpy(buf, remote, pos - remote + 1);
        buf[pos - remote + 1] = '\0';

        pos = strchr(buf + 1, '/');

        /* create missing dirs */
        for (LIBSSH2_SFTP_ATTRIBUTES attrs; pos; pos = strchr(pos + 1, '/')) {
            pos[0] = '\0';

            if (libssh2_sftp_lstat(s, buf, &attrs) < 0) {
                /* not exist, create it */
                if (libssh2_sftp_mkdir(s, buf, 0755) < 0) {
                    fprintf(stderr, "create remote dir [%s] failed: code %d.\n",
                        buf, (int)libssh2_sftp_last_error(s));
                    break;
                }
            } else if (!LIBSSH2_SFTP_S_ISDIR(attrs.permissions)) {
                /* exist but not a directory */
                fprintf(stderr, "create remote dir [%s] failed: file exist.\n", buf);
                break;
            }
    
            pos[0] = '/';
        }

        free(buf);

        retried = 1;
        goto retry;
    }

    fp = fopen(local, "rb");

    if (fp) {
        buf = malloc(READ_BUF_SIZE);

        do {
            pos = buf;
            nr = fread(buf, 1, READ_BUF_SIZE, fp);

            if (nr <= 0) break;

            do {
                nw = libssh2_sftp_write(hdl, pos, nr);

                if (nw < 0) {
                    fprintf(stderr, "ssh2_sftp_write failed [%d/%d]: code %d.\n",
                        nw, nr, (int)libssh2_sftp_last_error(s));
                    break;
                }
                nr -= nw;
                pos += nw;
            }
            while (nr > 0);
        }
        while (1);

        free(buf);
        fclose(fp);
    }

    libssh2_sftp_close_handle(hdl);

    return nw > 0 ? 0 : -1;
}

int scp_send_file(ssh_t* s, const char* local, const char* remote,
        int mode, unsigned long size)
{
    LIBSSH2_CHANNEL* ch;
    char* msg;
    char* buf;
    char* pos;
    FILE* fp;
    int nr, nw = -1;

    ch = libssh2_scp_send(s, remote, mode & 0777, size);

    if (!ch) {
        libssh2_session_last_error(s, &msg, NULL, 0);
        fprintf(stderr, "ssh2_scp_send failed: %s.\n", msg);
        return -1;
    }

    fp = fopen(local, "rb");

    if (fp) {
        buf = malloc(READ_BUF_SIZE);

        do {
            pos = buf;
            nr = fread(buf, 1, READ_BUF_SIZE, fp);

            if (nr <= 0) break;

            do {
                nw = libssh2_channel_write(ch, pos, nr);

                if (nw < 0) {
                    libssh2_session_last_error(s, &msg, NULL, 0);
                    fprintf(stderr, "ssh2_channel_write failed [%d/%d]: %s.\n",
                        nw, nr, msg);
                    break;
                }
                nr -= nw;
                pos += nw;
            }
            while (nr > 0);
        }
        while (1);

        free(buf);
        fclose(fp);
    }

    libssh2_channel_send_eof(ch);
    libssh2_channel_wait_eof(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);

    return nw > 0 ? 0 : -1;
}
