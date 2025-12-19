#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <limits>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <csignal>
#include <atomic>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <cstring>
#include <string>
#include <fstream>
#include <map>
#include <utility>
#include <sstream>
#include <mutex>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

#define CONTROL_COMMAND_PORT 8090
#define TELEMETRY_PORT 8091
#define FILE_TRANSFER_PORT 8082
#define BUFFER_SIZE 1024

const unsigned char AES_KEY_128[16] = {
    'D','R','O','N','E','G','U','A','R','D','1','2','3','4','5','6'
};

atomic<bool> running(true);
mutex mp_mutex;
map<string, pair<string, int>> mp;


void handleSignal(int)
{
    running = false;
}


//AES Encryption and Decryption functions
vector<unsigned char> aesEncrypt(
    const string &plaintext,
    unsigned char *iv_out
) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    RAND_bytes(iv_out, 16);

    vector<unsigned char> ciphertext(plaintext.size() + 16);
    int len, ciphertext_len;

    EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, AES_KEY_128, iv_out);
    EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                      (unsigned char*)plaintext.data(), plaintext.size());
    ciphertext_len = len;

    EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len);
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    ciphertext.resize(ciphertext_len);
    return ciphertext;
}

string aesDecrypt(
    const unsigned char *ciphertext,
    int ciphertext_len,
    const unsigned char *iv
) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    vector<unsigned char> plaintext(ciphertext_len);
    int len, plaintext_len;

    EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, AES_KEY_128, iv);
    EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len);
    plaintext_len = len;

    EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return string((char*)plaintext.data(), plaintext_len);
}

void showConnectedDrones()
{
    cout << "\nConnected Drones:\n";

    lock_guard<mutex> lock(mp_mutex);
    for (auto &d : mp)
    {
        cout << "- " << d.first
             << " (" << d.second.first
             << ":" << d.second.second << ")\n";
    }

    cout << endl;
}

void handleControlCommands()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed." << endl;
        return;
    }

    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(CONTROL_COMMAND_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    cout << "Control Commands server listening on port "
         << CONTROL_COMMAND_PORT << endl;

    while (running)
    {
        showConnectedDrones();

        string drone_name;
        cout << "Enter drone name: ";
        cin >> drone_name;

        cin.ignore(numeric_limits<streamsize>::max(), '\n');

        char control_command[1000];
        cout << "\nAvailable Commands:\n";
        cout << "1. update <speed>\n";
        cout << "2. send pic\n";
        cout << "Enter command: ";
        cin.getline(control_command, sizeof(control_command));

        pair<string,int> droneInfo;
        {
            lock_guard<mutex> lock(mp_mutex);
            auto it = mp.find(drone_name);
            if (it == mp.end())
            {
                cout << "INVALID DRONE_NAME" << endl;
                continue;
            }
            droneInfo = it->second;
        }

        string cc = control_command;
        if (cc.find("update") != string::npos)
        {
            try
            {
                stoi(cc.substr(7));
            }
            catch (...)
            {
                cout << "INVALID COMMAND" << endl;
                continue;
            }
        }
        else if (cc != "send pic")
        {
            cout << "INVALID COMMAND" << endl;
            continue;
        }

        sockaddr_in clientAddr{};
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(droneInfo.second);
        inet_pton(AF_INET, droneInfo.first.c_str(), &clientAddr.sin_addr);

        time_t now = time(nullptr);
        string payload = to_string(now) + "|" + cc;

        unsigned char iv[16];
        auto encrypted = aesEncrypt(payload, iv);

        if (sendto(udpSocket, (char*)iv, 16, 0,
                   (sockaddr*)&clientAddr, sizeof(clientAddr)) <= 0)
        {
            cerr << "Failed to send IV" << endl;
            continue;
        }

        if (sendto(udpSocket, (char*)encrypted.data(), encrypted.size(), 0,
                   (sockaddr*)&clientAddr, sizeof(clientAddr)) <= 0)
        {
            cerr << "Failed to send encrypted command" << endl;
        }
        else
        {
            cout << "Control command sent successfully (AES)"
                 << endl;
        }
    }

    closesocket(udpSocket);
    WSACleanup();
}
void connectClient(SOCKET new_socket, string clientIP, int clientPort)
{
    char buffer[BUFFER_SIZE];
    string drone_name;

    ofstream log("telemetry.log", ios::app);

    while (running)
    {
        unsigned char iv[16];
        if (recv(new_socket, (char*)iv, 16, 0) != 16)
            break;

        int n = recv(new_socket, buffer, BUFFER_SIZE, 0);
        if (n <= 0)
            break;

        string telemetry = aesDecrypt((unsigned char*)buffer, n, iv);

        // Handle REGISTER
        if (telemetry.find("REGISTER") == 0)
        {
            istringstream iss(telemetry);
            string tag;
            int controlPort;

            iss >> tag >> drone_name >> controlPort;

            {
                lock_guard<mutex> lock(mp_mutex);
                mp[drone_name] = {clientIP, controlPort};
            }

            cout << "Connected To Drone: "
                 << drone_name << " "
                 << clientIP << " "
                 << controlPort << endl;

            continue;
        }

        // Handle telemetry data
        istringstream iss(telemetry);
        string l1, l2, v1, v2;
        iss >> l1 >> v1 >> l2 >> v2;

        log << drone_name << " "
            << l1 << ": " << v1 << " "
            << l2 << ": " << v2 << endl;
    }

    // Graceful disconnect cleanup
    if (!drone_name.empty())
    {
        lock_guard<mutex> lock(mp_mutex);
        mp.erase(drone_name);
    }

    cout << "Connection closed by client: " << drone_name << endl;

    log.close();
    closesocket(new_socket);
}


// Function to handle telemetry data (TCP)
void handleTelemetry()
{
    WSADATA wsaData;
    SOCKET server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return;
    }

    // Create a TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(TELEMETRY_PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
    {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return;
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) == SOCKET_ERROR)
    {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return;
    }
    
    cout << "Telemetry server listening on port " << TELEMETRY_PORT << endl;
    
    while (running)
    {
        struct sockaddr_in client_address;
        int client_addrlen = sizeof(client_address);

        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen)) == INVALID_SOCKET)
        {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        // Get client IP address and port
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_address.sin_addr), clientIP, INET_ADDRSTRLEN);
        int clientPort = ntohs(client_address.sin_port);

        cout << "New telemetry client connected: " << clientIP << ":" << clientPort << endl;

        // Create a new thread to handle the client connection
        thread clientThread(connectClient, new_socket, string(clientIP), clientPort);
        clientThread.detach();
    }

    closesocket(server_fd);
    WSACleanup();
}

void fileReceive(SOCKET new_socket, string clientIP, int clientPort)
{
    char buffer[BUFFER_SIZE];
    string filename;

    cout << "File transfer from: " << clientIP << ':' << clientPort << endl;

    {
        lock_guard<mutex> lock(mp_mutex);
        for (auto &x : mp)
        {
            if (x.second.first == clientIP)
            {
                filename = x.first + ".txt";
                break;
            }
        }
    }

    if (filename.empty())
    {
        cout << "Drone sending file is not Active" << endl;
        closesocket(new_socket);
        return;
    }

    cout << "Receiving file: " << filename << endl;
    ofstream file(filename, ios::out | ios::binary);

    while (running)
    {
        unsigned char iv[16];
        if (recv(new_socket, (char*)iv, 16, 0) != 16)
            break;

        int n = recv(new_socket, buffer, BUFFER_SIZE, 0);
        if (n <= 0)
            break;

        string chunk = aesDecrypt((unsigned char*)buffer, n, iv);
        file.write(chunk.data(), chunk.size());
    }

    file.close();
    cout << "File received successfully: " << filename << endl;
    closesocket(new_socket);
}


// Function to handle file transfers (TCP)
void handleFileTransfer() 
{
    WSADATA wsaData;
    SOCKET server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) 
    {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return;
    }

    // Create a TCP socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) 
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(FILE_TRANSFER_PORT);

    // Bind the socket
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR) 
    {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return;
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) == SOCKET_ERROR) 
    {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closesocket(server_fd);
        WSACleanup();
        return;
    }

    cout << "File Transfer server listening on port " << FILE_TRANSFER_PORT << endl;

    while (running) 
    {
        struct sockaddr_in client_address;
        int client_addrlen = sizeof(client_address);

        if ((new_socket = accept(server_fd, (struct sockaddr *)&client_address, &client_addrlen)) == INVALID_SOCKET) 
        {
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_address.sin_addr), clientIP, INET_ADDRSTRLEN);
        int clientPort = ntohs(client_address.sin_port);

        cout << "New file transfer client: " << clientIP << ":" << clientPort << endl;

        thread fileReceiveThread(fileReceive, new_socket, string(clientIP), clientPort);
        fileReceiveThread.detach();
    }

    closesocket(server_fd);
    WSACleanup();
}

int main()
{
    signal(SIGINT, handleSignal);
    cout << "Starting Drone Control Server..." << endl;
    
    // Start threads for each mode of communication
    thread controlCommandThread(handleControlCommands);
    thread telemetryThread(handleTelemetry);
    thread fileTransferThread(handleFileTransfer);

    // Wait for all threads to finish
    controlCommandThread.join();
    telemetryThread.join();
    fileTransferThread.join();

    return 0;
}

