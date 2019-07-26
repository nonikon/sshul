# MAKE_VERSION = debug
MAKE_VERSION = release
TARGET = sshul
INSTALL_PATH = /usr/local/bin

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

.PHONY : install uninstall clean

install :
	@echo "\tINSTALL $(TARGET) -> $(INSTALL_PATH)"
	@cp $(TARGET) $(INSTALL_PATH)
uninstall :
	@echo "\tUNINSTALL $(INSTALL_PATH)/$(TARGET)"
	@$(RM) $(INSTALL_PATH)/$(TARGET)
clean :
	@echo "\tRM $(TARGET) $(OBJS)"
	@$(RM) $(TARGET) $(OBJS)
