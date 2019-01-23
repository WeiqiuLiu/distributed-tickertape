RPCGEN = rpcgen -C
PROT = ticker_prot
CC = cc
CFLAGS = -Wall -g
#CFLAGS = -Wall -g -DRPC_SVC_FG=1
LIBS = -L/usr/local/lib #-ldmalloc
DISTDIR = ticker

OURDIR = `echo ~ucacbnk`
TESTPROG = $(OURDIR)/gz03-2015/bin/test-ticker
KEYGRP = z25staff

all: ticker-client ticker-server
.PHONY: all

.SUFFIXES: .c .o
.c.o:
	$(CC) -c $(CFLAGS) $<

$(PROT).h: $(PROT).x
	rm -f $(PROT).h
	$(RPCGEN) -h -o $@ $(PROT).x
$(PROT)_xdr.c: $(PROT).x
	rm -f $(PROT)_xdr.c
	$(RPCGEN) -c -o $@ $(PROT).x
$(PROT)_clnt.c: $(PROT).x
	rm -f $(PROT)_clnt.c
	$(RPCGEN) -l -o $@ $(PROT).x
$(PROT)_svc.c: $(PROT).x
	rm -f $(PROT)_svc.c
	$(RPCGEN) -m -o $@ $(PROT).x
#	$(RPCGEN) -s udp -o $@ $(PROT).x
$(PROT)_xdr.o $(PROT)_clnt.o $(PROT)_svc.o client.o server.o: $(PROT).h
client.o server.o minirpc.o: minirpc.h

CLIENT_OBJS = client.o $(PROT)_xdr.o $(PROT)_clnt.o minirpc.o
ticker-client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS) $(LIBS)

SERVER_OBJS = server.o $(PROT)_xdr.o $(PROT)_svc.o $(PROT)_clnt.o minirpc.o
ticker-server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS) $(LIBS)

.PHONY: ourdist ourinstall
ourdist:
	rm -rf ticker.tar.gz ticker
	mkdir ticker
	cp Makefile .cvsignore \
		client.c minirpc.c minirpc.h server.c ticker_prot.x \
		ticker/
	tar czf ticker.tar.gz ticker
	rm -rf ticker
ourinstall: ourdist ticker-client
	rm -f $(OURDIR)/bin/ticker-client $(OURDIR)/bin/test-ticker \
		$(OURDIR)/src/test-ticker $(OURDIR)/src/ticker.tar.gz
	cp ticker.tar.gz $(OURDIR)/src/
	cp ticker-client test-ticker $(OURDIR)/bin/
	chmod 0555 $(OURDIR)/bin/ticker-client $(OURDIR)/bin/test-ticker
	ln $(OURDIR)/bin/test-ticker $(OURDIR)/src/test-ticker
	chgrp $(KEYGRP) $(TESTPROG) && chmod g+s $(TESTPROG)

.PHONY: dist
dist:
	rm -rf $(DISTDIR)
	mkdir $(DISTDIR)
	cp Makefile *.c *.h *.x $(DISTDIR)/
	cd $(DISTDIR) && $(MAKE) clean
	tar czf $(DISTDIR).tar.gz $(DISTDIR)
	rm -rf $(DISTDIR)

.PHONY: handin
handin:
	$(MAKE) clean
	$(MAKE) ticker-server
	$(MAKE) dist
	$(TESTPROG) ./ticker-server | tee score

.PHONY: clean clobber
clean:
	rm -f $(PROT).h $(PROT)_clnt.c $(PROT)_svc.c $(PROT)_xdr.c
	rm -f $(PROT)_tbl.i ticker-client ticker-server score
	rm -f *.core *.o *~
clobber: clean
	@:
