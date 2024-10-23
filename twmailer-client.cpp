#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define BUFFER_SIZE 1024

using namespace std;

void send_command(int sock, const string& command);
string read_line(int sock);
void interactive_mode(int sock);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./twmailer-client <ip> <port>" << endl;
        exit(EXIT_FAILURE);
    }

    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int client_sock;
    struct sockaddr_in server_addr{};

    // Create socket
    if ((client_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if(inet_pton(AF_INET, ip, &server_addr.sin_addr)<=0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // Connect
    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to server." << endl;

    interactive_mode(client_sock);

    close(client_sock);
    cout << "Disconnected from server." << endl;
    return 0;
}

void interactive_mode(int sock) {
    string input;

    while (true) {
        cout << "Enter command (SEND, LIST, READ, DEL, QUIT): ";
        getline(cin, input);

        if (input == "SEND") {
            send_command(sock, "SEND\n");

            string sender, receiver, subject, message, line;
            cout << "Sender: ";
            getline(cin, sender);
            cout << "Receiver: ";
            getline(cin, receiver);
            cout << "Subject: ";
            getline(cin, subject);
            cout << "Message (end with a single '.'): " << endl;
            while (getline(cin, line)) {
                if (line == ".") break;
                message += line + "\n";
            }

            send_command(sock, sender + "\n");
            send_command(sock, receiver + "\n");
            send_command(sock, subject + "\n");
            send_command(sock, message);
            send_command(sock, ".\n");

            string response = read_line(sock);
            cout << response;
        } else if (input == "LIST") {
            send_command(sock, "LIST\n");
            string username;
            cout << "Username: ";
            getline(cin, username);
            send_command(sock, username + "\n");

            string count_str = read_line(sock);
            if (count_str.empty()) {
                cout << "Error: No response from server." << endl;
                break;
            }
            cout << "Number of messages: " << count_str;
            int num = stoi(count_str);
            for (int i = 0; i < num; ++i) {
                string subject = read_line(sock);
                cout << subject;
            }
        } else if (input == "READ") {
            send_command(sock, "READ\n");
            string username, msg_num;
            cout << "Username: ";
            getline(cin, username);
            cout << "Message Number: ";
            getline(cin, msg_num);
            send_command(sock, username + "\n");
            send_command(sock, msg_num + "\n");

            string response = read_line(sock);
            if (response == "OK\n") {
                while (true) {
                    response = read_line(sock);
                    if (response == ".\n" || response.empty()) break;
                    cout << response;
                }
            } else {
                cout << "Error reading message." << endl;
            }
        } else if (input == "DEL") {
            send_command(sock, "DEL\n");
            string username, msg_num;
            cout << "Username: ";
            getline(cin, username);
            cout << "Message Number: ";
            getline(cin, msg_num);
            send_command(sock, username + "\n");
            send_command(sock, msg_num + "\n");

            string response = read_line(sock);
            cout << response;
        } else if (input == "QUIT") {
            send_command(sock, "QUIT\n");
            // No response is expected; close the socket and exit
            break;
        } else {
            cout << "Unknown command." << endl;
        }
    }
}

void send_command(int sock, const string& command) {
    	const char* data = command.c_str();
	size_t total_sent = 0;
	size_t data_len = command.length();
	
	while(total_sent < data_len) {
		ssize_t sent = send(sock, data + total_sent, data_len - total_sent, 0);
		if (sent <= 0)  {
			perror("send");
			break;
		}
		total_sent += sent;
	}
}

string read_line(int sock) {
    static string buffer;
    while (true) {
        size_t pos = buffer.find('\n');
        if (pos != string::npos) {
            string line = buffer.substr(0, pos + 1);
            buffer.erase(0, pos + 1);
            return line;
        }
        char temp[1];
        int valread = recv(sock, temp, 1, 0);
        if (valread <= 0) {
            // Connection closed or error
            return "";
        }
        buffer += temp[0];
    }
}

