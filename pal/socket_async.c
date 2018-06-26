// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

#include <stdbool.h>
#include <stdint.h>

// This file is OS-specific, and is identified by setting include directories
// in the project
#include "socket_async_os.h"

#include "socket_async.h"
#include "azure_c_shared_utility/xlogging.h"
#include "azure_c_shared_utility/tlsio_options.h"

// EXTRACT_IPV4 pulls the uint32_t IPv4 address out of an addrinfo struct
// This default definition handles lwIP.
#define EXTRACT_IPV4(ptr) ((struct sockaddr_in *) ptr->ai_addr)->sin_addr.s_addr


static int get_socket_errno(int file_descriptor)
{
    int sock_errno = 0;
    socklen_t optlen = sizeof(sock_errno);
    getsockopt(file_descriptor, SOL_SOCKET, SO_ERROR, &sock_errno, &optlen);
    return sock_errno;
}

int socket_async_get_option_caps()
{
    /* Codes_SRS_SOCKET_ASYNC_30_004: [ The  socket_async_get_option_caps  shall 
    return a bit-or of  TLSIO_OPTION_BIT  values to specify the implementation's 
    supported options. ]*/
    return TLSIO_OPTION_BIT_NONE;
}

/* Codes_SRS_SOCKET_ASYNC_30_006: [ The  socket_async_get_ipv4  shall 
return the IP V4 address of the supplied  hostname . ]*/
// Error handling is defered to the underlying getaddrinfo
// This call is synchronous.
uint32_t socket_async_get_ipv4(const char* hostname)
{
    struct addrinfo *addrInfo = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;
    int getAddrInfoResult;

    // This result is defaulted to zero because success only occurs
    // as finding an item while walking a list.
    uint32_t result = 0;

    //--------------------------------
    // Setup the hints address info structure
    // which is passed to the getaddrinfo() function
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    //--------------------------------
    // Call getaddrinfo(). If the call succeeds,
    // the result variable will hold a linked list
    // of addrinfo structures containing response
    // information
    getAddrInfoResult = getaddrinfo(hostname, NULL, &hints, &addrInfo);
    if (getAddrInfoResult == 0)
    {
        // If we find the AF_INET address, use it as the return value
        for (ptr = addrInfo; ptr != NULL; ptr = ptr->ai_next)
        {
            switch (ptr->ai_family)
            {
            case AF_INET:
                result = EXTRACT_IPV4(ptr);
                break;
            }
        }
        freeaddrinfo(addrInfo);
        if (result == 0)
        {
            LogInfo("Could not locate DNS entry for %s", hostname);
        }
    }
    else
    {
        // Not treated as an error because the common case is loss of network connectivity
        LogInfo("Failed getaddrinfo for %s: %d", hostname, getAddrInfoResult);
    }

    return result;
}

SOCKET_ASYNC_HANDLE socket_async_create(uint32_t serverIPv4, uint16_t port,
    bool is_UDP, SOCKET_ASYNC_OPTIONS_HANDLE options)
{
    SOCKET_ASYNC_HANDLE result;
    struct sockaddr_in sock_addr;

    /* Codes_SRS_SOCKET_ASYNC_30_013: [ The is_UDP parameter shall be true for a UDP connection, and false for TCP. ]*/
    int sock = socket(AF_INET, is_UDP ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (sock < 0)
    {
        /* Codes_SRS_SOCKET_ASYNC_30_010: [ If socket option creation fails, socket_async_create shall log an error and return SOCKET_ASYNC_INVALID_SOCKET. ]*/
        // An essentially impossible failure, not worth logging errno()
        LogError("create socket failed");
        result = SOCKET_ASYNC_INVALID_SOCKET;
    }
    else
    {
        bool setopt_ok;
        int setopt_return;
        // None of the currently defined options apply to UDP
        /* Codes_SRS_SOCKET_ASYNC_30_015: [ If is_UDP is true, the optional options parameter shall be ignored. ]*/
        if (!is_UDP)
        {
            if (options != NULL)
            {
                /* Codes_SRS_SOCKET_ASYNC_30_014: [ If the optional options parameter is non-NULL and is_UDP is false, and options->keep_alive is non-negative, socket_async_create shall set the socket options to the provided options values. ]*/
                if (options->keep_alive >= 0)
                {
                    int keepAlive = 1; //enable keepalive
                    setopt_ok = 0 == (setopt_return = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive)));
                    setopt_ok = setopt_ok && 0 == (setopt_return = setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, (void *)&(options->keep_idle), sizeof((options->keep_idle))));
                    setopt_ok = setopt_ok && 0 == (setopt_return = setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, (void *)&(options->keep_interval), sizeof((options->keep_interval))));
                    setopt_ok = setopt_ok && 0 == (setopt_return = setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, (void *)&(options->keep_count), sizeof((options->keep_count))));
                }
                else
                {
                    /* Codes_SRS_SOCKET_ASYNC_30_015: [ If the optional options parameter is non-NULL and is_UDP is false, and options->keep_alive is negative, socket_async_create not set the socket keep-alive options. ]*/
                    // < 0 means use system defaults, so do nothing
                    setopt_ok = true;
                    setopt_return = 0;
                }
            }
            else
            {
                /* Codes_SRS_SOCKET_ASYNC_30_017: [ If the optional options parameter is NULL and is_UDP is false, socket_async_create shall disable TCP keep-alive. ]*/
                int keepAlive = 0; //disable keepalive
                setopt_ok = 0 == setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepAlive, sizeof(keepAlive));
                setopt_return = 0;
            }
        }
        else
        {
            setopt_ok = true;
            setopt_return = 0;
        }

        // NB: On full-sized (multi-process) systems it would be necessary to use the SO_REUSEADDR option to 
        // grab the socket from any earlier (dying) invocations of the process and then deal with any 
        // residual junk in the connection stream. This doesn't happen with embedded, so it doesn't need
        // to be defended against.

        if (!setopt_ok)
        {
            /* Codes_SRS_SOCKET_ASYNC_30_020: [ If socket option setting fails, socket_async_create shall log an error and return SOCKET_ASYNC_INVALID_SOCKET. ]*/
            // setsockopt has no real possibility of failing due to the way it's being used here, so there's no need 
            // to spend memory trying to log the not-really-possible errno.
            LogError("setsockopt failed: %d", setopt_return);
            result = SOCKET_ASYNC_INVALID_SOCKET;
        }
        else
        {
            /* Codes_SRS_SOCKET_ASYNC_30_019: [ The socket returned shall be non-blocking. ]*/
            // When supplied with either F_GETFL and F_SETFL parameters, the fcntl function
            // does simple bit flips which have no error path, so it is not necessary to
            // check for errors. (Source checked for linux and lwIP).
            int originalFlags = fcntl(sock, F_GETFL, 0);
            int bind_ret;

            (void)fcntl(sock, F_SETFL, originalFlags | O_NONBLOCK);

            memset(&sock_addr, 0, sizeof(sock_addr));
            sock_addr.sin_family = AF_INET;
            sock_addr.sin_addr.s_addr = 0;
            sock_addr.sin_port = 0; // random local port

            bind_ret = bind(sock, (const struct sockaddr*)&sock_addr, sizeof(sock_addr));

            if (bind_ret != 0)
            {
                /* Codes_SRS_SOCKET_ASYNC_30_021: [ If socket binding fails, socket_async_create shall log an error and return SOCKET_ASYNC_INVALID_SOCKET. ]*/
                LogError("bind socket failed: %d", get_socket_errno(sock));
                result = SOCKET_ASYNC_INVALID_SOCKET;
            }
            else
            {
                int connect_ret;

                memset(&sock_addr, 0, sizeof(sock_addr));
                sock_addr.sin_family = AF_INET;
                /* Codes_SRS_SOCKET_ASYNC_30_011: [ The host_ipv4 parameter shall be the 32-bit IP V4 of the target server. ]*/
                sock_addr.sin_addr.s_addr = serverIPv4;
                /* Codes_SRS_SOCKET_ASYNC_30_012: [ The port parameter shall be the port number for the target server. ]*/
                sock_addr.sin_port = htons(port);

                connect_ret = connect(sock, (const struct sockaddr*)&sock_addr, sizeof(sock_addr));
                if (connect_ret == -1)
                {
                    int sockErr = get_socket_errno(sock);
                    // The underlying  lwip 2.0.3 getsockopt code swallows EINPROGRESS and returns
                    // conn->last_err instead, which is zero if connect succeeded
                    if (sockErr != EINPROGRESS && sockErr != 0)
                    {
                        /* Codes_SRS_SOCKET_ASYNC_30_022: [ If socket connection fails, socket_async_create shall log an error and return SOCKET_ASYNC_INVALID_SOCKET. ]*/
                        LogError("Socket connect failed, not EINPROGRESS: %d", sockErr);
                        result = SOCKET_ASYNC_INVALID_SOCKET;
                    }
                    else
                    {
                        // This is the normally expected code path for our non-blocking socket
                        /* Codes_SRS_SOCKET_ASYNC_30_018: [ On success, socket_async_create shall return the created and configured SOCKET_ASYNC_HANDLE. ]*/
                        result = sock;
                    }
                }
                else
                {
                    /* Codes_SRS_SOCKET_ASYNC_30_018: [ On success, socket_async_create shall return the created and configured SOCKET_ASYNC_HANDLE. ]*/
                    // This result would be a surprise because a non-blocking socket
                    // returns EINPROGRESS. But it could happen if this thread got
                    // blocked for a while by the system while the handshake proceeded,
                    // or for a UDP socket.
                    result = sock;
                }
            }
        }
    }

    return result;
}

int socket_async_is_create_complete(SOCKET_ASYNC_HANDLE sock, bool* is_complete)
{
    int result;
    if (is_complete == NULL)
    {
        /* Codes_SRS_SOCKET_ASYNC_30_026: [ If the is_complete parameter is NULL, socket_async_is_create_complete shall log an error and return FAILURE. ]*/
        LogError("is_complete is NULL");
        result = __FAILURE__;
    }
    else
    {
        struct timeval tv;
        int select_ret;

        // Check if the socket is ready to perform a write.
        fd_set writeset;
        fd_set errset;
        FD_ZERO(&writeset);
        FD_ZERO(&errset);
        FD_SET(sock, &writeset);
        FD_SET(sock, &errset);

        tv.tv_sec = 0;
        tv.tv_sec = 0;
        select_ret = select(sock + 1, NULL, &writeset, &errset, &tv);
        if (select_ret < 0)
        {
            /* Codes_SRS_SOCKET_ASYNC_30_028: [ On failure, the is_complete value shall be set to false and socket_async_create shall return FAILURE. ]*/
            LogError("Socket select failed: %d", get_socket_errno(sock));
            result = __FAILURE__;
        }
        else
        {
            if (FD_ISSET(sock, &errset))
            {
                /* Codes_SRS_SOCKET_ASYNC_30_028: [ On failure, the is_complete value shall be set to false and socket_async_create shall return FAILURE. ]*/
                LogError("Socket select errset non-empty: %d", get_socket_errno(sock));
                result = __FAILURE__;
            }
            else if (FD_ISSET(sock, &writeset))
            {
                /* Codes_SRS_SOCKET_ASYNC_30_027: [ On success, the is_complete value shall be set to the completion state and socket_async_create shall return 0. ]*/
                // Ready to read
                result = 0;
                *is_complete = true;
            }
            else
            {
                /* Codes_SRS_SOCKET_ASYNC_30_027: [ On success, the is_complete value shall be set to the completion state and socket_async_create shall return 0. ]*/
                // Not ready yet
                result = 0;
                *is_complete = false;
            }
        }
    }
    return result;
}

int socket_async_send(SOCKET_ASYNC_HANDLE sock, const void* buffer, size_t size, size_t* sent_count)
{
    int result;
    if (buffer == NULL || sent_count == NULL)
    {
        /* Codes_SRS_SOCKET_ASYNC_30_033: [ If the buffer parameter is NULL, socket_async_send shall log the error return FAILURE. ]*/
        /* Codes_SRS_SOCKET_ASYNC_30_034: [ If the sent_count parameter is NULL, socket_async_send shall log the error return FAILURE. ]*/
        LogError("bad parameter");
        result = __FAILURE__;
    }
    else if (size == 0)
    {
        /* Codes_SRS_SOCKET_ASYNC_30_073: [ If the size parameter is 0, socket_async_send shall set sent_count to 0 and return 0. ]*/
        // This behavior is not always defined by the underlying API, so we make it predictable here
        *sent_count = 0;
        result = 0;
    }
    else
    {
        ssize_t send_result = send(sock, buffer, size, 0);
        if (send_result < 0)
        {
            // On some systems it may be necessary to redefine "errno" in socket_async_os.h
            // to get proper behavior here.
            int sock_err = errno;
            if (sock_err == EAGAIN || sock_err == EWOULDBLOCK)
            {
                /* Codes_SRS_SOCKET_ASYNC_30_036: [ If the underlying socket is unable to accept any bytes for transmission because its buffer is full, socket_async_send shall return 0 and the sent_count parameter shall receive the value 0. ]*/
                // Nothing sent, try again later
                *sent_count = 0;
                result = 0;
            }
            else
            {
                /* Codes_SRS_SOCKET_ASYNC_30_037: [ If socket_async_send fails unexpectedly, socket_async_send shall log the error return FAILURE. ]*/
                // Something bad happened
                LogError("Unexpected send error: %d", sock_err);
                result = __FAILURE__;
            }
        }
        else
        {
            /* Codes_SRS_SOCKET_ASYNC_30_035: [ If the underlying socket accepts one or more bytes for transmission, socket_async_send shall return 0 and the sent_count parameter shall receive the number of bytes accepted for transmission. ]*/
            // Sent at least part of the message
            result = 0;
            *sent_count = (size_t)send_result;
        }
    }
    return result;
}

int socket_async_receive(SOCKET_ASYNC_HANDLE sock, void* buffer, size_t size, size_t* received_count)
{
    int result;
    if (buffer == NULL || received_count == NULL || size == 0)
    {
        /* Codes_SRS_SOCKET_ASYNC_30_052: [ If the buffer parameter is NULL, socket_async_receive shall log the error and return FAILURE. ]*/
        /* Codes_SRS_SOCKET_ASYNC_30_053: [ If the received_count parameter is NULL, socket_async_receive shall log the error and return FAILURE. ]*/
        /* Codes_SRS_SOCKET_ASYNC_30_072: [ If the size parameter is 0, socket_async_receive shall log an error and return FAILURE. ]*/
        LogError("bad parameter");
        result = __FAILURE__;
    }
    else
    {
        ssize_t recv_result = recv(sock, buffer, size, 0);
        if (recv_result < 0)
        {
            // On some systems it may be necessary to redefine "errno" in socket_async_os.h
            // to get proper behavior here.
            int sock_err = errno;
            if (sock_err == EAGAIN || sock_err == EWOULDBLOCK)
            {
                /* Codes_SRS_SOCKET_ASYNC_30_055: [ If the underlying socket has no received bytes available, socket_async_receive shall return 0 and the received_count parameter shall receive the value 0. ]*/
                // Nothing received, try again later
                *received_count = 0;
                result = 0;
            }
            else
            {
                /* Codes_SRS_SOCKET_ASYNC_30_056: [ If the underlying socket fails unexpectedly, socket_async_receive shall log the error and return FAILURE. ]*/
                // Something bad happened
                LogError("Unexpected recv error: %d", sock_err);
                result = __FAILURE__;
            }
        }
        else
        {
            /* Codes_SRS_SOCKET_ASYNC_30_054: [ On success, the underlying socket shall set one or more received bytes into buffer, socket_async_receive shall return 0, and the received_count parameter shall receive the number of bytes received into buffer. ]*/
            // Received some stuff
            *received_count = (size_t)recv_result;
            result = 0;
        }
    }
    return result;
}

void socket_async_destroy(SOCKET_ASYNC_HANDLE sock)
{
    /* Codes_SRS_SOCKET_ASYNC_30_071: [ socket_async_destroy shall call the underlying close method on the supplied socket. ]*/
    close(sock);
}


