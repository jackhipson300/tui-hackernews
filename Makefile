CFLAGS = -Wall -Wextra -Werror -pedantic -Wconversion 

main: main.c
	gcc $(CFLAGS) -o main main.c -I/usr/include/libxml2 -lcurl -lxml2

clean:
	rm main
