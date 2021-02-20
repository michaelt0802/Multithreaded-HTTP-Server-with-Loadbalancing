all: queue http
	gcc -o httpserver -Wall -Wextra -Wpedantic -Wshadow -pthread httpserver.c queue.o http.c threads.h

queue:
	gcc -c -Wall -Wextra -Wpedantic -Wshadow -pthread queue.c queue.h

http:
	gcc -c -Wall -Wextra -Wpedantic -Wshadow -pthread http.c http.h
	
loadbalancer:
	gcc -c -Wall -Wextra -Wpedantic -Wshadow -pthread loadbalancer.c loadbalancer.h

clean:
	rm -f httpserver
