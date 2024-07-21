CFLAGS = -Wall -Wextra -Werror -pedantic -Wconversion 

rsstest: cursestest.c
	gcc $(CFLAGS) -o main main.c -I/usr/include/libxml2 -lcurl -lxml2

clean:
	rm rsstest
