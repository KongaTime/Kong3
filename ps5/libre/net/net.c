/*
 * RPCS3 for PS5 - the network calls a title lacks.
 * Copyright (c) 2026 KongaTime
 * SPDX-License-Identifier: MIT
 *
 * Part of the PS5 libre layer (ps5/libre/README.md); link-title.sh binds
 * getaddrinfo, freeaddrinfo, gai_strerror, getnameinfo, accept4, the if_*
 * calls and in6addr_any to these libre_ ones. None is exported to a title.
 * Written from POSIX (RFC 3493 for the address calls) on libkernel's socket
 * calls, inet_pton and inet_ntop:
 *
 * - getaddrinfo and getnameinfo work with numeric addresses (and "localhost");
 *   a host name to look up is not known (EAI_NONAME): there is no resolver here.
 * - A title sees no network interfaces by name: the if_* calls find none.
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

const struct in6_addr libre_in6addr_any = IN6ADDR_ANY_INIT;

int libre_accept4(int fd, struct sockaddr *address, socklen_t *length, int flags)
{
   if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
   {
      errno = EINVAL;
      return -1;
   }
   const int accepted = accept(fd, address, length);
   if (accepted < 0)
      return -1;
   if (((flags & SOCK_CLOEXEC) && fcntl(accepted, F_SETFD, FD_CLOEXEC) != 0) ||
      ((flags & SOCK_NONBLOCK) && fcntl(accepted, F_SETFL, fcntl(accepted, F_GETFL) | O_NONBLOCK) != 0))
   {
      const int error = errno;
      close(accepted);
      errno = error;
      return -1;
   }
   return accepted;
}

static const char *const g_errors[] = {
   [0] = "Success",
   [EAI_AGAIN] = "Temporary failure in name resolution",
   [EAI_BADFLAGS] = "Invalid value for ai_flags",
   [EAI_FAIL] = "Non-recoverable failure in name resolution",
   [EAI_FAMILY] = "ai_family not supported",
   [EAI_MEMORY] = "Memory allocation failure",
   [EAI_NONAME] = "hostname nor servname provided, or not known",
   [EAI_SERVICE] = "servname not supported for ai_socktype",
   [EAI_SOCKTYPE] = "ai_socktype not supported",
   [EAI_SYSTEM] = "System error returned in errno",
   [EAI_BADHINTS] = "Invalid value for hints",
   [EAI_PROTOCOL] = "Resolved protocol is unknown",
   [EAI_OVERFLOW] = "Argument buffer overflow",
};

const char *libre_gai_strerror(int code)
{
   if (code >= 0 && code < (int)(sizeof g_errors / sizeof g_errors[0]) && g_errors[code])
      return g_errors[code];
   return "Unknown error";
}

void libre_freeaddrinfo(struct addrinfo *list)
{
   while (list)
   {
      struct addrinfo *next = list->ai_next;
      free(list->ai_canonname);
      free(list); /* its address is in the same block */
      list = next;
   }
}

/* One result: the entry and its address in one block */
static struct addrinfo *entry(int family, int type, int protocol, const void *host, unsigned short port)
{
   const size_t length = family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
   struct addrinfo *info = calloc(1, sizeof *info + length);
   if (!info)
      return NULL;
   info->ai_family = family;
   info->ai_socktype = type;
   info->ai_protocol = protocol;
   info->ai_addrlen = (socklen_t)length;
   info->ai_addr = (struct sockaddr *)(info + 1);
   if (family == AF_INET)
   {
      struct sockaddr_in *in = (struct sockaddr_in *)info->ai_addr;
      in->sin_len = sizeof *in;
      in->sin_family = AF_INET;
      in->sin_port = htons(port);
      memcpy(&in->sin_addr, host, sizeof in->sin_addr);
   }
   else
   {
      struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)info->ai_addr;
      in6->sin6_len = sizeof *in6;
      in6->sin6_family = AF_INET6;
      in6->sin6_port = htons(port);
      memcpy(&in6->sin6_addr, host, sizeof in6->sin6_addr);
   }
   return info;
}

int libre_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **result)
{
   const int flags = hints ? hints->ai_flags : 0;
   const int family = hints ? hints->ai_family : AF_UNSPEC;
   const int type = hints ? hints->ai_socktype : 0;
   const int protocol = hints ? hints->ai_protocol : 0;
   if (!result)
      return EAI_BADHINTS;
   *result = NULL;
   if (!node && !service)
      return EAI_NONAME;
   if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
      return EAI_FAMILY;
   if (type != 0 && type != SOCK_STREAM && type != SOCK_DGRAM && type != SOCK_RAW)
      return EAI_SOCKTYPE;

   /* The port: a number, or none */
   unsigned long port = 0;
   if (service && *service)
   {
      char *end;
      port = strtoul(service, &end, 10);
      if (*end || port > 65535)
         return EAI_SERVICE; /* a service by name: there is no services table */
   }

   /* The addresses: none given (any, or loopback), "localhost", or numeric */
   struct in_addr v4;
   struct in6_addr v6;
   int have4 = 0, have6 = 0;
   if (!node)
   {
      v4.s_addr = htonl((flags & AI_PASSIVE) ? INADDR_ANY : INADDR_LOOPBACK);
      v6 = (flags & AI_PASSIVE) ? (struct in6_addr)IN6ADDR_ANY_INIT : (struct in6_addr)IN6ADDR_LOOPBACK_INIT;
      have4 = have6 = 1;
   }
   else if (inet_pton(AF_INET, node, &v4) == 1)
      have4 = 1;
   else if (inet_pton(AF_INET6, node, &v6) == 1)
      have6 = 1;
   else if (!(flags & AI_NUMERICHOST) && strcasecmp(node, "localhost") == 0)
   {
      v4.s_addr = htonl(INADDR_LOOPBACK);
      v6 = (struct in6_addr)IN6ADDR_LOOPBACK_INIT;
      have4 = have6 = 1;
   }
   else
      return EAI_NONAME; /* a name to look up: no resolver */
   if (family == AF_INET)
      have6 = 0;
   if (family == AF_INET6)
      have4 = 0;
   if (!have4 && !have6)
      return EAI_FAMILY;

   /* Each address with each kind of socket asked for (stream and datagram when
    * none is), IPv4 first */
   int kinds[2][2] = {{SOCK_STREAM, IPPROTO_TCP}, {SOCK_DGRAM, IPPROTO_UDP}};
   int kind_count = 2;
   if (type)
   {
      kinds[0][0] = type;
      kinds[0][1] = protocol ? protocol : type == SOCK_STREAM ? IPPROTO_TCP : type == SOCK_DGRAM ? IPPROTO_UDP : 0;
      kind_count = 1;
   }
   else if (protocol)
   {
      kinds[0][1] = kinds[1][1] = protocol;
   }
   struct addrinfo *first = NULL, **last = &first;
   for (int f = 0; f < 2; f++)
   {
      if (f == 0 ? !have4 : !have6)
         continue;
      for (int k = 0; k < kind_count; k++)
      {
         struct addrinfo *info = entry(f == 0 ? AF_INET : AF_INET6, kinds[k][0], kinds[k][1],
            f == 0 ? (const void *)&v4 : (const void *)&v6, (unsigned short)port);
         if (!info)
         {
            libre_freeaddrinfo(first);
            return EAI_MEMORY;
         }
         *last = info;
         last = &info->ai_next;
      }
   }
   if (first && (flags & AI_CANONNAME) && node && !(first->ai_canonname = strdup(node)))
   {
      libre_freeaddrinfo(first);
      return EAI_MEMORY;
   }
   *result = first;
   return 0;
}

int libre_getnameinfo(const struct sockaddr *address, socklen_t length, char *host, socklen_t host_size, char *service,
   socklen_t service_size, int flags)
{
   if (!address)
      return EAI_FAIL;
   const void *where;
   unsigned short port;
   if (address->sa_family == AF_INET && length >= sizeof(struct sockaddr_in))
   {
      where = &((const struct sockaddr_in *)address)->sin_addr;
      port = ntohs(((const struct sockaddr_in *)address)->sin_port);
   }
   else if (address->sa_family == AF_INET6 && length >= sizeof(struct sockaddr_in6))
   {
      where = &((const struct sockaddr_in6 *)address)->sin6_addr;
      port = ntohs(((const struct sockaddr_in6 *)address)->sin6_port);
   }
   else
      return EAI_FAMILY;

   if (host && host_size)
   {
      /* Only the number: no resolver to name it */
      if (flags & NI_NAMEREQD)
         return EAI_NONAME;
      if (!inet_ntop(address->sa_family, where, host, host_size))
         return EAI_OVERFLOW;
   }
   if (service && service_size)
   {
      const int written = snprintf(service, service_size, "%u", port);
      if (written < 0 || (socklen_t)written >= service_size)
         return EAI_OVERFLOW;
   }
   return 0;
}

unsigned int libre_if_nametoindex(const char *name)
{
   (void)name;
   errno = ENXIO;
   return 0;
}

char *libre_if_indextoname(unsigned int index, char *name)
{
   (void)index, (void)name;
   errno = ENXIO;
   return NULL;
}

struct if_nameindex *libre_if_nameindex(void)
{
   /* No interface: only the list's end */
   return calloc(1, sizeof(struct if_nameindex));
}

void libre_if_freenameindex(struct if_nameindex *list)
{
   free(list);
}
