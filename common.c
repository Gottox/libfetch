/*-
 * Copyright (c) 1998 Dag-Erling Co�dan Sm�rgrav
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <com_err.h>
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "fetch.h"
#include "common.h"


/*** Local data **************************************************************/

/*
 * Error messages for resolver errors
 */
static struct fetcherr _netdb_errlist[] = {
    { HOST_NOT_FOUND,	FETCH_RESOLV,	"Host not found" },
    { TRY_AGAIN,	FETCH_TEMP,	"Transient resolver failure" },
    { NO_RECOVERY,	FETCH_RESOLV,	"Non-recoverable resolver failure" },
    { NO_DATA,		FETCH_RESOLV,	"No address record" },
    { -1,		FETCH_UNKNOWN,	"Unknown resolver error" }
};

static int com_err_initialized;

/*** Error-reporting functions ***********************************************/

/*
 * Initialize the common error library
 */
static void
_fetch_init_com_err(void)
{
    initialize_ftch_error_table();
    com_err_initialized = 1;
}

/*
 * Map error code to string
 */
static int
_fetch_finderr(struct fetcherr *p, int e)
{
    int i;
    for (i = 0; p[i].num != -1; i++)
	if (p[i].num == e)
	    break;
    return i;
}

/*
 * Set error code
 */
void
_fetch_seterr(struct fetcherr *p, int e)
{
    int n;
    
    if (!com_err_initialized)
	_fetch_init_com_err();

    n = _fetch_finderr(p, e);
    fetchLastErrCode = p[n].cat;
    com_err("libfetch", fetchLastErrCode, "(%03d %s)", e, p[n].string);
}

/*
 * Set error code according to errno
 */
void
_fetch_syserr(void)
{
    int e;
    e = errno;
    
    if (!com_err_initialized)
	_fetch_init_com_err();

    switch (errno) {
    case 0:
	fetchLastErrCode = FETCH_OK;
	break;
    case EPERM:
    case EACCES:
    case EROFS:
    case EAUTH:
    case ENEEDAUTH:
	fetchLastErrCode = FETCH_AUTH;
	break;
    case ENOENT:
    case EISDIR: /* XXX */
	fetchLastErrCode = FETCH_UNAVAIL;
	break;
    case ENOMEM:
	fetchLastErrCode = FETCH_MEMORY;
	break;
    case EBUSY:
    case EAGAIN:	
	fetchLastErrCode = FETCH_TEMP;
	break;
    case EEXIST:
	fetchLastErrCode = FETCH_EXISTS;
	break;
    case ENOSPC:
	fetchLastErrCode = FETCH_FULL;
	break;
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ENETDOWN:
    case ENETUNREACH:
    case ENETRESET:
    case EHOSTUNREACH:
	fetchLastErrCode = FETCH_NETWORK;
	break;
    case ECONNABORTED:
    case ECONNRESET:
	fetchLastErrCode = FETCH_ABORT;
	break;
    case ETIMEDOUT:
	fetchLastErrCode = FETCH_TIMEOUT;
	break;
    case ECONNREFUSED:
    case EHOSTDOWN:
	fetchLastErrCode = FETCH_DOWN;
	break;
    default:
	fetchLastErrCode = FETCH_UNKNOWN;
    }
    com_err("libfetch", fetchLastErrCode, "(%03d %s)", e, strerror(e));
}


/*
 * Emit status message
 */
int
_fetch_info(char *fmt, ...)
{
    va_list ap;
    char *s;
    
    if (!com_err_initialized)
	_fetch_init_com_err();

    va_start(ap, fmt);
    vasprintf(&s, fmt, ap);
    va_end(ap);

    if (s == NULL) {
	com_err("libfetch", FETCH_MEMORY, "");
	return -1;
    } else {
	com_err("libfetch", FETCH_VERBOSE, "%s", s);
	free(s);
	return 0;
    }
}


/*** Network-related utility functions ***************************************/

/*
 * Establish a TCP connection to the specified port on the specified host.
 */
int
_fetch_connect(char *host, int port, int verbose)
{
    struct sockaddr_in sin;
    struct hostent *he;
    int sd;

#ifndef NDEBUG
    fprintf(stderr, "\033[1m---> %s:%d\033[m\n", host, port);
#endif

    if (verbose)
	_fetch_info("looking up %s", host);
    
    /* look up host name */
    if ((he = gethostbyname(host)) == NULL) {
	_netdb_seterr(h_errno);
	return -1;
    }

    if (verbose)
	_fetch_info("connecting to %s:%d", host, port);
    
    /* set up socket address structure */
    bzero(&sin, sizeof(sin));
    bcopy(he->h_addr, (char *)&sin.sin_addr, he->h_length);
    sin.sin_family = he->h_addrtype;
    sin.sin_port = htons(port);

    /* try to connect */
    if ((sd = socket(sin.sin_family, SOCK_STREAM, IPPROTO_TCP)) == -1) {
	_fetch_syserr();
	return -1;
    }
    if (connect(sd, (struct sockaddr *)&sin, sizeof sin) == -1) {
	_fetch_syserr();
	close(sd);
	return -1;
    }

    return sd;
}


/*
 * Read a line of text from a socket w/ timeout
 */
#define MIN_BUF_SIZE 1024

int
_fetch_getln(int fd, char **buf, size_t *size, size_t *len)
{
    struct timeval now, timeout, wait;
    fd_set readfds;
    int r;
    char c;
    
    if (*buf == NULL) {
	if ((*buf = malloc(MIN_BUF_SIZE)) == NULL) {
	    errno = ENOMEM;
	    return -1;
	}
	*size = MIN_BUF_SIZE;
    }

    **buf = '\0';
    *len = 0;

    if (fetchTimeout) {
	gettimeofday(&timeout, NULL);
	timeout.tv_sec += fetchTimeout;
	FD_ZERO(&readfds);
    }
    
    do {
	if (fetchTimeout) {
	    FD_SET(fd, &readfds);
	    gettimeofday(&now, NULL);
	    wait.tv_sec = timeout.tv_sec - now.tv_sec;
	    wait.tv_usec = timeout.tv_usec - now.tv_usec;
	    if (wait.tv_usec < 0) {
		wait.tv_usec += 1000000;
		wait.tv_sec--;
	    }
	    if (wait.tv_sec < 0) {
		errno = ETIMEDOUT;
		return -1;
	    }
	    r = select(fd+1, &readfds, NULL, NULL, &wait);
	    if (r == -1) {
		if (errno == EINTR)
		    continue;
		/* EBADF or EINVAL: shouldn't happen */
		return -1;
	    }
	    if (!FD_ISSET(fd, &readfds))
		continue;
	}
	r = read(fd, &c, 1);
	if (r == 0)
	    break;
	if (r == -1) {
	    if (errno == EINTR)
		continue;
	    /* any other error is bad news */
	    return -1;
	}
	(*buf)[*len] = c;
	*len += 1;
	if (*len == *size) {
	    char *tmp;
	    
	    if ((tmp = realloc(*buf, *size * 2 + 1)) == NULL) {
		errno = ENOMEM;
		return -1;
	    }
	    *buf = tmp;
	    *size = *size * 2 + 1;
	}
    } while (c != '\n');
    
    return 0;
}


/*** Directory-related utility functions *************************************/

int
_fetch_add_entry(struct url_ent **p, int *size, int *len,
		 char *name, struct url_stat *stat)
{
    struct url_ent *tmp;

    if (*p == NULL) {
#define INITIAL_SIZE 8
	if ((*p = malloc(INITIAL_SIZE * sizeof **p)) == NULL) {
	    errno = ENOMEM;
	    _fetch_syserr();
	    return -1;
	}
	*size = INITIAL_SIZE;
	*len = 0;
#undef INITIAL_SIZE
    }
    
    if (*len >= *size - 1) {
	tmp = realloc(*p, *size * 2 * sizeof **p);
	if (tmp == NULL) {
	    errno = ENOMEM;
	    _fetch_syserr();
	    return -1;
	}
	*size *= 2;
	*p = tmp;
    }

    tmp = *p + *len;
    snprintf(tmp->name, MAXPATHLEN, "%s", name);
    bcopy(stat, &tmp->stat, sizeof *stat);

    (*len)++;
    (++tmp)->name[0] = 0;

    return 0;
}