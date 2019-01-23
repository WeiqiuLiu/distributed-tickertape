
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "ticker_prot.h"
#include "minirpc.h"

void
timer ()
{
  if (elapsed >= 2)
    exit (0);
}

void
repeat (int nrpcs, int nhosts, char **hosts)
{
  struct sockaddr_in *dests;
  char buf[80];
  submit_args arg;
  int failed = 0;
  int i, j;

  dests = malloc (nhosts * sizeof (*dests));
  if (!dests) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  bzero (dests, nhosts * sizeof (*dests));

  for (i = 0; i < nhosts; i++) {
    struct hostent *hp = gethostbyname (hosts[0]);
    if (!hp) {
      fprintf (stderr, "%s: could not get address of host\n", hosts[0]);
      failed = 1;
    }
    dests[i].sin_family = AF_INET;
    dests[i].sin_port = htons (atoi (hosts[1]));
    dests[i].sin_addr = *(struct in_addr *) hp->h_addr;
    hosts += 2;
  }
  if (failed)
    exit (1);

  arg.msg = buf;
  for (j = 0; j < nrpcs; j++) {
    for (i = 0; i < nhosts; i++) {
      sprintf (arg.msg, "t-%d", i + (j * nhosts));
      rpc_send (&dests[i], TICKER_PROG, TICKER_VERS, TICKER_SUBMIT,
		(xdrproc_t) xdr_submit_args, &arg);
    }
  }

  want_timer = 1;
  rpc_run ();
}

CLIENT *
make_ticker_client (char *host, int port)
{
  struct hostent *hp;
  struct sockaddr_in sin;
  CLIENT *c;
  struct timeval tv;
  int sock = RPC_ANYSOCK;

  hp = gethostbyname (host);
  if (!hp) {
    fprintf (stderr, "%s: could not find address for host\n", host);
    return NULL;
  }

  bzero (&sin, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (port);
  sin.sin_addr = *(struct in_addr *) hp->h_addr;

  tv.tv_sec = 1;
  tv.tv_usec = 0;
  c = clntudp_create (&sin, TICKER_PROG, TICKER_VERS, tv, &sock);
  if (!c) {
    clnt_pcreateerror (host);
    return NULL;
  }

  tv.tv_sec = 15;
  clnt_control (c, CLSET_TIMEOUT, (char *) &tv);

  return c;
}

int
submit (CLIENT *clnt, char *msg)
{
  submit_args arg;
  submit_result *result;
  int ok;

  arg.msg = msg;
  result = ticker_submit_1 (&arg, clnt);
  if (!result) {
    clnt_perror (clnt, "call failed:");
    return -1;
  }

  /* Note, you MUST call xdr_free to avoid a memory leak.
   * After calling xdr_free, you can't use the results anymore.
   * That's why we have to copy ok into a local variable */
  ok = result->ok;
  xdr_free ((xdrproc_t) xdr_submit_result, (char *) result);
  return ok;
}

void usage (const char *) __attribute__ ((noreturn));
void
usage (const char *argv0)
{
  const char *name;
  if ((name = strrchr (argv0, '/')))
    name++;
  else
    name = argv0;

  fprintf (stderr,
	   "usage: %s host1 port1 tag1 [host2 port2 tag2 ...]\n"
	   "       %s -r # host1 port1 [host2 port2 ...]\n",
	   name, name);
  exit (1);
}

int
main (int argc, char *argv[])
{
  int n;
  CLIENT *c;

  if (argc < 4)
    usage (argv[0]);

  if (!strcmp (argv[1], "-r")) {
    if ((argc % 2) != 1)
      usage (argv[0]);
    repeat (atoi (argv[2]), (argc - 3) / 2, argv + 3);
    exit (0);
  }

  if ((argc % 3) != 1)
    usage (argv[0]);

  for (n = 1; n < argc; n += 3) {
    if ((c = make_ticker_client (argv[n], atoi (argv[n+1])))) {
      if (submit (c, argv[n+2]) <= 0)
	fprintf (stderr, "warning:  %s returned failure\n", argv[n]);
      clnt_destroy (c);
    }
  }

  exit (0);
}
