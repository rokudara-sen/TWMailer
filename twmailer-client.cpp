// include necessary headers
#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h> // for close()
#include <netinet/in.h> // for sockaddr_in
#include <arpa/inet.h> // for inet_pton
#include <sys/socket.h> // for socket functions
#include <sys/types.h>
#include <fstream>
#include <sstream>

#define BUFFER_SIZE 1024 // define buffer size

using namespace std;

void send_command(int sock, const string& command);
string read_line(int sock);
void interactive_mode(int sock);

int main(int argc, char *argv[]) {
    // check if correct number of arguments is provided
    if (argc != 3) {
        cerr << "Usage: ./twmailer-client <ip> <port>" << endl;
        exit(EXIT_FAILURE);
    }

    const char *ip = argv[1]; // get server ip
    int port = atoi(argv[2]); // get port number

    int client_sock;
    struct sockaddr_in server_addr{}; // server address

    // create socket
    if ((client_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET; // ipv4
    server_addr.sin_port = htons(port); // port number

    // convert ipv4 addresses from text to binary form
    if(inet_pton(AF_INET, ip, &server_addr.sin_addr)<=0) {
        perror("Invalid address/ Address not supported");
        exit(EXIT_FAILURE);
    }

    // connect to server
    if (connect(client_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection Failed");
        exit(EXIT_FAILURE);
    }

    cout << "Connected to server." << endl;

    interactive_mode(client_sock); // start interactive mode

    close(client_sock); // close socket
    cout << "Disconnected from server." << endl;
    return 0;
}

void interactive_mode(int sock) {
    string input;
    bool logged_in = false;
    string username;

    while (true) {
        if (!logged_in) {
            cout << "Please login first." << endl;
            cout << "Enter command (LOGIN or QUIT): ";
            getline(cin, input);

            if (input == "LOGIN") {
                send_command(sock, "LOGIN\n"); // send login command
                cout << "Username: ";
                getline(cin, username); // get username
                cout << "Password: ";
                string password;
                getline(cin, password); // get password

                send_command(sock, username + "\n"); // send username
                send_command(sock, password + "\n"); // send password

                string response = read_line(sock); // read response
                if (response == "OK\n") {
                    logged_in = true; // set logged in
                    cout << "Login successful." << endl;
                } else {
                    cout << "Login failed." << endl;
                }
            } else if (input == "QUIT") {
                send_command(sock, "QUIT\n"); // send quit command
                break;
            } else {
                cout << "Unknown command." << endl;
            }
        } else {
            cout << "Enter command (SEND, LIST, READ, DEL, QUIT): ";
            getline(cin, input);

            if (input == "SEND") {
                send_command(sock, "SEND\n"); // send send command

                string receiver, subject, message, line, fileask, filename, filepath, file;
                cout << "Receiver: ";
                getline(cin, receiver); // get receiver
                cout << "Subject: ";
                getline(cin, subject); // get subject
                if (subject.length() > 80) {
                    subject = subject.substr(0, 80); // truncate subject
                    cout << "Subject truncated to 80 characters." << endl;
                }
                cout << "Message (end with a single '.'): " << endl;
                while (getline(cin, line)) {
                    if (line == ".") break; // end of message
                    message += line + "\n"; // append line to message
                }

                cout << "Do you want to send a file?[y|n]";
            getline(cin, fileask);
            if(fileask == "y" || fileask == "Y"){
                cout << "Filepath:";
                getline(cin, filepath);
                cout << "Filename:";
                getline(cin, filename);

                ifstream inputFile(filepath + "/" + filename, std::ios::in);
                if (inputFile.is_open())
                {
                    cout << "File opened!" << endl;
                }
                else
                    cerr << "File could not be opened!" << endl;

                std::ostringstream ss;
                std::string line;
                while (std::getline(inputFile, line)) {
                    ss << line << std::endl;
                }
                file = ss.str();
                inputFile.close();
            }

                send_command(sock, receiver + "\n"); // send receiver
                send_command(sock, subject + "\n"); // send subject
                send_command(sock, filename + "\n");
                send_command(sock, message); // send message body
                send_command(sock, ".\n"); // indicate end of message
                send_command(sock, file);
                send_command(sock, "6943\n");

                string response = read_line(sock); // read response
                cout << response;
            } else if (input == "LIST") {
                send_command(sock, "LIST\n"); // send list command

                string count_str = read_line(sock); // read number of messages
                if (count_str.empty()) {
                    cout << "Error: No response from server." << endl;
                    break;
                }
                cout << "Number of messages: " << count_str;
                int num = stoi(count_str);
                for (int i = 0; i < num; ++i) {
                    string subject = read_line(sock); // read each subject
                    cout << subject;
                }
            } else if (input == "READ") {
                send_command(sock, "READ\n"); // send read command
                string msg_num;
                cout << "Message Number: ";
                getline(cin, msg_num); // get message number
                send_command(sock, msg_num + "\n"); // send message number

                string response = read_line(sock); // read response
                if (response == "OK\n") {
                    while (true) {
                        response = read_line(sock); // read message content
                        if (response == ".\n" || response.empty()) break; // end of message
                        cout << response;
                    }
                } else {
                    cout << "Error reading message." << endl;
                }
            } else if (input == "DEL") {
                send_command(sock, "DEL\n"); // send delete command
                string msg_num;
                cout << "Message Number: ";
                getline(cin, msg_num); // get message number
                send_command(sock, msg_num + "\n"); // send message number

                string response = read_line(sock); // read response
                cout << response;
            } else if (input == "QUIT") {
                send_command(sock, "QUIT\n"); // send quit command
                break;
            } else {
                cout << "Unknown command." << endl;
            }
        }
    }
}

void send_command(int sock, const string& command) {
    const char* data = command.c_str(); // get c string
    size_t total_sent = 0;
    size_t data_len = command.length();

    while(total_sent < data_len) {
        ssize_t sent = send(sock, data + total_sent, data_len - total_sent, 0); // send data
        if (sent <= 0) {
            perror("send"); // error in sending
            break;
        }
        total_sent += sent; // update total sent
    }
}

string read_line(int sock) {
    static string buffer;
    while (true) {
        size_t pos = buffer.find('\n'); // find newline
        if (pos != string::npos) {
            string line = buffer.substr(0, pos + 1); // get line
            buffer.erase(0, pos + 1); // remove from buffer
            return line; // return line
        }
        char temp[1];
        int valread = recv(sock, temp, 1, 0); // read one character
        if (valread <= 0) {
            return ""; // connection closed or error
        }
        buffer += temp[0]; // append to buffer
    }
}

