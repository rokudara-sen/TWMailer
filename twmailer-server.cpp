// include all necessary headers
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <unistd.h> // for close()
#include <netinet/in.h> // for sockaddr_in
#include <sys/socket.h> // for socket functions
#include <sys/types.h>
#include <dirent.h> // for directory operations
#include <arpa/inet.h> // for inet_ntoa
#include <sys/stat.h> // for mkdir
#include <ctime> // for time()
#include <mutex> // for mutexes
#include <thread> // for threading
#include <unordered_map> // for blacklist
#include <ldap.h> // for ldap functions

#define BUFFER_SIZE 1024 // define buffer size
#define LDAP_DEPRECATED 1 // allow deprecated ldap functions

using namespace std;

// global variables
string mail_spool_dir; // directory for mail spool
mutex blacklist_mutex; // mutex for blacklist
unordered_map<string, time_t> blacklist; // ip blacklist
mutex mail_mutex; // mutex for mail operations

// function declarations
void handle_client(int client_sock, sockaddr_in client_addr);
string read_line(int sock);
void send_response(int sock, const string& response);
void process_login(int sock, string& username, sockaddr_in client_addr, bool& authenticated);
void process_send(int sock, const string& username);
void process_list(int sock, const string& username);
void process_read(int sock, const string& username);
void process_del(int sock, const string& username);
bool authenticate_user(const string& username, const string& password);
bool is_blacklisted(const string& ip);
void update_blacklist(const string& ip);
void persist_blacklist();
void load_blacklist();

int main(int argc, char *argv[]) {
    // check if correct number of arguments is provided
    if (argc != 3) {
        cerr << "Usage: ./twmailer-server <port> <mail-spool-directoryname>" << endl;
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]); // get port number
    mail_spool_dir = argv[2]; // get mail spool directory name

    // load the blacklist from file
    load_blacklist();

    // create mail spool directory if it doesn't exist
    mkdir(mail_spool_dir.c_str(), 0777);

    int server_sock, client_sock; // socket descriptors
    struct sockaddr_in server_addr{}, client_addr{}; // addresses
    socklen_t client_len = sizeof(client_addr); // client address length

    // create socket
    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    // bind socket to server address
    server_addr.sin_family = AF_INET; // ipv4
    server_addr.sin_addr.s_addr = INADDR_ANY; // any incoming interface
    server_addr.sin_port = htons(port); // port number
    if (::bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // listen for incoming connections
    if (listen(server_sock, 10) < 0) {
        perror("Listen");
        exit(EXIT_FAILURE);
    }

    cout << "Server is listening on port " << port << endl;

    while (true) {
        // accept incoming connections
        if ((client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len)) < 0) {
            perror("Accept");
            exit(EXIT_FAILURE);
        }
        cout << "Connection accepted" << endl;

        // handle client in a new thread
        thread t(handle_client, client_sock, client_addr);
        t.detach(); // detach the thread
    }

    return 0;
}

void handle_client(int client_sock, sockaddr_in client_addr) {
    string command; // store client command
    string username; // store username after login
    bool authenticated = false; // authentication status
    int login_attempts = 0; // count login attempts
    string client_ip = inet_ntoa(client_addr.sin_addr); // get client ip

    while (true) {
        // check if client ip is blacklisted
        if (is_blacklisted(client_ip)) {
            send_response(client_sock, "ERR\n"); // send error response
            close(client_sock); // close connection
            cout << "Connection closed (blacklisted IP)" << endl;
            return;
        }

        command = read_line(client_sock); // read command from client

        if (command == "LOGIN") {
            process_login(client_sock, username, client_addr, authenticated); // process login
            if (!authenticated) {
                login_attempts++; // increment login attempts
                if (login_attempts >= 3) {
                    update_blacklist(client_ip); // add to blacklist
                    send_response(client_sock, "ERR\n"); // send error
                    close(client_sock); // close connection
                    cout << "Connection closed (too many login attempts)" << endl;
                    return;
                }
            }
        } else if (command == "SEND") {
            if (authenticated) {
                process_send(client_sock, username); // process send
            } else {
                send_response(client_sock, "ERR\n"); // send error
            }
        } else if (command == "LIST") {
            if (authenticated) {
                process_list(client_sock, username); // process list
            } else {
                send_response(client_sock, "ERR\n"); // send error
            }
        } else if (command == "READ") {
            if (authenticated) {
                process_read(client_sock, username); // process read
            } else {
                send_response(client_sock, "ERR\n"); // send error
            }
        } else if (command == "DEL") {
            if (authenticated) {
                process_del(client_sock, username); // process delete
            } else {
                send_response(client_sock, "ERR\n"); // send error
            }
        } else if (command == "QUIT") {
            // no response for quit, close connection
            break;
        } else {
            send_response(client_sock, "ERR\n"); // unknown command
        }
    }

    close(client_sock); // close client socket
    cout << "Connection closed" << endl;
}

string read_line(int sock) {
    char c;
    string line;
    while (recv(sock, &c, 1, 0) > 0) { // read one character at a time
        if (c == '\n') break; // end of line
        line += c; // append character to line
    }
    return line; // return the line
}

void send_response(int sock, const string& response) {
    const char* data = response.c_str(); // get c string
    size_t total_sent = 0;
    size_t data_len = response.length();

    while(total_sent < data_len) {
        ssize_t sent = send(sock, data + total_sent, data_len - total_sent, 0); // send data
        if (sent <= 0) {
            perror("send"); // error in sending
            break;
        }
        total_sent += sent; // update total sent
    }
}

void process_login(int sock, string& username, sockaddr_in client_addr, bool& authenticated) {
    username = read_line(sock); // read username
    string password = read_line(sock); // read password
    string client_ip = inet_ntoa(client_addr.sin_addr); // get client ip

    if (authenticate_user(username, password)) { // authenticate user
        authenticated = true; // set authenticated to true
        send_response(sock, "OK\n"); // send ok response
    } else {
        authenticated = false; // authentication failed
        send_response(sock, "ERR\n"); // send error response
    }
}

bool authenticate_user(const string& username, const string& password) {
    LDAP *ld; // ldap connection
    int rc; // return code

    // LDAP server URI and user DN
    string ldap_uri = "ldap://ldap.technikum-wien.at"; // ldap server uri
    string ldap_bind_dn = "uid=" + username + ",ou=people,dc=technikum-wien,dc=at"; // user's DN

    // Initialize LDAP connection
    rc = ldap_initialize(&ld, ldap_uri.c_str());
    if (rc != LDAP_SUCCESS) {
        cerr << "LDAP initialization failed: " << ldap_err2string(rc) << endl;
        return false;
    }

    // Set LDAP version
    int version = LDAP_VERSION3; // ldap version 3
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version); // set ldap version

    // Start TLS
    rc = ldap_start_tls_s(ld, NULL, NULL);
    if (rc != LDAP_SUCCESS) {
        ldap_unbind_ext_s(ld, NULL, NULL);
        cerr << "ldap_start_tls_s() failed: " << ldap_err2string(rc) << endl;
        return false;
    }

    // Prepare credentials
    struct berval cred;
    cred.bv_val = (char*)password.c_str();  // set password
    cred.bv_len = password.length();

    // Attempt to bind using the user's DN and password
    rc = ldap_sasl_bind_s(ld, ldap_bind_dn.c_str(), LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);

    // Clean up
    ldap_unbind_ext_s(ld, NULL, NULL); // unbind ldap

    if (rc == LDAP_SUCCESS) {
        return true; // authentication successful
    } else {
        cerr << "LDAP bind failed: " << ldap_err2string(rc) << endl;
        return false; // authentication failed
    }
}

void process_send(int sock, const string& username) {
    string receiver = read_line(sock); // read receiver
    string subject = read_line(sock); // read subject
    string filename = read_line(sock);
    string message, line, file;

    // truncate subject if necessary
    if (subject.length() > 80) {
        subject = subject.substr(0, 80); // limit to 80 chars
    }

    // read message until a single dot '.\n' is encountered
    while ((line = read_line(sock)) != ".") {
        message += line + "\n"; // append line to message
    }
    while ((line = read_line(sock)) != "6943") {
        file += line + "\n"; 
    }

    // lock mutex before accessing mail spool
    mail_mutex.lock();

    // save the message
    string user_dir = mail_spool_dir + "/" + receiver;
    mkdir(user_dir.c_str(), 0777); // create user directory if not exists

    std::ofstream outFile(user_dir + "/" + filename, std::ios::out);
    if (!outFile && filename != "") {
        cerr << "Failed to open file: " + filename << endl;
    }
    outFile << file;
    outFile.close();

    // count existing messages
    int msg_count = 0;
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir(user_dir.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) msg_count++; // count files
        }
        closedir(dir);
    }

    string msg_filename = user_dir + "/" + to_string(msg_count + 1) + ".txt"; // message filename
    ofstream msg_file(msg_filename);
    if (msg_file.is_open()) {
        msg_file << "From: " << username << "\n"; // write sender
        msg_file << "To: " << receiver << "\n"; // write receiver
        msg_file << "Subject: " << subject << "\n"; // write subject
        msg_file << "Filename: " << filename << "\n";
        msg_file << message; // write message body
        msg_file.close(); // close file
        send_response(sock, "OK\n"); // send ok response
    } else {
        send_response(sock, "ERR\n"); // send error response
    }

    mail_mutex.unlock(); // unlock mutex
}

void process_list(int sock, const string& username) {
    // lock mutex before accessing mail spool
    mail_mutex.lock();

    string user_dir = mail_spool_dir + "/" + username;
    vector<string> subjects;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(user_dir.c_str())) != NULL) {
        // read all files in user directory
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_type == DT_REG) {
                string filepath = user_dir + "/" + ent->d_name;
                ifstream msg_file(filepath);
                string line;
                while (getline(msg_file, line)) {
                    if (line.find("Subject: ") == 0) {
                        subjects.push_back(line.substr(9)); // extract subject
                        break;
                    }
                }
                msg_file.close();
            }
        }
        closedir(dir);
    }

    mail_mutex.unlock(); // unlock mutex

    send_response(sock, to_string(subjects.size()) + "\n"); // send number of messages
    for (const auto& subject : subjects) {
        send_response(sock, subject + "\n"); // send each subject
    }
}

void process_read(int sock, const string& username) {
    string msg_num = read_line(sock); // read message number

    // lock mutex before accessing mail spool
    mail_mutex.lock();

    string filepath = mail_spool_dir + "/" + username + "/" + msg_num + ".txt";

    ifstream msg_file(filepath);
    if (msg_file.is_open()) {
        send_response(sock, "OK\n"); // send ok response
        string line;
        while (getline(msg_file, line)) {
            send_response(sock, line + "\n"); // send each line of message
        }
        msg_file.close();
        send_response(sock, ".\n"); // indicate end of message
    } else {
        send_response(sock, "ERR\n"); // send error response
    }

    mail_mutex.unlock(); // unlock mutex
}

void process_del(int sock, const string& username) {
    string msg_num = read_line(sock); // read message number

    // lock mutex before accessing mail spool
    mail_mutex.lock();

    string filepath = mail_spool_dir + "/" + username + "/" + msg_num + ".txt";

    if (remove(filepath.c_str()) == 0) {
        send_response(sock, "OK\n"); // send ok response
    } else {
        send_response(sock, "ERR\n"); // send error response
    }

    mail_mutex.unlock(); // unlock mutex
}

bool is_blacklisted(const string& ip) {
    lock_guard<mutex> lock(blacklist_mutex); // lock mutex
    auto it = blacklist.find(ip);
    if (it != blacklist.end()) {
        time_t current_time = time(nullptr);
        if (current_time - it->second < 60) { // check if within 1 minute
            return true; // ip is blacklisted
        } else {
            blacklist.erase(it); // remove from blacklist
            persist_blacklist(); // update blacklist file
            return false; // ip is not blacklisted
        }
    }
    return false; // ip is not blacklisted
}

void update_blacklist(const string& ip) {
    lock_guard<mutex> lock(blacklist_mutex); // lock mutex
    blacklist[ip] = time(nullptr); // add ip to blacklist with current time
    persist_blacklist(); // update blacklist file
}

void persist_blacklist() {
    ofstream blacklist_file("blacklist.txt"); // open blacklist file
    for (const auto& entry : blacklist) {
        blacklist_file << entry.first << " " << entry.second << "\n"; // write each entry
    }
    blacklist_file.close(); // close file
}

void load_blacklist() {
    ifstream blacklist_file("blacklist.txt"); // open blacklist file
    if (!blacklist_file.is_open()) {
        return; // file not found, return
    }
    string ip;
    time_t timestamp;
    while (blacklist_file >> ip >> timestamp) {
        blacklist[ip] = timestamp; // load each entry
    }
    blacklist_file.close(); // close file
}

