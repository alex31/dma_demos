# Required include directories
BOARDINC = $(CONFDIR)/cfg

# List of all the board related files.
BOARDSRC = $(CONFDIR)/board.c

# Shared variables
ALLCSRC += $(BOARDSRC)
ALLINC  += $(BOARDINC)
