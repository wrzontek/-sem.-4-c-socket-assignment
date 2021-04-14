CC = g++ -Wall -O2 -std=c++17

.PHONY: all clean

server:
	$(CC) server.cpp -o server -lstdc++fs

clean:
	rm server