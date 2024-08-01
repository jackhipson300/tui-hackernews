CFLAGS = -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -Wformat -Wvla -fno-common -fstack-protector-strong -Wfloat-equal -Wpointer-arith 
LDFLAGS = -I/usr/include/libxml2 -lcurl -lncurses -lcjson

main: main.c
	gcc -DDEVELOPMENT $(CFLAGS) -g -o main main.c $(LDFLAGS)

valgrind: main 
	valgrind --leak-check=full -s ./main

production: main 
	gcc -O3 -Wno-unused-result -o hnews main.c $(LDFLAGS)

clean:
	rm main
