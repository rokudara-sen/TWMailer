#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/stat.h>  // Added for mkdir

#define BUFFER_SIZE 1024

using namespace std;

string mail_spool_dir;

void handle_client(int client_sock);
string read_line(int sock);
void send_response(int sock, const string& response);
void process_send(int sock);
void process_list(int sock);
void process_read(int sock);
void process_del(int sock);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>" << endl;
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    mail_spool_dir = argv[2];

    // Create mail spool directory if it doesn't exist
    mkdir(mail_spool_dir.c_str(), 0777);

    int server_sock, client_sock;
    struct sockaddr_in server_addr{}, client_addr{};
    socklen_t client_len = sizeof(client_addr);

    // Create socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (::bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_sock, 3) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    cout << "Server is listening on port " << port << endl;

    while (true) {
        // Accept
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            perror("Accept");
            exit(EXIT_FAILURE);
        }
        cout << "Connection accepted" << endl;
        handle_client(client_sock);
        close(client_sock);
        cout << "Connection closed" << endl;
    }

    return 0;
}

void handle_client(int client_sock) {
    string command;
    while (true) {
        command = read_line(client_sock);
        if (command == "SEND") {
            process_send(client_sock);
        } else if (command == "LIST") {
            process_list(client_sock);
        } else if (command == "READ") {
            process_read(client_sock);
        } else if (command == "DEL") {
            process_del(client_sock);
        } else if (command == "QUIT") {
            // No response is sent for QUIT; close the connection
            break;
        } else {
            send_response(client_sock, "ERR\n");
        }
    }
}

string read_line(int sock) {
    char c;
    string line;
    while (recv(sock, &c, 1, 0) > 0) {
        if (c == '\n') break;
        line += c;
    }
    return line;
}

void send_response(int sock, const string& response) {
    	const char* data = response.c_str();
        size_t total_sent = 0;
        size_t data_len = response.length();

        while(total_sent < data_len) {
                ssize_t sent = send(sock, data + total_sent, data_len - total_sent, 0);
                if (sent <= 0)  {
                        perror("send");
                        break;
                }
                total_sent += sent;
        }
}

void process_send(int sock) {
    string sender = read_line(sock);
    string receiver = read_line(sock);
    string subject = read_line(sock);
    string message, line;

    // Read message until a single dot '.\n' is encountered
    while ((line = read_line(sock)) != ".") {
        message += line + "\n";
    }

    // Save the message
    string user_dir = mail_spool_dir + "/" + receiver;
    mkdir(user_dir.c_str(), 0777);

    // Count existing messages
    int msg_count = 0;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(user_dir.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) msg_count++;
        }
        closedir(dir);
    }

    string msg_filename = user_dir + "/" + to_string(msg_count + 1) + ".txt";
    ofstream msg_file(msg_filename);
    if (msg_file.is_open()) {
        msg_file << "From: " << sender << "\n";
        msg_file << "To: " << receiver << "\n";
        msg_file << "Subject: " << subject << "\n";
        msg_file << message;
        msg_file.close();
        send_response(sock, "OK\n");
    } else {
        send_response(sock, "ERR\n");
    }
}

void process_list(int sock) {
    string username = read_line(sock);
    string user_dir = mail_spool_dir + "/" + username;
    vector<string> subjects;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(user_dir.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {
                string filepath = user_dir + "/" + ent->d_name;
                ifstream msg_file(filepath);
                string line;
                while (getline(msg_file, line)) {
                    if (line.find("Subject: ") == 0) {
                        subjects.push_back(line.substr(9));
                        break;
                    }
                }
                msg_file.close();
            }
        }
        closedir(dir);
    }

    send_response(sock, to_string(subjects.size()) + "\n");
    for (const auto& subject : subjects) {
        send_response(sock, subject + "\n");
    }
}

void process_read(int sock) {
    string username = read_line(sock);
    string msg_num = read_line(sock);
    string filepath = mail_spool_dir + "/" + username + "/" + msg_num + ".txt";

    ifstream msg_file(filepath);
    if (msg_file.is_open()) {
        send_response(sock, "OK\n");
        string line;
        while (getline(msg_file, line)) {
            send_response(sock, line + "\n");
        }
        msg_file.close();
        send_response(sock, ".\n"); // Indicate end of message
    } else {
        send_response(sock, "ERR\n");
    }
}

void process_del(int sock) {
    string username = read_line(sock);
    string msg_num = read_line(sock);
    string filepath = mail_spool_dir + "/" + username + "/" + msg_num + ".txt";

    if (remove(filepath.c_str()) == 0) {
        send_response(sock, "OK\n");
    } else {
        send_response(sock, "ERR\n");
    }
}

