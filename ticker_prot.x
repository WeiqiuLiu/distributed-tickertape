/*
 * Ticker-tape serializer RPC definitions
 */

typedef string msg_t<79>;

struct xaction_args{
  int timestamp;
  int proc_id;
};
struct submit_args {
  msg_t msg;
};
struct msg_args{
  msg_t msg;
  int proc_id;
  int timestamp;
};
struct submit_result {
  bool ok;
};
struct update_args {
  int svc_no;
};

program TICKER_PROG {
  version TICKER_VERS {
    submit_result TICKER_SUBMIT (submit_args) = 1;
    submit_result TICKER_REQUEST (xaction_args) = 2;
    submit_result TICKER_ACK (xaction_args) = 3;
    submit_result TICKER_MSG (msg_args) = 4;
    submit_result TICKER_RELEASE (xaction_args) = 5;
    submit_result TICKER_UPDATE (update_args) = 6;
  } = 1;
} = 400001;
