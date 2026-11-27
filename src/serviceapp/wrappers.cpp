// dummied source from enigma2
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

#include "wrappers.h"

/* a single address may not take the whole budget of the fetch: an
 * unreachable IPv6 address first in the list would leave no time for the
 * IPv4 one after it */
#define CONNECT_TIMEOUT_PER_ADDRESS_MS 5000

int Select(int maxfd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	int retval;
	fd_set rset, wset, xset;
	timeval interval;

	/* make a backup of all fd_set's and timeval struct */
	if (readfds) rset = *readfds;
	if (writefds) wset = *writefds;
	if (exceptfds) xset = *exceptfds;
	if (timeout)
	{
		interval = *timeout;
	}
	else
	{
		/* make gcc happy... */
		timerclear(&interval);
	}

	while (1)
	{
		retval = ::select(maxfd, readfds, writefds, exceptfds, timeout);

		if (retval < 0)
		{
			/* restore the backup before we continue */
			if (readfds) *readfds = rset;
			if (writefds) *writefds = wset;
			if (exceptfds) *exceptfds = xset;
			if (timeout) *timeout = interval;
			if (errno == EINTR) continue;
			fprintf(stderr,"Select] error: %m");
			break;
		}

		break;
	}
	return retval;
}

long long monotonicMs()
{
	/* monotonic: a clock set by NTP while a fetch runs must not stretch or
	 * cut its deadline */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int waitFd(int fd, bool forWrite, long long deadlineMs)
{
	while (1)
	{
		long long remaining = deadlineMs - monotonicMs();
		if (remaining <= 0)
			return 0;
		fd_set set;
		FD_ZERO(&set);
		FD_SET(fd, &set);
		struct timeval timeout;
		timeout.tv_sec = remaining / 1000;
		timeout.tv_usec = (remaining % 1000) * 1000;
		int result = ::select(fd + 1, forWrite ? NULL : &set, forWrite ? &set : NULL, NULL, &timeout);
		if (result < 0)
		{
			/* a signal: wait again for what is left of the time */
			if (errno == EINTR)
				continue;
			fprintf(stderr, "[waitFd] error: %m\n");
			return -1;
		}
		/* 0: the remaining time has run out, the next round returns 0 */
		if (result > 0)
			return 1;
	}
}

ssize_t readSome(SSL *ssl, int fd, void *buf, size_t count, long long deadlineMs)
{
	if (count > INT_MAX)
		count = INT_MAX;
	while (1)
	{
		bool waitWrite = false;
		if (ssl != NULL)
		{
			/* read first, wait only when OpenSSL asks for it: SSL_read hands
			 * out the decrypted data it has buffered already (SSL_pending()),
			 * a select() before it would wait for socket data that has been
			 * consumed long ago and sit out the whole deadline */
			ERR_clear_error();
			int retval = SSL_read(ssl, buf, (int)count);
			if (retval > 0)
				return retval;
			int error = SSL_get_error(ssl, retval);
			if (error == SSL_ERROR_ZERO_RETURN)
				return 0;
			/* OpenSSL 1.1: the server closed the TCP connection without a
			 * TLS close_notify - common, and the end of a Connection: close
			 * response */
			if (error == SSL_ERROR_SYSCALL && retval == 0 && ERR_peek_error() == 0)
				return 0;
			if (error == SSL_ERROR_WANT_WRITE)
				waitWrite = true;
			else if (error != SSL_ERROR_WANT_READ)
			{
				fprintf(stderr, "[readSome(SSL)] error: %s\n", ERR_error_string(ERR_get_error(), NULL));
				return -1;
			}
		}
		else
		{
			ssize_t retval = ::read(fd, buf, count);
			if (retval >= 0)
				return retval;
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
			{
				fprintf(stderr, "[readSome] error: %m\n");
				return -1;
			}
		}
		int ready = waitFd(fd, waitWrite, deadlineMs);
		if (ready == 0)
			return -2;
		if (ready < 0)
			return -1;
	}
}

ssize_t writeAll(SSL *ssl, int fd, const void *buf, size_t count, long long deadlineMs)
{
	const char *ptr = (const char*)buf;
	size_t handledcount = 0;
	while (handledcount < count)
	{
		bool waitRead = false;
		size_t chunk = count - handledcount;
		if (chunk > INT_MAX)
			chunk = INT_MAX;
		if (ssl != NULL)
		{
			ERR_clear_error();
			/* after WANT_* the same buffer and length are passed again, as
			 * OpenSSL requires for a non-blocking SSL_write */
			int retval = SSL_write(ssl, &ptr[handledcount], (int)chunk);
			if (retval > 0)
			{
				handledcount += retval;
				continue;
			}
			int error = SSL_get_error(ssl, retval);
			if (error == SSL_ERROR_WANT_READ)
				waitRead = true;
			else if (error != SSL_ERROR_WANT_WRITE)
			{
				fprintf(stderr, "[writeAll(SSL)] error: %s\n", ERR_error_string(ERR_get_error(), NULL));
				return -1;
			}
		}
		else
		{
#ifdef MSG_NOSIGNAL
			/* a server that has closed the connection must not kill the
			 * process with SIGPIPE */
			ssize_t retval = ::send(fd, &ptr[handledcount], chunk, MSG_NOSIGNAL);
#else
			ssize_t retval = ::write(fd, &ptr[handledcount], chunk);
#endif
			if (retval > 0)
			{
				handledcount += retval;
				continue;
			}
			if (retval == 0)
				return -1;
			if (errno == EINTR)
				continue;
			if (errno != EAGAIN && errno != EWOULDBLOCK)
			{
				fprintf(stderr, "[writeAll] error: %m\n");
				return -1;
			}
		}
		int ready = waitFd(fd, !waitRead, deadlineMs);
		if (ready == 0)
			return -2;
		if (ready < 0)
			return -1;
	}
	return handledcount;
}

int Connect(const char *hostname, int port, long long deadlineMs)
{
	int sd = -1;
	std::vector<struct addrinfo *> addresses;
	struct addrinfo *info = NULL;
	struct addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* both ipv4 and ipv6 */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0; /* any */
#ifdef AI_ADDRCONFIG
	hints.ai_flags = AI_ADDRCONFIG; /* only return ipv6 if we have an ipv6 address ourselves, and ipv4 if we have an ipv4 address ourselves */
#else
	hints.ai_flags = 0; /* we have only IPV4 support, if AI_ADDRCONFIG is not available */
#endif
	char portstring[15];
	/* this is suboptimal, but the alternative would require us to mess with the memberdata of an 'abstract' addressinfo struct */
	snprintf(portstring, sizeof(portstring), "%d", port);
	/* getaddrinfo itself has no timeout: it is bounded by the resolver
	 * settings (resolv.conf timeout/attempts), not by deadlineMs */
	if (getaddrinfo(hostname, portstring, &hints, &info) || !info) return -1;
	struct addrinfo *ptr = info;
	while (ptr)
	{
		addresses.push_back(ptr);
		ptr = ptr->ai_next;
	}

	for (unsigned int i = 0; i < addresses.size(); i++)
	{
		long long now = monotonicMs();
		if (now >= deadlineMs)
		{
			fprintf(stderr, "[Connect] %s: timed out\n", hostname);
			break;
		}
		long long addressDeadline = now + CONNECT_TIMEOUT_PER_ADDRESS_MS;
		if (addressDeadline > deadlineMs)
			addressDeadline = deadlineMs;
		sd = ::socket(addresses[i]->ai_family, addresses[i]->ai_socktype, addresses[i]->ai_protocol);
		/* e.g. no IPv6 in the kernel: the next (IPv4) address may still work */
		if (sd < 0) continue;
		int flags;
		/* non-blocking for the whole connection: connect, TLS handshake,
		 * read and write all wait in select() against the deadline */
		if ((flags = fcntl(sd, F_GETFL, 0)) < 0 || fcntl(sd, F_SETFL, flags | O_NONBLOCK) < 0)
		{
			::close(sd);
			sd = -1;
			continue;
		}
		int connectresult = ::connect(sd, addresses[i]->ai_addr, addresses[i]->ai_addrlen);
		if (connectresult < 0 && (errno == EINTR || errno == EINPROGRESS))
		{
			/* non-blocking connect: wait until the socket is writable, then
			 * it is connected when SO_ERROR is 0 */
			int error = 0;
			socklen_t len = sizeof(error);
			if (waitFd(sd, true, addressDeadline) > 0 &&
				getsockopt(sd, SOL_SOCKET, SO_ERROR, &error, &len) == 0 &&
				!error)
				connectresult = 0;
		}
		if (connectresult < 0)
		{
			::close(sd);
			sd = -1;
			continue;
		}
#ifdef SO_NOSIGPIPE
		int val = 1;
		setsockopt(sd, SOL_SOCKET, SO_NOSIGPIPE, (char*)&val, sizeof(val));
#endif
		/* we have a working connection */
		break;
	}
	freeaddrinfo(info);
	return sd;
}

int SSLConnect(const char *hostname, int fd, SSL **ssl, SSL_CTX **ctx, long long deadlineMs)
{
	*ctx = SSL_CTX_new(SSLv23_client_method());
	if (*ctx == NULL)
	{
		fprintf(stderr, "Error in SSL_CTX_new:\n");
		ERR_print_errors_fp(stderr);
		return -1;
	}
	SSL_CTX_set_default_verify_paths(*ctx);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
	/* OpenSSL 3: a server closing without close_notify is the normal end
	 * of a Connection: close response, not an error */
	SSL_CTX_set_options(*ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
	*ssl = SSL_new(*ctx);
	if (*ssl == NULL)
	{
		fprintf(stderr, "Error in SSL_new:\n");
		ERR_print_errors_fp(stderr);
		SSL_CTX_free(*ctx);
		return -1;
	}
	struct addrinfo hints = { 0 }, *ai = NULL;
	hints.ai_flags = AI_NUMERICHOST;
	if (getaddrinfo(hostname, NULL, &hints, &ai) != 0)
	{
		if (SSL_set_tlsext_host_name(*ssl, hostname) != 1)
		{
			fprintf(stderr, "Error in SSL_set_tlsext_host_name:\n");
			ERR_print_errors_fp(stderr);
			SSL_free(*ssl);
			SSL_CTX_free(*ctx);
			return -1;
		}
	}
	else
	{
		freeaddrinfo(ai);
	}
	if (SSL_set_fd(*ssl, fd) == 0)
	{
		fprintf(stderr, "Error in SSL_set_fd:\n");
		ERR_print_errors_fp(stderr);
		SSL_free(*ssl);
		SSL_CTX_free(*ctx);
		return -1;
	}
	/* The certificate is not verified on purpose: this connection only reads
	 * the HLS master playlist for the variant list, the player opens the
	 * stream itself without verification (FFmpeg tls_verify=0), and many
	 * IPTV servers have self-signed or expired certificates - verifying here
	 * would only hide the variants of streams that play anyway. */
	while (1)
	{
		ERR_clear_error();
		int ret = SSL_connect(*ssl);
		if (ret == 1)
			break;
		int error = SSL_get_error(*ssl, ret);
		int ready = -1;
		/* the socket is non-blocking: a server that stalls the handshake
		 * would otherwise block the caller (the enigma2 main thread) for
		 * as long as the TCP connection lives */
		if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
			ready = waitFd(fd, error == SSL_ERROR_WANT_WRITE, deadlineMs);
		if (ready <= 0)
		{
			if (ready == 0)
				fprintf(stderr, "Error in SSL_connect: timed out\n");
			else
				fprintf(stderr, "Error in SSL_connect: %s\n",
						ERR_error_string(ERR_get_error(), NULL));
			SSL_free(*ssl);
			SSL_CTX_free(*ctx);
			return -1;
		}
	}
	return 0;
}
