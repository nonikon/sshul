#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "scp_session.h"

scp_session_t* scp_session_open(
    const char* host, int port, const char* user, const char* passwd)
{
    LIBSSH2_SESSION* session = NULL;
    struct sockaddr_in sin;
    int sock;

/* 
    if (libssh2_init(0) != 0) {
        return NULL;
    } */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        fprintf(stderr, "create socket error.\n");
        return NULL;
    }

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);

    // todo, resolve ip address
    if(connect(sock, (struct sockaddr*)&sin, sizeof(sin)) != 0) {
        fprintf(stderr, "connect [%s] failed.\n", host);
        goto error;
    }

    session = libssh2_session_init();
    if (!session) {
        goto error;
    }

    if (libssh2_session_handshake(session, sock)) {
        fprintf(stderr, "ssh2 handshake failed.\n");
        goto error;
    }
    if (libssh2_userauth_password(session, user, passwd)) {
        fprintf(stderr, "ssh2 auth password failed.\n");
        goto error;
    }

    return session;
error:
    scp_session_close(session);
    close(sock);

    return NULL;
}

int scp_session_send_file(
    scp_session_t* s, const char* local, const char* remote)
{
    LIBSSH2_CHANNEL* channel;
    struct stat f;
    FILE* fp;
    char buf[4096];
    int nr, nw = -1;

    if (stat(local, &f) != 0) {
        fprintf(stderr, "can't stat file [%s].\n", local);
        return -1;
    }

    channel = libssh2_scp_send(s, remote,
        f.st_mode & 0777, (unsigned long)f.st_size);
    if (!channel) {
        fprintf(stderr, "can't open channel for [%s].\n", local);
        return -1;
    }

    fp = fopen(local, "r");
    if (fp) {
        while ((nr = fread(buf, 1, sizeof(buf), fp)) > 0) {
            nw = libssh2_channel_write(channel, buf, nr);
            if (nw < 0) {
                fprintf(stderr, "send file [%s] failed.\n", local);
                break;
            }
            if (nw != nr) {
                fprintf(stderr, "warn: read %d, write %d.\n", nr, nw);
            }
        }
        fclose(fp);
    }

    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);
    libssh2_channel_free(channel);

    return nw > 0 ? 0 : -1;
}

void scp_session_close(scp_session_t* s)
{
    if (s) {
        libssh2_session_disconnect(s, "Normal Shutdown");
        libssh2_session_free(s);
    }

    // libssh2_exit();
}
