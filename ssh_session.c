#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include "ssh_session.h"

#define GENERIC_BUF_SIZE    16384

static char* generic_buf;

static libssh2_socket_t connect_tcp_server(const char* host, int port)
{
    char portstr[16];
    struct addrinfo hints;
    struct addrinfo* res = NULL;
    libssh2_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        fprintf(stderr, "create socket failed (%s).\n", strerror(errno));
        return -1;
    }

    sprintf(portstr, "%d", port);
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = 0;
    hints.ai_flags = 0;

    if (getaddrinfo(host, portstr, &hints, &res) != 0) {
        fprintf(stderr, "getaddrinfo failed (%s).\n", strerror(errno));
        goto error;
    }

    if(connect(sock, res->ai_addr, res->ai_addrlen) != 0) {
        fprintf(stderr, "connect (%s) failed (%s).\n", host, strerror(errno));
        goto error;
    }

    freeaddrinfo(res);
    return sock;
error:
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    if (res) {
        freeaddrinfo(res);
    }
    return -1;
}

ssh_t* ssh_session_open(const char* host, int port, int compress,
        const char* user, const char* passwd)
{
    libssh2_socket_t sock = connect_tcp_server(host, port);
    ssh_t* s;
    char* msg;

    if (sock < 0) {
        return NULL;
    }

    s = libssh2_session_init();
    if (!s) {
        fprintf(stderr, "ssh2_session_init failed.\n");
        goto error;
    }
    if (compress) {
        libssh2_session_flag(s, LIBSSH2_FLAG_COMPRESS, 1);
    }
    // libssh2_session_set_blocking(s, 1);

    if (libssh2_session_handshake(s, sock)) {
        libssh2_session_last_error(s, &msg, NULL, 0);
        fprintf(stderr, "ssh2_session_handshake failed (%s).\n", msg);
        goto error;
    }

    if (libssh2_userauth_password(s, user, passwd)) {
        libssh2_session_last_error(s, &msg, NULL, 0);
        fprintf(stderr, "ssh2_userauth_password failed (%s).\n", msg);
        goto error;
    }

// #ifndef NDEBUG
//     libssh2_trace(s, LIBSSH2_TRACE_SCP | LIBSSH2_TRACE_SFTP);
// #endif
    if (!generic_buf) {
        generic_buf = malloc(GENERIC_BUF_SIZE);
    }
    return s;
error:
    ssh_session_close(s);
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    return NULL;
}

void ssh_session_close(ssh_t* s)
{
    if (s) {
        libssh2_session_disconnect(s, "Normal Shutdown");
        libssh2_session_free(s);
    }
    free(generic_buf);
    generic_buf = NULL;
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
        int mode, int exists, time_t mtime, uint64_t size)
{
    LIBSSH2_SFTP_HANDLE* hdl;
    FILE* fp;
    int ret = -1;

    /* local file is a directory */
    if (LIBSSH2_SFTP_S_ISDIR(mode)) {
        if (exists || libssh2_sftp_mkdir(s, remote, mode & 0777) == 0) {
            return 0;
        }
        fprintf(stdout, "create remote dir failed (%d)", (int)libssh2_sftp_last_error(s));
        return -1;
    }
#ifndef _WIN32
    /* local file is a symlink */
    if (LIBSSH2_SFTP_S_ISLNK(mode)) {
        /* unlink remote file if exists */
        if (exists && libssh2_sftp_unlink(s, remote) < 0) {
            fprintf(stdout, "unlink remote file failed (%d)", (int)libssh2_sftp_last_error(s));
        } else {
            int nread = readlink(local, generic_buf, GENERIC_BUF_SIZE - 1);
            /* create remote link file */
            if (nread > 0) {
                generic_buf[nread] = 0;
                if (libssh2_sftp_symlink(s, generic_buf, (char*)remote) == 0) {
                    return 0;
                }
                fprintf(stdout, "symlink remote file failed (%d)",
                    (int)libssh2_sftp_last_error(s));
                return -1;
            }
        }
        return -1;
    }
#endif
    if (!LIBSSH2_SFTP_S_ISREG(mode)) {
        fprintf(stdout, "unsupported file type (%d)", mode & LIBSSH2_SFTP_S_IFMT);
        return -1;
    }

    hdl = libssh2_sftp_open(s, remote,
            LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, mode & 0777);
    if (!hdl) {
        fprintf(stdout, "open remote file failed (%d)", (int)libssh2_sftp_last_error(s));
        return -1;
    }

    fp = fopen(local, "rb");

    if (fp) {
        char* pos;
        int nread, nwrite;
        int percent = 0;
        uint64_t cursize = 0;

        while (1) {
            pos = generic_buf;
            nread = fread(generic_buf, 1, GENERIC_BUF_SIZE, fp);
            if (nread > 0) {
                cursize += nread;
                nwrite = (int)(cursize * 100 / size);
                if (nwrite != percent) {
                    percent = nwrite;
                    fprintf(stdout, "\033[u%3d%%", percent);
                    fflush(stdout);
                }
                do {
                    nwrite = libssh2_sftp_write(hdl, pos, nread);
                    if (nwrite < 0) {
                        fprintf(stdout, "write remote file failed [%d/%d] (%d)",
                            nwrite, nread, (int)libssh2_sftp_last_error(s));
                        goto out;
                    }
                    nread -= nwrite;
                    pos += nwrite;
                }
                while (nread > 0);

            } else if (nread == 0) {
                ret = 0;
                break; /* eof */
            } else {
                fprintf(stdout, "read local file failed (%s)", strerror(errno));
                break;
            }
        }
out:
        fclose(fp);
    } else {
        fprintf(stdout, "open local file failed (%s)", strerror(errno));
    }

    libssh2_sftp_close_handle(hdl);
    return ret;
}

int sftp_recv_file(sftp_t* s, const char* local, const char* remote,
        int mode, int exists, time_t mtime, uint64_t size)
{
    LIBSSH2_SFTP_HANDLE* hdl;
    FILE* fp;
    int ret = -1;

    /* remote file is a directory */
    if (LIBSSH2_SFTP_S_ISDIR(mode)) {
#ifdef _WIN32
        if (exists || CreateDirectoryA(local, NULL)) {
            return 0;
        }
        fprintf(stdout, "create local dir failed (%d)", GetLastError());
#else
        if (exists || mkdir(local, mode & 0777) == 0) {
            return 0;
        }
        fprintf(stdout, "create local dir failed (%s)", strerror(errno));
#endif
        return -1;
    }
    /* remote file is a symlink */
    if (LIBSSH2_SFTP_S_ISLNK(mode)) {
        /* unlink local file if exists */
#ifdef _WIN32
        if (exists && !DeleteFileA(local)) {
            fprintf(stdout, "unlink local file failed (%d)", GetLastError());
#else
        if (exists && unlink(local) < 0) {
            fprintf(stdout, "unlink local file failed (%s)", strerror(errno));
#endif
        } else {
            int nread = libssh2_sftp_readlink(s, remote, generic_buf, GENERIC_BUF_SIZE - 1);
            /* create local link file */
            if (nread > 0) {
#ifdef _WIN32
                /* just write a normal file, TODO */
                fp = fopen(local, "wb");
                if (fp) {
                    fwrite(generic_buf, 1, nread, fp);
                    fclose(fp);
                    return 0;
                }
#else
                generic_buf[nread] = 0;
                if (symlink(generic_buf, local) == 0) {
                    return 0;
                }
#endif
                fprintf(stdout, "symlink local file failed (%s)", strerror(errno));
                return -1;
            }
        }
        return -1;
    }
    if (!LIBSSH2_SFTP_S_ISREG(mode)) {
        fprintf(stdout, "unsupported file type (%d)", mode & LIBSSH2_SFTP_S_IFMT);
        return -1;
    }

#ifdef _WIN32
    fp = fopen(local, "wb");
#else
    if (1) {
        int fd = open(local, O_WRONLY | O_CREAT | O_TRUNC, mode & 0777);
        if (fd < 0) {
            fprintf(stdout, "open local file failed (%s)", strerror(errno));
            return -1;
        }
        fp = fdopen(fd, "wb");
    }
#endif
    if (!fp) {
        fprintf(stdout, "open local file (%s) failed", local);
        // close(fd);
        return -1;
    }

    hdl = libssh2_sftp_open(s, remote, LIBSSH2_FXF_READ, 0);

    if (hdl) {
        int nread, nwrite;
        int percent = 0;
        uint64_t cursize = 0;

        while (1) {
            nread = libssh2_sftp_read(hdl, generic_buf, GENERIC_BUF_SIZE);
            if (nread > 0) {
                cursize += nread;
                nwrite = (int)(cursize * 100 / size);
                if (nwrite != percent) {
                    percent = nwrite;
                    fprintf(stdout, "\033[u%3d%%", percent);
                    fflush(stdout);
                }
                nwrite = fwrite(generic_buf, 1, nread, fp);
                if (nwrite != nread) {
                    fprintf(stdout, "write local file failed [%d/%d] (%s)",
                        nwrite, nread, strerror(errno));
                    break;
                }
            } else if (nread == 0) {
                ret = 0;
                break; /* eof */
            } else {
                fprintf(stdout, "read remote file failed (%d)",
                    (int)libssh2_sftp_last_error(s));
                break;
            }
        }

        libssh2_sftp_close_handle(hdl);
    } else {
        fprintf(stdout, "open remote file failed (%d)", (int)libssh2_sftp_last_error(s));
    }

    fclose(fp);
    return ret;
}
