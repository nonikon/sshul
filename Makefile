MAKE_VERSION = debug
TARGET = scpul

CC = gcc
CFLAGS = -Wall
LDFLAGS = -Wall -lm -lssh2

ifeq ($(MAKE_VERSION), debug)
CFLAGS += -g -DDEBUG
else
CFLAGS += -O2
endif

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

all : $(TARGET)

$(TARGET) : $(OBJS)
	@echo "\tLD $@"
	@$(CC) -o $@ $^ $(LDFLAGS)
$(OBJS) : %.o : %.c
	@echo "\tCC $@"
	@$(CC) -c $< -o $@ $(CFLAGS)

.PHONY : clean

clean :
	@echo "\tRM $(TARGET) $(OBJS)"
	@$(RM) $(TARGET) $(OBJS)
