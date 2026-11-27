// dummied source from enigma2
#ifndef __wrappers_h
#define __wrappers_h

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include <vector>
#include <string>
#include <sys/select.h>

/* All socket helpers below work against an absolute deadline in
 * milliseconds of the monotonic clock (monotonicMs() + timeout): the HLS
 * explorer runs on the enigma2 main thread, so every step of a fetch -
 * connect, TLS handshake, request, response - has to share one time budget
 * instead of starting a fresh timeout per call (or per byte). */
long long monotonicMs();
/* 1 = ready, 0 = deadline reached, -1 = error */
int waitFd(int fd, bool forWrite, long long deadlineMs);
/* >0 bytes read, 0 = end of stream, -1 = error, -2 = deadline reached */
ssize_t readSome(SSL *ssl, int fd, void *buf, size_t count, long long deadlineMs);
ssize_t writeAll(SSL *ssl, int fd, const void *buf, size_t count, long long deadlineMs);
int Select(int maxfd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
/* the returned socket is non-blocking, use it with the functions above */
int Connect(const char *hostname, int port, long long deadlineMs);
int SSLConnect(const char *hostname, int fd, SSL **ssl, SSL_CTX **ctx, long long deadlineMs);

#endif
