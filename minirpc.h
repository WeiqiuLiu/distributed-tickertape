
#include <netinet/in.h>
#include <rpc/rpc.h>

extern time_t elapsed;	   /* Number of seconds elapsed since start */
extern int want_timer;	      /* If > 0, want timer called each sec */

int mkudpsock (int port);

int rpc_marshal (void *msgbuf, size_t msgmax,
		 u_int32_t prog, u_int32_t vers, u_int32_t proc,
		 xdrproc_t argxdr, void *arg);

/*
 * This function sends an RPC to the server at UDP port dest, but
 * returns immediately without awaiting a reply.  (It will keep trying
 * in the background in case the UDP packet is lost.)
 *
 * prog, vers, and proc are as in the .x file, for instance
 * TICKER_PROG, TICKER_VERS, and TICKER_SUBMIT respectively.
 *
 * argxdr is the auto-generated XDR marshaling routing for the
 * argument type.  For example, for type submit_args, the function is
 * xdr_submit_args (in generally just prepend "xdr_" to the name of
 * the type).
 *
 * arg is a pointer to the actual arguments.  E.g., for TICKER_SUBMIT,
 * this would be of type submit_args *.
 */
void rpc_send (struct sockaddr_in *dest,
	       u_int32_t prog, u_int32_t vers, u_int32_t proc,
	       xdrproc_t argxdr, void *arg);

/*
 * This function is like rpc_send, except that it sends the same RPC
 * to several servers.  dest is now a vector of pointers to UDP socket
 * addresses.  There must be a NULL pointer after the last socket
 * address.
 */
void rpc_broadcast (struct sockaddr_in **dest,
		    u_int32_t prog, u_int32_t vers, u_int32_t proc,
		    xdrproc_t argxdr, void *arg);

void rpc_run (void) __attribute__ ((noreturn));

/* You need to implement this in server.c: */
extern void timer (void);
