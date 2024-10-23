all: server client

server: twmailer-server.cpp
	g++ -o twmailer-server twmailer-server.cpp

client: twmailer-client.cpp
	g++ -o twmailer-client twmailer-client.cpp

clean:
	rm -f twmailer-server twmailer-client

