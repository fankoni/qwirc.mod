# Makefile for src/mod/qwirc.mod/

doofus:
	@echo ""
	@echo "Let's try this from the right directory..."
	@echo ""
	@cd ../../../; make

static: ../qwirc.o
modules: ../../../qwirc.so

../qwirc.o:
	$(CC) $(CFLAGS) $(CPPFLAGS) -DMAKING_MODS -c qw_main.c qw_net.c \
	qw_utils.c qw_parser.c qwirc.c
	rm -f ../qwirc.o
	mv qwirc.o ../

../../../qwirc.so: ../qwirc.o qw_main.o qw_net.o qw_utils.o qw_parser.o 
	$(LD) -o ../../../qwirc.so ../qwirc.o qw_main.o qw_net.o qw_utils.o qw_parser.o
	$(STRIP) ../../../qwirc.so

depend:
	$(CC) $(CFLAGS) $(CPPFLAGS) -MM *.c > .depend

clean:
	@rm -f .depend *.o *.so *~

#safety hash

../qwirc.o: .././qwirc.mod/qwirc.c .././qwirc.mod/qw_main.c  \
.././qwirc.mod/qw_net.c .././qwirc.mod/qw_common.h .././qwirc.mod/qw_utils.c \
.././qwirc.mod/qw_parser.c
