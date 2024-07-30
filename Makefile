CFLAGS = -Wall -Wextra -Werror -pedantic -Wconversion 

main: main.c
	gcc $(CFLAGS) -g -o main main.c -I/usr/include/libxml2 -lcurl -lncurses -lcjson

valgrind: main 
	valgrind --leak-check=full -s ./main

clean:
	rm main
