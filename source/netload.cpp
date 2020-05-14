#include "netload.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <zlib.h>
#include <cstdio>
#include <mutex>

namespace netloader {

    namespace {

#define ENTRY_ARGBUFSIZE 0x400
#define DIRECTORY_SEPARATOR_CHAR '/'

        struct LaunchArgs {
            char *dst;
            uint32_t buf[ENTRY_ARGBUFSIZE / sizeof(uint32_t)];
            struct in_addr nxlink_host;

            size_t add(const char *arg) {
                size_t len = strlen(arg) + 1;
                if (uintptr_t(this->dst + len) >= uintptr_t(this->buf + sizeof(this->buf)))
                    return len; // Overflow

                this->buf[0]++;
                strcpy(this->dst, arg);
                this->dst += len;
                return len;
            }
        };

        LaunchArgs g_args;

        char path_buffer[FS_MAX_PATH];

        inline char *getExtension(const char *str) {
            const char *p;
            for (p = str + strlen(str); p >= str && *p != '.'; p--)
                ;
            return (char *)p;
        }

#define PING_ENABLED 1

#define ZLIB_CHUNK (16 * 1024)
#define FILE_BUFFER_SIZE (128 * 1024)

        int netloader_listenfd = -1;
        static int netloader_datafd = -1;
#if PING_ENABLED
        int netloader_udpfd = -1;
#endif
        u8 in[ZLIB_CHUNK];
        u8 out[ZLIB_CHUNK];

        std::mutex g_mutex;

        static volatile bool netloader_exitflag = 0;
        static volatile bool netloader_activated = 0, netloader_launchapp = 0;
        static volatile size_t netloader_filelen, netloader_filetotal;
        static volatile char netloader_errortext[1024];

        bool getExit() {
            std::scoped_lock lk(g_mutex);
            return netloader_exitflag;
        }

        //---------------------------------------------------------------------------------
        static void netloader_error(const char *func, int err) {
            //---------------------------------------------------------------------------------
            if (getExit())
                return;

            std::scoped_lock lk(g_mutex);
            if (netloader_errortext[0] == 0) {
                memset((char *)netloader_errortext, 0, sizeof(netloader_errortext));
                snprintf((char *)netloader_errortext, sizeof(netloader_errortext) - 1, "%s: err=%d\n %s\n", func, err, strerror(errno));
            }
        }

        //---------------------------------------------------------------------------------
        static void netloader_socket_error(const char *func) {
            //---------------------------------------------------------------------------------
            int errcode;
            errcode = errno;
            netloader_error(func, errcode);
        }

        //---------------------------------------------------------------------------------
        void shutdownSocket(int socket) {
            //---------------------------------------------------------------------------------
            close(socket);
        }

        static const char DIRECTORY_THIS[] = ".";
        static const char DIRECTORY_PARENT[] = "..";

        //---------------------------------------------------------------------------------
        static bool isDirectorySeparator(int c) {
            //---------------------------------------------------------------------------------
            return c == DIRECTORY_SEPARATOR_CHAR;
        }

        //---------------------------------------------------------------------------------
        static void sanitisePath(char *path) {
            //---------------------------------------------------------------------------------
            std::string tmpPath = path;
            tmpPath[0] = 0;

            char *dirStart = path;
            char *curPath = tmpPath.data();

            dirStart = path;

            while (isDirectorySeparator(dirStart[0]))
                dirStart++;

            do {
                char *dirEnd = strchr(dirStart, DIRECTORY_SEPARATOR_CHAR);
                if (dirEnd) {
                    dirEnd++;
                    if (!strncmp(DIRECTORY_PARENT, dirStart, strlen(DIRECTORY_PARENT))) {
                        /* move back one directory */
                        size_t pathlen = tmpPath.size();
                        if (tmpPath[pathlen - 1] == DIRECTORY_SEPARATOR_CHAR)
                            tmpPath[pathlen - 1] = 0;
                        char *prev = strrchr(tmpPath.c_str(), DIRECTORY_SEPARATOR_CHAR);
                        if (prev) {
                            curPath = prev + 1;
                        } else {
                            curPath = tmpPath.data();
                        }

                        dirStart = dirEnd;
                    } else if (!strncmp(DIRECTORY_THIS, dirStart, strlen(DIRECTORY_THIS))) {
                        /* strip this entry */
                        dirStart = dirEnd;
                    } else {
                        size_t dirSize = dirEnd - dirStart;
                        strncpy(curPath, dirStart, dirSize);
                        curPath[dirSize] = 0;
                        curPath += dirSize;
                        dirStart += dirSize;
                    }
                } else {
                    strcpy(curPath, dirStart);
                    dirStart += strlen(dirStart);
                }
            } while (dirStart[0]);

            strcpy(path, tmpPath.c_str());
        }

        //---------------------------------------------------------------------------------
        static int set_socket_nonblocking(int sock) {
            //---------------------------------------------------------------------------------

            int flags = fcntl(sock, F_GETFL);

            if (flags == -1)
                return -1;

            int rc = fcntl(sock, F_SETFL, flags | O_NONBLOCK);

            if (rc != 0)
                return -1;

            return 0;
        }

        //---------------------------------------------------------------------------------
        static int recvall(int sock, void *buffer, int size, int flags) {
        //---------------------------------------------------------------------------------
            u8 *ptr = static_cast<u8*>(buffer); // cast for pointer arithmetic
            int len, sizeleft = size;
            bool blockflag = 0;

            while (sizeleft) {

                len = recv(sock, ptr, sizeleft, flags);

                if (len == 0) {
                    size = 0;
                    break;
                };

                if (len != -1) {
                    sizeleft -= len;
                    ptr += len;
                } else {
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        netloader_socket_error("recv");
                        break;
                    } else {
                        blockflag = 1;
                    }

                    if (blockflag && getExit())
                        return 0;
                }
            }
            return size;
        }

        //---------------------------------------------------------------------------------
        static int sendall(int sock, void *buffer, int size, int flags) {
        //---------------------------------------------------------------------------------
            u8 *ptr = static_cast<u8*>(buffer); // cast for pointer arithmetic
            int len, sizeleft = size;
            bool blockflag = 0;

            while (sizeleft) {

                len = send(sock, ptr, sizeleft, flags);

                if (len == 0) {
                    size = 0;
                    break;
                };

                if (len != -1) {
                    sizeleft -= len;
                    ptr += len;
                } else {
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        netloader_socket_error("recv");
                        break;
                    } else {
                        blockflag = 1;
                    }

                    if (blockflag && getExit())
                        return 0;
                }
            }
            return size;
        }

        //---------------------------------------------------------------------------------
        static int decompress(int sock, FILE *fh, size_t filesize) {
            //---------------------------------------------------------------------------------
            int ret;
            unsigned have;
            z_stream strm;
            uint32_t chunksize = 0;

            /* allocate inflate state */
            strm.zalloc = Z_NULL;
            strm.zfree = Z_NULL;
            strm.opaque = Z_NULL;
            strm.avail_in = 0;
            strm.next_in = Z_NULL;
            ret = inflateInit(&strm);
            if (ret != Z_OK) {
                netloader_error("inflateInit failed.", ret);
                return ret;
            }

            size_t total = 0;
            /* decompress until deflate stream ends or end of file */
            do {
                if (getExit()) {
                    ret = Z_DATA_ERROR;
                    break;
                }

                int len = recvall(sock, &chunksize, sizeof(chunksize), 0);

                if (len != 4) {
                    (void)inflateEnd(&strm);
                    netloader_error("Error getting chunk size", len);
                    return Z_DATA_ERROR;
                }

                if (chunksize > sizeof(in)) {
                    (void)inflateEnd(&strm);
                    netloader_error("Invalid chunk size", chunksize);
                    return Z_DATA_ERROR;
                }

                strm.avail_in = recvall(sock, in, chunksize, 0);

                if (strm.avail_in == 0) {
                    (void)inflateEnd(&strm);
                    netloader_error("remote closed socket.", 0);
                    return Z_DATA_ERROR;
                }

                strm.next_in = in;

                /* run inflate() on input until output buffer not full */
                do {
                    strm.avail_out = ZLIB_CHUNK;
                    strm.next_out = out;
                    ret = inflate(&strm, Z_NO_FLUSH);

                    switch (ret) {

                        case Z_NEED_DICT:
                            ret = Z_DATA_ERROR; /* and fall through */

                        case Z_DATA_ERROR:
                        case Z_MEM_ERROR:
                        case Z_STREAM_ERROR:
                            (void)inflateEnd(&strm);
                            netloader_error("inflate error", ret);
                            return ret;
                    }

                    have = ZLIB_CHUNK - strm.avail_out;

                    if (fwrite(out, 1, have, fh) != have || ferror(fh)) {
                        (void)inflateEnd(&strm);
                        netloader_error("file write error", 0);
                        return Z_ERRNO;
                    }

                    total += have;
                    std::scoped_lock lk(g_mutex);
                    netloader_filetotal = total;
                    //printf("%zu (%zd%%)",total, (100 * total) / filesize);
                } while (strm.avail_out == 0);

                /* done when inflate() says it's done */
            } while (ret != Z_STREAM_END);

            /* clean up and return */
            (void)inflateEnd(&strm);
            return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
        }

        //---------------------------------------------------------------------------------
        int loadnro(int sock, struct in_addr remote) {
            //---------------------------------------------------------------------------------
            int len, namelen, filelen;
            char filepath[PATH_MAX + 1];
            len = recvall(sock, &namelen, sizeof(namelen), 0);

            if (len != 4) {
                netloader_error("Error getting name length", errno);
                return -1;
            }

            if (namelen >= int(sizeof(filepath) - 1)) {
                netloader_error("File-path length is too large", errno);
                return -1;
            }

            len = recvall(sock, filepath, namelen, 0);

            if (len != namelen) {
                netloader_error("Error getting file-path", errno);
                return -1;
            }

            filepath[namelen] = 0;

            len = recvall(sock, &filelen, sizeof(filelen), 0);

            if (len != 4) {
                netloader_error("Error getting file length", errno);
                return -1;
            }

            {
                std::scoped_lock lk(g_mutex);
                netloader_filelen = filelen;
            }

            int response = 0;

            sanitisePath(filepath);

            snprintf(path_buffer, sizeof(path_buffer) - 1, "/switch/.overlays/%s", filepath);
            // make sure it's terminated
            path_buffer[sizeof(path_buffer) - 1] = 0;
            strncpy(filepath, path_buffer, sizeof(filepath) - 1); // menuEntryLoad() below will overwrite path_buffer, so copy path_buffer to filepath and use that instead.
            filepath[sizeof(filepath) - 1] = 0;

            LaunchArgs *ad = &g_args;
            g_args.dst = (char *)&g_args.buf[1];
            g_args.nxlink_host = remote;

            const char *ext = getExtension(path_buffer);
            /* We only support overlays and no fileassoc. */
            if (!ext || strcasecmp(ext, ".ovl") != 0) {
                netloader_error("unsupported file type", 0);
                return -1;
            }

            g_args.add(path_buffer);

            FILE *file = NULL;

            file = fopen(filepath, "wb");
            if (file == NULL) {
                netloader_error("open", errno);
                response = -1;
            } else {
                fseek(file, filelen, SEEK_SET);
                fwrite("\0", 1, 1, file);
                fseek(file, 0, SEEK_SET);
            }

            send(sock, &response, sizeof(response), 0);

            char *writebuffer = NULL;
            if (response == 0) {
                writebuffer = (char *)malloc(FILE_BUFFER_SIZE);
                if (writebuffer == NULL) {
                    netloader_error("Failed to allocate memory", ENOMEM);
                    response = -1;
                } else {
                    memset(writebuffer, 0, FILE_BUFFER_SIZE);
                    setvbuf(file, writebuffer, _IOFBF, FILE_BUFFER_SIZE);
                }
            }

            if (response == 0) {
                //printf("transferring %s\n%d bytes.\n", filepath, filelen);

                if (decompress(sock, file, filelen) == Z_OK) {
                    int netloaded_cmdlen = 0;
                    len = sendall(sock, &response, sizeof(response), 0);

                    if (len != sizeof(response)) {
                        netloader_error("Error sending response", errno);
                        response = -1;
                    }

                    //printf("\ntransferring command line\n");

                    if (response == 0) {
                        len = recvall(sock, &netloaded_cmdlen, sizeof(netloaded_cmdlen), 0);

                        if (len != 4) {
                            netloader_error("Error getting netloaded_cmdlen", errno);
                            response = -1;
                        }
                    }

                    if (response == 0) {
                        if (uintptr_t(g_args.dst + netloaded_cmdlen) >= uintptr_t(g_args.buf + sizeof(g_args.buf)))
                            netloaded_cmdlen = (uintptr_t)g_args.buf + sizeof(g_args.buf) - 1 - (uintptr_t)g_args.dst;

                        len = recvall(sock, g_args.dst, netloaded_cmdlen, 0);

                        if (len != netloaded_cmdlen) {
                            netloader_error("Error getting args", errno);
                            response = -1;
                        }
                    }

                    if (response == 0) {
                        while (netloaded_cmdlen) {
                            size_t len = strlen(g_args.dst) + 1;
                            ad->dst += len;
                            ad->buf[0]++;
                            netloaded_cmdlen -= len;
                        }
                    }

                } else {
                    response = -1;
                }
            }

            if (file) {
                fflush(file);
                fclose(file);
            }
            if (response == -1)
                unlink(filepath);
            free(writebuffer);

            return response;
        }

        //---------------------------------------------------------------------------------
        int activate() {
            //---------------------------------------------------------------------------------
            struct sockaddr_in serv_addr;

            memset(&serv_addr, 0, sizeof(serv_addr));
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            serv_addr.sin_port = htons(NXLINK_SERVER_PORT);

#if PING_ENABLED
            // create udp socket for broadcast ping
            netloader_udpfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (netloader_udpfd < 0) {
                netloader_socket_error("udp socket");
                return -1;
            }

            if (bind(netloader_udpfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                netloader_socket_error("bind udp socket");
                return -1;
            }

            if (set_socket_nonblocking(netloader_udpfd) == -1) {
                netloader_socket_error("listen fcntl");
                return -1;
            }
#endif
            // create listening socket on all addresses on NXLINK_SERVER_PORT

            netloader_listenfd = socket(AF_INET, SOCK_STREAM, 0);
            if (netloader_listenfd < 0) {
                netloader_socket_error("socket");
                return -1;
            }

            uint32_t tmpval = 1;
            int rc = setsockopt(netloader_listenfd, SOL_SOCKET, SO_REUSEADDR, (const char *)&tmpval, sizeof(tmpval));
            if (rc != 0) {
                netloader_socket_error("setsockopt");
                return -1;
            }

            rc = bind(netloader_listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            if (rc != 0) {
                netloader_socket_error("bind");
                return -1;
            }

            if (set_socket_nonblocking(netloader_listenfd) == -1) {
                netloader_socket_error("listen fcntl");
                return -1;
            }

            rc = listen(netloader_listenfd, 10);
            if (rc != 0) {
                netloader_socket_error("listen");
                return -1;
            }

            return 0;
        }

        //---------------------------------------------------------------------------------
        int deactivate() {
            //---------------------------------------------------------------------------------
            // close all remaining sockets and allow mainloop to return to main menu
            if (netloader_listenfd >= 0) {
                shutdownSocket(netloader_listenfd);
                netloader_listenfd = -1;
            }

            if (netloader_datafd >= 0) {
                shutdownSocket(netloader_datafd);
                netloader_datafd = -1;
            }

#if PING_ENABLED
            if (netloader_udpfd >= 0) {
                shutdownSocket(netloader_udpfd);
                netloader_udpfd = -1;
            }
#endif

            return 0;
        }

        int netloader_loop(struct sockaddr_in * sa_remote) {

#if PING_ENABLED
            char recvbuf[256];
            socklen_t fromlen = sizeof(struct sockaddr_in);

            int len = recvfrom(netloader_udpfd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)sa_remote, &fromlen);

            if (len != -1) {
                if (strncmp(recvbuf, "nxboot", strlen("nxboot")) == 0) {
                    sa_remote->sin_family = AF_INET;
                    sa_remote->sin_port = htons(NXLINK_CLIENT_PORT);
                    sendto(netloader_udpfd, "bootnx", strlen("bootnx"), 0, (struct sockaddr *)sa_remote, sizeof(struct sockaddr_in));
                }
            }
#endif

            if (netloader_listenfd >= 0 && netloader_datafd < 0) {
                socklen_t addrlen = sizeof(struct sockaddr_in);
                netloader_datafd = accept(netloader_listenfd, (struct sockaddr *)sa_remote, &addrlen);
                if (netloader_datafd < 0) {
                    if (errno != EWOULDBLOCK && errno != EAGAIN) {
                        netloader_error("accept", errno);
                        return -1;
                    }
                } else {
                    if (set_socket_nonblocking(netloader_datafd) == -1) {
                        netloader_socket_error("set_socket_nonblocking(netloader_datafd)");
                        return -1;
                    }

                    close(netloader_listenfd);
                    netloader_listenfd = -1;
                    return 1;
                }
            }

            return 0;
        }
    }

    void getState(State *state) {
        if (state == NULL)
            return;
            
        std::scoped_lock lk(g_mutex);

        state->activated = netloader_activated;
        state->launch_app = netloader_launchapp;

        state->transferring = (netloader_datafd >= 0 && netloader_filelen);
        state->sock_connected = netloader_datafd >= 0;
        state->filelen = netloader_filelen;
        state->filetotal = netloader_filetotal;

        memset(state->errormsg, 0, sizeof(state->errormsg));
        if (netloader_errortext[0]) {
            strncpy(state->errormsg, (char *)netloader_errortext, sizeof(state->errormsg) - 1);
            memset((char *)netloader_errortext, 0, sizeof(netloader_errortext));
        }
    }

    void signalExit() {
        std::scoped_lock lk(g_mutex);
        netloader_exitflag = 1;
    }

    void task(void *arg) {
        int ret = 0;
        struct sockaddr_in sa_remote;

        {
            std::scoped_lock lk(g_mutex);
            netloader_exitflag = 0;
            netloader_activated = 0;
            netloader_launchapp = 0;
            netloader_filelen = 0;
            netloader_filetotal = 0;
        }

        if (activate() == 0) {
            std::scoped_lock lk(g_mutex);
            netloader_activated = 1;
        } else {
            deactivate();
            return;
        }

        while ((ret = netloader_loop(&sa_remote)) == 0 && !getExit()) {
            svcSleepThread(100'000'000);
        }

        if (ret == 1 && !getExit()) {
            int result = loadnro(netloader_datafd, sa_remote.sin_addr);
            if (result == 0) {
                ret = 1;
            } else {
                ret = -1;
            }
        }

        deactivate();
        std::scoped_lock lk(g_mutex);
        if (ret == 1 && !netloader_exitflag)
            netloader_launchapp = 1; //Access netloader_exitflag directly since the mutex is already locked.
        netloader_exitflag = 0;
        netloader_activated = 0;
    }

    static char *init_args(char *dst, size_t dst_maxsize, u32 *in_args, size_t size) {
        size_t tmplen;
        u32 argi;
        char *in_argdata = (char *)&in_args[1];

        size -= sizeof(u32);

        for (argi = 0; argi < in_args[0]; argi++) {
            if (size < 2)
                break;

            tmplen = __builtin_strnlen(in_argdata, size - 1);

            if (tmplen + 3 > dst_maxsize)
                break;

            if (dst_maxsize < 3)
                break;

            *dst++ = '"';
            dst_maxsize--;

            strncpy(dst, in_argdata, tmplen);
            in_argdata += tmplen + 1;
            size -= tmplen + 1;
            dst += tmplen;
            dst_maxsize -= tmplen;

            *dst++ = '"';
            dst_maxsize--;

            if (argi + 1 < in_args[0]) {
                *dst++ = ' ';
                dst_maxsize--;
            }
        }
        return dst;
    }

    static char argBuf[ENTRY_ARGBUFSIZE];

    Result setNext() {
        memset(argBuf, 0, sizeof(argBuf));

        uint32_t remote = g_args.nxlink_host.s_addr;

        if (remote) {
            char nxlinked[17];
            sprintf(nxlinked, "%08x_NXLINK_", remote);
            g_args.add(nxlinked);
        }

        init_args(argBuf, sizeof(argBuf) - 1, g_args.buf, sizeof(g_args.buf));

        std::strcat(argBuf, " --skipCombo");

        return envSetNextLoad(path_buffer, argBuf);
    }

}
