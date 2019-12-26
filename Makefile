# MAKE_VERSION ?= debug
MAKE_VERSION ?= release
TARGET ?= sshul
INSTALL_PATH ?= /usr/local/bin

CC ?= gcc
LDFLAGS ?= -lssh2

ifeq ($(MAKE_VERSION), debug)
_CFLAGS = -g -DDEBUG
else
_CFLAGS = -O2
endif

_CFLAGS += -Wall $(CFLAGS)
_LDFLAGS += -Wall $(LDFLAGS)

OBJS = main.o config.o ssh_session.o \
	json.o xlist.o xstring.o

all : $(TARGET)

$(TARGET) : $(OBJS)
	@echo "\tLD $@"
	@$(CC) -o $@ $^ -lm $(_LDFLAGS)
$(OBJS) : %.o : %.c
	@echo "\tCC $@"
	@$(CC) -c $< -o $@ $(_CFLAGS)

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
