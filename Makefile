# MAKE_VERSION ?= debug
MAKE_VERSION ?= release
TARGET ?= sshul
INSTALL_PATH ?= /usr/local/bin

CC ?= gcc
CFLAGS ?=
LDFLAGS ?= -lssh2

ifeq ($(MAKE_VERSION), debug)
_CFLAGS = -g -Wall -DDEBUG $(CFLAGS)
_LDFLAGS = $(LDFLAGS)
else
_CFLAGS = -Os -Wall -DNDEBUG $(CFLAGS)
_LDFLAGS = -s $(LDFLAGS)
endif

OBJS = main.o match.o config.o db.o ssh_session.o \
	json.o xlist.o xstring.o xhash.o md5.o

all : $(TARGET)

$(TARGET) : $(OBJS)
	@echo --LD $@
	@$(CC) -o $@ $^ -lm $(_LDFLAGS)
$(OBJS) : %.o : %.c
	@echo --CC $@
	@$(CC) -c $< -o $@ $(_CFLAGS)

.PHONY : install uninstall clean

install :
	@echo INSTALL $(TARGET) -> $(INSTALL_PATH)
	@cp $(TARGET) $(INSTALL_PATH)
uninstall :
	@echo UNINSTALL $(INSTALL_PATH)/$(TARGET)
	@$(RM) $(INSTALL_PATH)/$(TARGET)
clean :
	@echo REMOVE $(TARGET) $(OBJS)
	@$(RM) $(TARGET) $(OBJS)
