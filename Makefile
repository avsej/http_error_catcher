CFLAGS=-ggdb3 -O0 -pedantic
LDFLAGS=-lmicrohttpd -lriemann_c_client
PREFIX=/usr/local

.PHONY: clean install uninstall

http_error_catcher: http_error_catcher.c

clean:
	rm -f http_error_catcher

install: http_error_catcher
	install -m 555 http_error_catcher $(PREFIX)/bin/http_error_catcher

uninstall: http_error_catcher
	rm -f $(PREFIX)/bin/http_error_catcher
