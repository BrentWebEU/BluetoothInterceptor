CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu99
LDFLAGS = -lbluetooth
TARGET = bt_interceptor
OBJS = main.o bt_utils.o tcp_server.o crypto.o

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c config.h bt_utils.h tcp_server.h crypto.h
bt_utils.o: bt_utils.c bt_utils.h config.h
tcp_server.o: tcp_server.c tcp_server.h config.h
crypto.o: crypto.c crypto.h config.h

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	rm -f /usr/local/bin/$(TARGET)
