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

ifdef OS
CP = copy
RM = del
else
CP = cp
RM = rm
endif

OBJS = main.o match.o config.o db.o ssh_session.o \
	json.o xlist.o xstring.o xhash.o md5.o

all : $(TARGET)

$(TARGET) : $(OBJS)
	@echo - LD $@
	@$(CC) -o $@ $^ -lm $(_LDFLAGS)
$(OBJS) : %.o : %.c
	@echo - CC $@
	@$(CC) -c $< -o $@ $(_CFLAGS)

.PHONY : install uninstall clean

install :
	@echo COPY [$(TARGET)] TO [$(INSTALL_PATH)]
	@$(CP) $(TARGET) $(INSTALL_PATH)
uninstall :
	@echo DELETE [$(INSTALL_PATH)/$(TARGET)]
	@$(RM) $(INSTALL_PATH)/$(TARGET)
clean :
	@echo DELETE $(TARGET) $(OBJS)
	@$(RM) $(TARGET) $(OBJS)
