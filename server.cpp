#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>


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

#pragma comment(lib, "ws2_32.lib")

using namespace std;

#define CONTROL_COMMAND_PORT 8090
#define TELEMETRY_PORT 8091
#define FILE_TRANSFER_PORT 8082
#define BUFFER_SIZE 1024

map<string, pair<string, int>> mp;

string xorEncryptDecrypt(const string &message, char key)
{
    string result = message;
    for (int i = 0; i < message.size(); i++)
    {
        result[i] = message[i] ^ key;
    }
    return result;
}

// Function to handle control commands (UDP)
void handleControlCommands()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed." << endl;
        return;
    }

    // Create a UDP socket
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(CONTROL_COMMAND_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket
    if (bind(udpSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(udpSocket);
        WSACleanup();
        return;
    }

    cout << "Control Commands server listening on port " << CONTROL_COMMAND_PORT << endl;

    while (1)
    {
        string drone_name;
        cout << "Enter drone name: ";
        cin >> drone_name;
        cout << drone_name << endl;
        
        char control_command[1000];
        cout << "Enter command: ";
        cin.ignore(); // Clear the input buffer
        cin.getline(control_command, 1000);
        
        if (mp.find(drone_name) == mp.end())
        {
            cout << "INVALID DRONE_NAME" << endl;
            continue;
        }
        
        string cc = control_command;
        if (cc.find("update") != string::npos)
        {
            string valueStr = cc.substr(7); // Extract substring after "update "
            try
            {
                int newSpeed = stoi(valueStr);
                cout << "Updating speed to: " << newSpeed << endl;
            }
            catch (invalid_argument &e)
            {
                cout << "INVALID COMMAND - invalid speed value" << endl;
                continue;
            }
        }
        else if(cc == "send pic")
        {
            cout << "Requesting file transfer from drone" << endl;
        }
        else
        {
            cout << "INVALID COMMAND" << endl;
            continue;
        }
        
        cout << "Sending Control Command to: " << drone_name << ' ' << mp[drone_name].second << ' ' << mp[drone_name].first << endl;
        
        sockaddr_in clientAddr;
        clientAddr.sin_family = AF_INET;
        clientAddr.sin_port = htons(mp[drone_name].second);
        inet_pton(AF_INET, (mp[drone_name].first).c_str(), &clientAddr.sin_addr);
        
        string encryptedCommand = xorEncryptDecrypt(control_command, 'K');

        if (sendto(udpSocket, encryptedCommand.c_str(), encryptedCommand.length(), 0,
                   (struct sockaddr *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
        {
            cerr << "Send failed: " << WSAGetLastError() << endl;
        }
        else
        {
            cout << "Control command sent successfully" << endl;
        }
    }

    closesocket(udpSocket);
    WSACleanup();
}

void connectClient(SOCKET new_socket, string clientIP, int clientPort)
{
    char buffer[BUFFER_SIZE] = {0};
    int valread;
    string drone_name;
    
    while ((valread = recv(new_socket, buffer, BUFFER_SIZE, 0)) > 0)
    {
        // Decrypt telemetry data
        string encryptedTelemetry(buffer, valread);
        string telemetry = xorEncryptDecrypt(encryptedTelemetry, 'K');
        cout << "Received Telemetry: " << telemetry << endl;
        
        if(telemetry[0] == '1')
        {
            drone_name = telemetry.substr(2);
            mp[drone_name] = {clientIP, clientPort};
            cout << "Connected To Drone: " << drone_name << ' ' << clientIP << ' ' << clientPort << endl;
            memset(buffer, 0, BUFFER_SIZE);
            continue;
        }
        
        istringstream iss(telemetry);
        string data1, data2;
        string x, y;

        // Extract components from the string
        iss >> data1 >> x >> data2 >> y;
        cout << "Telemetry data received from " << drone_name << ": " << data1 << ": " << x << ' ' << data2 << ": " << y << endl;

        memset(buffer, 0, BUFFER_SIZE);
    }

    mp.erase(drone_name);
    cout << "Connection closed by client: " << drone_name << endl;
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
    
    while (true)
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
    char buffer[BUFFER_SIZE] = {0};
    string filename = "";

    cout << "File transfer from: " << clientIP << ':' << clientPort << endl;
    
    for(auto x : mp)
    {
        if(((x.second).first == clientIP))
        {
            filename = x.first + ".txt";
            break;
        }
    }

    if(filename == "")
    {
        cout << "Drone sending file is not Active" << endl;
        closesocket(new_socket);
        return;
    }
    
    cout << "Receiving file: " << filename << endl;
    ofstream file(filename, ios::out | ios::binary);

    // Read file in chunks
    int valread;
    while ((valread = recv(new_socket, buffer, BUFFER_SIZE, 0)) > 0) 
    {
        string encryptedChunk(buffer, valread);
        string chunk = xorEncryptDecrypt(encryptedChunk, 'K');
        file.write(chunk.c_str(), chunk.size());
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

    while (true) 
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

