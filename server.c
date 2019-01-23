
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <assert.h>
#include "ticker_prot.h"
#include "minirpc.h"
#include <rpc/svc.h>
#include <sys/queue.h>
#include <arpa/inet.h>

#define NORMAL 1
#define REQUEST 2
#define WAIT 3
#define GRANTED 4
#define RELEASE 5
#define ACK 6
#define DATA 7

void ticker_prog_1 (struct svc_req *rqstp, SVCXPRT *transp);
void release();
char *progname;
u_int32_t my_id;		/* This server's unique ID */
int nothers;			/* Number of other servers */
struct sockaddr_in **others;	/* Addresses of the other servers,
				 * followed by a NULL pointer */
struct sockaddr_in **others;
/*struct xaction_args *action_list；*/
int state = 1;
int logic_clock = 0;
int alive_svc[10];
int next_send = 0;
int sent_acked_no = 0;

struct xaction {
  int timestamp;
  int proc_id;
  int expire;
  LIST_ENTRY (xaction) link;
};

struct waitmsg {
  char* msg;
  LIST_ENTRY (waitmsg) link;
};

struct ackentry {
  int proc_id;
  int latest_ack;
  LIST_ENTRY (ackentry) link;
};

static LIST_HEAD (xaction_t, xaction) requestList = { NULL };
static LIST_HEAD (waitmsg_t, waitmsg) waitlist = { NULL };
static LIST_HEAD (ackentry_t, ackentry) acklist = { NULL };

static void
insert_x(struct xaction *x){
  if (requestList.lh_first == NULL){
    LIST_INSERT_HEAD (&requestList, x, link);
  } else {
    struct xaction *temp;
    temp = requestList.lh_first;
    if (x->timestamp < temp->timestamp) {
      LIST_INSERT_HEAD (&requestList, x, link);
    } else if (x->timestamp == temp->timestamp) {
      if (x->proc_id > temp->proc_id) {
        LIST_INSERT_AFTER(temp, x, link);
      } else {
        LIST_INSERT_HEAD (&requestList, x, link);
      }
    } else {
      for ( ; temp; temp = temp->link.le_next) {
        /*printf("%ld\n", (temp->link.le_next)->timestamp);*/
        if (temp->link.le_next == NULL) {
          LIST_INSERT_AFTER(temp, x, link);
          break;
        } else {
          if (x->timestamp < temp->link.le_next->timestamp) {
            LIST_INSERT_AFTER(temp, x, link);
            break;
          } else if (x->timestamp == temp->link.le_next->timestamp) {
            if (x->proc_id < temp->link.le_next->proc_id) {
              LIST_INSERT_AFTER(temp, x, link);
              break;
            }
          }
        }
        
      }
    }
  }
}

static int
check_if_need_insert_x(int proc_id){
  struct xaction *x;
  for (x = requestList.lh_first; x; x = x->link.le_next) {
    if (x->proc_id == proc_id) return 0;
  }
  return 1;
}

static int
check_if_all_acked(){
  struct ackentry *a;
  if (requestList.lh_first == NULL) {
    return 0;
  }
  int timestamp = requestList.lh_first->timestamp;
  int num = 0;
  for (a = acklist.lh_first; a; a = a->link.le_next) {
    if (a->latest_ack > timestamp) {
      num++;
    } else if (a->latest_ack == timestamp && a->proc_id == requestList.lh_first->proc_id) {
      num++;
    }
  }
  if (num == nothers) return 1;
  else return 0;
}

static int
check_if_I_acked(int timestamp)
{
  if (timestamp < sent_acked_no) {
    return 1;
  } else {
    return 0;
  }
}

/*static int
check_if_sender_fail() {
  struct ackentry *a;
  if (requestList.lh_first == NULL) {
    return 0;
  }
  int timestamp = requestList.lh_first->timestamp;
  for (a = acklist.lh_first; a; a = a->link.le_next) {
    if (a->latest_ack >= timestamp && a->proc_id == requestList.lh_first->proc_id) {
      return 0;
    } 
  }
  return 1;
}*/

static void
send_request() {
  struct xaction *x = NULL;
  if (!(x = malloc (sizeof (*x)))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  x->timestamp = logic_clock++;
  x->expire = elapsed + 6;
  x->proc_id = my_id;
  insert_x(x);

  xaction_args arg;
  arg.timestamp = x->timestamp;
  arg.proc_id = my_id;
  if (sent_acked_no < x->timestamp) sent_acked_no = x->timestamp;
  rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_REQUEST, (xdrproc_t) xdr_xaction_args, &arg);
}

static int
getState(){
  want_timer = 1;
  if (requestList.lh_first == NULL /*&& waitlist.lh_first == NULL*/) {
    next_send = 0;
    want_timer = 0;
    return NORMAL;
  } else {
    /*if (requestList.lh_first != NULL){*/
      if (requestList.lh_first->proc_id == my_id) {
        if (check_if_all_acked() == 0) {
          next_send = 0;
          return REQUEST;
        } else {
          next_send = DATA;
          return GRANTED;
        }
      } else {
        if (check_if_I_acked(requestList.lh_first->timestamp) == 0) next_send = ACK;
        else next_send = 0;

        return WAIT;
      }
    /*} else {
      send_request();
      return getState();
    }*/
  }
}

static void
update_logic_clock(int timestamp){
  if (timestamp >= logic_clock) {
    logic_clock = timestamp + 1;
  }
}

static void
update_acklist(struct ackentry *a)
{
  struct ackentry *temp = NULL;
  int flag = 0;
  for (temp = acklist.lh_first; temp; temp = temp->link.le_next) {
    if (temp->proc_id == a->proc_id) {
      flag = 1;
      break;
    }
  }
  if (flag == 0) {
    LIST_INSERT_HEAD(&acklist, a, link);
  } else {
    if (temp->latest_ack < a->latest_ack) {
      temp->latest_ack = a->latest_ack;
    }
  }
}

static void
insert_m(struct waitmsg *m){
  LIST_INSERT_HEAD(&waitlist, m, link);
}

static void
msg_free(struct waitmsg *m)
{
  LIST_REMOVE (m, link);
  free (m->msg);
  free (m);
}
/*
static int
check_if_need_send_ack(int timestamp, int proc_id) {
  if (timestamp + 1 < logic_clock) {
    return 0;
  } else {
    return 1;
  }
}*/

static void
sendAll()
{
  if (requestList.lh_first != NULL && requestList.lh_first->proc_id == my_id) {
    struct waitmsg *m, oldmsg;
    msg_args arg1;
    arg1.proc_id = my_id;
    arg1.timestamp = requestList.lh_first->timestamp;
    for (m = waitlist.lh_first; m; m = m->link.le_next) {
      arg1.msg = m->msg;
      printf("%s\n", m->msg);
      rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_MSG, (xdrproc_t) xdr_msg_args, &arg1);
      oldmsg = *m;
      msg_free(m);
      m = &oldmsg;
    }
    xaction_args arg;
    arg.timestamp = requestList.lh_first->timestamp;
    arg.proc_id = my_id;
    rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_RELEASE, (xdrproc_t) xdr_xaction_args, &arg);

    release(my_id, requestList.lh_first->timestamp);
  } else {
    state = getState();
  }
  
}

static int
check_if_has_been_released(int proc_id, int timestamp){
  struct xaction *x;
  for (x = requestList.lh_first; x; x = x->link.le_next) {
    if (x->proc_id == proc_id && x->timestamp == timestamp) {
      return 0;
    }
  }
  return 1;
}

static void
remove_x(int proc_id, int timestamp){
  struct xaction *x;
  for (x = requestList.lh_first; x; x = x->link.le_next) {
    if (x->proc_id == proc_id && x->timestamp == timestamp) {
      LIST_REMOVE(x, link);
      free(x);
      break;
    }
  }
}

void
release(int proc_id, int timestamp){
  remove_x(proc_id, timestamp);
  state = getState();
  /*if (state == GRANTED) {
    requestList.lh_first->expire = elapsed + 10;
  }*/
}

static void
update_svc_no() {
  int timestamp = requestList.lh_first->timestamp;
  int num = 0;
  struct ackentry *a;
  for (a = acklist.lh_first; a; a = a->link.le_next) {
    if (a->latest_ack >= timestamp && a->proc_id == requestList.lh_first->proc_id) {
      num++;
    } else if (a->latest_ack > timestamp && a->proc_id != requestList.lh_first->proc_id) {
      num++;
    }
  }
  nothers = num;
}

/*static void
update_svc(){
  for(int i = 0; others[i] != NULL; i++) {
    for (int j = 0; j < no_acked; j++) {
      if (alive_svc[j] == i) break;
      else if (j == no_acked-1) {
        for (int k = 0; others[i+k] != NULL; k++) {
          others[i+k] = others[i+k+1];
        }
        i--;
      }
    }
  }
}*/


submit_result *
ticker_release_1_svc (xaction_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  release(argp->proc_id, argp->timestamp);

  result.ok = TRUE;
  return (&result);
}

submit_result *
ticker_update_1_svc (update_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  
  nothers = argp->svc_no;

  result.ok = TRUE;
  return (&result);
}

submit_result *
ticker_submit_1_svc (submit_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  struct waitmsg *m = NULL;
  int len = strlen(argp->msg);
  if (!(m = malloc (sizeof (*m))) || !(m->msg = malloc (sizeof(msg_t)))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }

  memcpy(m->msg, argp->msg, len);
  m->msg[len] = '\0';

  insert_m(m);
  /*if (waitlist.lh_first == NULL) {
    m->expire = elapsed + 10;
  } else {
    m->expire = waitlist.lh_first->expire;
  }*/

  if (check_if_need_insert_x(my_id) == 1) {
    send_request();
  }

  state = getState();

  result.ok = TRUE;
  return (&result);
}

submit_result *
ticker_request_1_svc (xaction_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  struct xaction *x;
  if (!(x = malloc (sizeof (*x)))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }

  struct ackentry *a;
  if (!(a = malloc (sizeof (*a)))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  
  
  a->latest_ack = argp->timestamp;
  a->proc_id = argp->proc_id;
  update_acklist(a);

  update_logic_clock(argp->timestamp);


  x->proc_id = argp->proc_id;
  x->timestamp = argp->timestamp;
  x->expire = elapsed + 9;
  insert_x(x);


/*  for(int i=0; others[i] != NULL; i++) {
    if (inet_ntoa(others[i]->sin_addr) == inet_ntoa(rqstp->rq_xprt->xp_raddr.sin_addr)) {
      if (ntohs(others[i]->sin_port) == argp->port){
        x->svc_no = i;
        break;
      }
    }
  }*/

  if (check_if_I_acked(x->timestamp) == 0) {
    xaction_args arg;
    arg.proc_id = my_id;
    arg.timestamp = logic_clock++;
    sent_acked_no = arg.timestamp;
    rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_ACK, (xdrproc_t) xdr_xaction_args, &arg);
  } 
/*
  if (state == REQUEST) {
    if (requestList.lh_first->timestamp == x->timestamp && requestList.lh_first->proc_id == x->proc_id) {
      flag = 1;
    }
  } else if (state == NORMAL) {
    flag = 1;
  }*/

  state = getState();

  result.ok = TRUE;
  return (&result);
}

submit_result *
ticker_ack_1_svc (xaction_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  struct ackentry *a;
  if (!(a = malloc (sizeof (*a)))) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  a->latest_ack = argp->timestamp;
  a->proc_id = argp->proc_id;
  update_acklist(a);
  update_logic_clock(argp->timestamp);
 
  state = getState();

  /*for(int i=0; others[i] != NULL; i++) {
    if (inet_ntoa(others[i]->sin_addr) == inet_ntoa(rqstp->rq_xprt->xp_raddr.sin_addr)) {
      if (ntohs(others[i]->sin_port) == argp->port){
        alive_svc[no_acked] = i;
        break;
      }
    }
  }  */
  /*printf("state %d\n", state);*/    

  result.ok = TRUE;
  return &result;
}

submit_result *
ticker_msg_1_svc (msg_args *argp, struct svc_req *rqstp)
{
  static submit_result result;
  
  if (requestList.lh_first->timestamp < argp->timestamp) {
    printf("%s\n", argp->msg);
  } else if (requestList.lh_first->timestamp == argp->timestamp && requestList.lh_first->proc_id <= argp->proc_id) {
    printf("%s\n", argp->msg);
  } else if (check_if_has_been_released(argp->proc_id, argp->timestamp) == 0) {
    printf("%s\n", argp->msg);
  } 
  
  /*printf("%s\n", argp->msg);*/
  
  state = getState();
  
  result.ok = TRUE;
  return (&result);
}

/*
 * This function gets called once per second, if you set the global
 * variable want_timer to a positive value.
 *
 * You should use this to print out trades in order.  (You have to
 * delay printing a trade until you know you have heard of and printed
 * any trades that happened before that trade.)
 */
void
timer (void)
{
  /*if (requestList.lh_first != NULL && requestList.lh_first->expire <= elapsed) {
    sendAll();
  }*/
  state = getState();
  while (next_send == DATA || (requestList.lh_first != NULL && requestList.lh_first->expire <= elapsed)) {
    /*if (next_send == REQUEST) {
      struct xaction *x;
      x = requestList.lh_first;
      if (x != NULL && x->responed == 0 && x->expire <= elapsed + 10) {
        printf("actual sent: %d\n", next_send);
        xaction_args arg;
        arg.timestamp = x->timestamp;
        arg.proc_id = my_id;
        arg.command = REQUEST;
        arg.port = port;
        if (sent_acked_no < x->timestamp) sent_acked_no = x->timestamp;
        rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_REQUEST, (xdrproc_t) xdr_xaction_args, &arg);
        x->responed = 1;
        if (check_if_all_acked() == 1 && x->proc_id == my_id) {
          sendAll();
        }
      }
    } else if (next_send == ACK) {
      printf("actual sent: %d\n", next_send);
      xaction_args arg;
      arg.command = ACK;
      arg.proc_id = my_id;
      arg.port = port;
      arg.timestamp = logic_clock++;
      sent_acked_no = arg.timestamp;
      rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_ACK, (xdrproc_t) xdr_xaction_args, &arg);
    } else */
    if (next_send == DATA) {
      if (requestList.lh_first != NULL /*&& requestList.lh_first->expire <= elapsed*/) {
        sendAll();
      }
    } else {
      update_svc_no();
      if (my_id == requestList.lh_first->proc_id) {
      } else {/*
        requestList.lh_first->expire = elapsed + 3;*/
        /*sender_fail_test = 1;*/
        nothers--;
        release(requestList.lh_first->proc_id, requestList.lh_first->timestamp); 
      }
      update_args arg;
      arg.svc_no = nothers;
      rpc_broadcast(others, TICKER_PROG, TICKER_VERS, TICKER_UPDATE, (xdrproc_t) xdr_update_args, &arg);
      
       /*else {
        nothers--;
        release(requestList.lh_first->proc_id, requestList.lh_first->timestamp); 
      }*/
    }
    state = getState();
  }
   /*else if (next_send == TIMER) {
    if (requestList.lh_first->expire <= elapsed) {
      if (requestList.lh_first->proc_id == my_id) {
        update_svc();
        sendAll();
      } else {
        update_svc();
        release();
      }
    }
  }*/
}

/*
 *  You don't have to change anything below here
 */

static void
init_others (int no, char **av)
{
  int i;
  struct hostent *hp;
  int failed = 0;

  nothers = no;

  others = malloc ((nothers + 1) * sizeof (others[0]));
  if (!others) {
    fprintf (stderr, "out of memory\n");
    exit (1);
  }
  others[nothers] = NULL;	/* So end of list is NULL */

  for (i = 0; i < nothers; i++) {
    others[i] = malloc (sizeof (*others[i]));
    if (!others[i]) {
      fprintf (stderr, "out of memory");
      exit (1);
    }
    bzero (others[i], sizeof (*others[i]));
    others[i]->sin_family = AF_INET;
    hp = gethostbyname (*av++);
    if (!hp) {
      fprintf (stderr, "%s: could not find address of host %s\n",
	       progname, av[-1]);
      failed = 1;
    }
    others[i]->sin_addr = *(struct in_addr *) hp->h_addr;
    others[i]->sin_port = htons (atoi (*av++));
  }

  if (failed)
    exit (1);
}

static void usage (void) __attribute__ ((noreturn));
static void
usage (void)
{
  fprintf (stderr, "usage: %s my-id my-port host1 port1 [host2 port2 ...]\n",
	   progname);
  exit (1);
}

int
main (int argc, char **argv)
{
  int my_sock;
  SVCXPRT *transp;

  if ((progname = strrchr (argv[0], '/')))
    progname++;
  else
    progname = argv[0];

  if (argc < 3 || (argc - 3) % 2)
    usage ();

  my_id = atoi (argv[1]);
  my_sock = mkudpsock (atoi (argv[2]));
  init_others ((argc - 3) / 2, argv + 3);

  if (!(transp = svcudp_create (my_sock))) {
    fprintf (stderr, "cannot create UDP service\n");
    exit (1);
  }
  if (!svc_register (transp, TICKER_PROG, TICKER_VERS, ticker_prog_1, 0)) {
    fprintf (stderr, "failed to register RPC program\n");
    exit (1);
  }

  setvbuf (stdout, NULL, _IOLBF, 0);
  rpc_run ();
}
