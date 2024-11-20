all: server client

server: twmailer-server.cpp
	g++ -std=c++14 -Wall -pthread -o twmailer-server twmailer-server.cpp -lldap -llber

client: twmailer-client.cpp
	g++ -std=c++14 -Wall -o twmailer-client twmailer-client.cpp

clean:
	rm -f twmailer-server twmailer-client
