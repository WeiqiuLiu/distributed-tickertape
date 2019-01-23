
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <assert.h>
#include "minirpc.h"
#include <rpc/svc.h>
#include <sys/queue.h>

/* Stuff needed to interface with rpcgen output */
int _rpcsvcdirty;
typedef long xdrlong_t;	    /* On some sysstems is int or u_int32_t */

time_t elapsed;
int want_timer;


static u_int32_t xid;
static int client_sock = -1;
struct rpc {
  const struct sockaddr_in *dest;
  int msglen;
  char *msg;
  int lastsent;
  int expire;
  LIST_ENTRY (rpc) link;
};
static LIST_HEAD (rpclist_t, rpc) rpclist = { NULL };

int
mkudpsock (int port)
{
  int udpsock;
  struct sockaddr_in sin;

  if (port < 0 || port > 0xffff) {
    fprintf (stderr, "invalid UDP port number %d\n", port);
    exit (1);
  }

  udpsock = socket (AF_INET, SOCK_DGRAM, 0);
  if (udpsock < 0) {
    perror ("socket");
    exit (1);
  }

  bzero (&sin, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = htonl (INADDR_ANY);
  sin.sin_port = htons (port);
  if (bind (udpsock, (struct sockaddr *) &sin, sizeof (sin)) < 0) {
    perror ("bind");
    exit (1);
  }

  return udpsock;
}

static void
make_async (int fd)
{
  int n;
  if ((n = fcntl (fd, F_GETFL)) != -1)
    fcntl (fd, F_SETFL, n | O_NONBLOCK);
}

static void
tvsub (struct timeval *a_minus_b, const struct timeval *a,
       const struct timeval *b)
{
  a_minus_b->tv_sec = a->tv_sec - b->tv_sec;
  if (a->tv_usec >= b->tv_usec)
    a_minus_b->tv_usec = a->tv_usec - b->tv_usec;
  else {
    a_minus_b->tv_usec = 1000000 + a->tv_usec - b->tv_usec;
    a_minus_b->tv_sec--;
  }
}

static bool_t
xdr_putint (XDR *xdrs, u_int32_t val)
{
  xdrlong_t l = val;
  return XDR_PUTLONG (xdrs, &l);
}

static void
rpc_init (void)
{
  if (client_sock < 0) {
    srand48(getpid());
    client_sock = mkudpsock (0);
    make_async (client_sock);
    xid = time (NULL) ^ (getpid () << 16);
  }
}

static void
rpc_free (struct rpc *rp)
{
  LIST_REMOVE (rp, link);
  free (rp->msg);
  free (rp);
}

static void
rpc_alloc (const struct sockaddr_in *dest, void *msg, int msglen)
{
  struct rpc *rp;

  assert (msglen > 8);
  if (!(rp = malloc (sizeof (*rp))) || !(rp->msg = malloc (msglen))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  rp->dest = dest;
  rp->msglen = msglen;
  memcpy (rp->msg, msg, msglen);
  rp->lastsent = elapsed;
  rp->expire = elapsed + 7;
  LIST_INSERT_HEAD (&rpclist, rp, link);
  if (drand48() < 0.5)
    usleep(1000);
  sendto (client_sock, rp->msg, rp->msglen, 0,
	  (struct sockaddr *) rp->dest, sizeof (*rp->dest));
}

int
rpc_marshal (void *msgbuf, size_t msgmax,
	     u_int32_t prog, u_int32_t vers, u_int32_t proc,
	     xdrproc_t argxdr, void *arg)
{
  XDR x;
  rpc_init ();
  xdrmem_create (&x, msgbuf, msgmax, XDR_ENCODE);
  if (!xdr_putint (&x, xid++)
      || !xdr_putint (&x, CALL)
      || !xdr_putint (&x, RPC_MSG_VERSION)
      || !xdr_putint (&x, prog)
      || !xdr_putint (&x, vers)
      || !xdr_putint (&x, proc)
      || !xdr_putint (&x, AUTH_NONE) /* cred */
      || !xdr_putint (&x, 0)
      || !xdr_putint (&x, AUTH_NONE) /* verf */
      || !xdr_putint (&x, 0)
      || !(*argxdr) (&x, arg)) {
    xdr_destroy (&x);
    fprintf (stderr, "failed to marshal arguments\n");
    return -1;
  }

  msgmax = xdr_getpos (&x);
  xdr_destroy (&x);
  return msgmax;
}


void
rpc_send (struct sockaddr_in *dest,
	  u_int32_t prog, u_int32_t vers, u_int32_t proc,
	  xdrproc_t argxdr, void *arg)
{
  char msgbuf[1500];
  int n = rpc_marshal (msgbuf, sizeof (msgbuf),
		       prog, vers, proc, argxdr, arg);
  if (n < 0)
    return;

  rpc_alloc (dest, msgbuf, n);
}

void
rpc_broadcast (struct sockaddr_in **dest,
	       u_int32_t prog, u_int32_t vers, u_int32_t proc,
	       xdrproc_t argxdr, void *arg)
{
  char msgbuf[1500];
  int n = rpc_marshal (msgbuf, sizeof (msgbuf),
		       prog, vers, proc, argxdr, arg);
  if (n < 0)
    return;

  while (*dest)
    rpc_alloc (*dest++, msgbuf, n);
}

static void
rpc_getreplies (void)
{
  static char reply[] = { 0, 0, 0, 1 };
  unsigned char buf[8192];
  int n;
  struct sockaddr_in sin;
  socklen_t sinlen = sizeof (sin);
  struct rpc *rp;

  bzero (&sin, sizeof (sin));
  sin.sin_family = AF_INET;
  while ((n = recvfrom (client_sock, buf, sizeof (buf), 0,
			(struct sockaddr *) &sin, &sinlen)) >= 0) {
    if (n < 8 || memcmp (buf + 4, reply, 4))
      continue;
    for (rp = rpclist.lh_first; rp; rp = rp->link.le_next) {
      if (!memcmp (rp->msg, buf, 4)
	  && sin.sin_port == rp->dest->sin_port
	  && sin.sin_addr.s_addr == rp->dest->sin_addr.s_addr) {
	rpc_free (rp);
	break;
      }
    }
  }
}

static void
rpc_resend (void)
{
  struct rpc *rp, oldrp;
  for (rp = rpclist.lh_first; rp; rp = rp->link.le_next) {
    if (elapsed > rp->expire) {
      oldrp = *rp;
      rpc_free (rp);
      rp = &oldrp;
    }
    else if (elapsed > rp->lastsent + 1) {
      sendto (client_sock, rp->msg, rp->msglen, 0,
	      (struct sockaddr *) rp->dest, sizeof (*rp->dest));
      rp->lastsent = elapsed;
    }
  }
}

void
rpc_run (void)
{
  fd_set rfds;
  struct timeval tvstart, tvnext, tvnow, tv, *tvp;
  int n;

  rpc_init ();
  gettimeofday (&tvstart, NULL);
  tvnow = tvnext = tvstart;
  for (;;) {
    if (want_timer > 0 || rpclist.lh_first) {
      tvsub (&tv, &tvnext, &tvnow);
      tvp = &tv;
    }
    else
      tvp = NULL;

    rfds = svc_fdset;
    FD_SET (client_sock, &rfds);
    n = select (FD_SETSIZE, &rfds, NULL, NULL, tvp);
    gettimeofday (&tvnow, NULL);
    tvsub (&tv, &tvnow, &tvstart);
    elapsed = tv.tv_sec;
    if (n > 0 && FD_ISSET (client_sock, &rfds)) {
      rpc_getreplies ();
      n--;
      FD_CLR (client_sock, &rfds);
    }
    if (n > 0)
      svc_getreqset (&rfds);

    gettimeofday (&tvnow, NULL);
    if (tvnow.tv_sec > tvnext.tv_sec
	|| (tvnow.tv_sec == tvnext.tv_sec && tvnow.tv_usec >= tvnext.tv_usec)) {
      tvnext.tv_sec = tvnow.tv_sec + (tvnext.tv_usec <= tvnow.tv_usec);
      tvsub (&tv, &tvnow, &tvstart);
      elapsed = tv.tv_sec;
      rpc_resend ();
      if (want_timer > 0)
	timer ();
    }
  }
}

