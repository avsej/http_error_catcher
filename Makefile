CFLAGS=-pedantic -Wall -Wextra
LDFLAGS=-lmicrohttpd -lriemann_c_client -pthread
PREFIX=/usr/local

.PHONY: clean install uninstall

http-error-catcher: http-error-catcher.o
	$(CC) $(CPPFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f http-error-catcher
	rm -f http-error-catcher.o

install: http_error_catcher
	install -m 755 http-error-catcher $(PREFIX)/bin/http-error-catcher
	install -m 755 http-error-catcher.env /etc/default/http-error-catcher
	install -m 755 http-error-catcher.service /usr/lib/systemd/system/http-error-catcher.service

uninstall: http-error-catcher
	rm -f $(PREFIX)/bin/http-error-catcher
	rm -f /etc/default/http-error-catcher
	rm -f /usr/lib/systemd/system/http-error-catcher.service
