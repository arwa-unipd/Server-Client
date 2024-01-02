


PROGRAMS = client \
           server 

all: $(PROGRAMS)

server: LDLIBS=-lpthread

clean:
	rm -f $(PROGRAMS)


# ----------------------------------------------------------------------------------
# NOTE:
# here we are using a hidden implicit rule that links any .c file into an executable
# to see all active implicit rules use: make -p 
#   
#   LINK.c = $(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH)   
#
#   # Not a target:
#   .c:
#   #  Builtin rule
#   #  Implicit rule search has not been done.
#   #  Modification time never checked.
#   #  File has not been updated.
#   #  recipe to execute (built-in):
#   	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@
#
# ----------------------------------------------------------------------------------