#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <csignal>
#include <atomic>

#define WIN32_LEAN_AND_MEAN
#include <iostream>
#include <thread>
#include <fstream>
#include <string>
#include <chrono>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

#define CONTROL_COMMAND_PORT 8090
#define TELEMETRY_PORT 8091
#define FILE_TRANSFER_PORT 8082
#define BUFFER_SIZE 1024

const unsigned char AES_KEY_128[16] = {
    'D','R','O','N','E','G','U','A','R','D','1','2','3','4','5','6'
};

string drone_name;
int drone_port;
atomic<bool> running = true;

class Drone
{
public:
    string name;
    int port;
    int position;
    int speed;

    // Default constructor
    Drone() : name(""), port(0), position(0), speed(0) {}

    // Parameterized constructor
    Drone(string name, int port) : name(name), port(port), speed(0)
    {
        position = 0;
    }

    // Function to update drone's speed
    void Speed(int speed)
    {
        this->speed = speed;
    }

    // Function to get telemetry data as a string
    string getTelemetryData() const
    {
        return "Position: " +
               to_string(position) + " Speed: " +
               to_string(speed);
    }

    void UpdatePosition()
    {
        while (running)
        {
            position += speed;
            this_thread::sleep_for(chrono::seconds(1));
        }
    }
};

Drone drone;

void ltrim(string &str)
{
    size_t start = str.find_first_not_of(" \t\n\r\f\v");
    if (start != string::npos)
    {
        str.erase(0, start);
    }
    else
    {
        str.clear();
    }
}

void rtrim(string &str)
{
    size_t end = str.find_last_not_of(" \t\n\r\f\v");
    if (end != string::npos)
    {
        str.erase(end + 1);
    }
    else
    {
        str.clear();
    }
}

void handleSignal(int)
{
    running = false;
    
}

//AES helper functions
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


// Function to send files to server
void FileTransferClient() 
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) 
    {
        cerr << "WSAStartup failed: " << WSAGetLastError() << endl;
        return;
    }

    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == INVALID_SOCKET) 
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(FILE_TRANSFER_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(client_fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) 
    {
        cerr << "Connection failed: " << WSAGetLastError() << endl;
        closesocket(client_fd);
        WSACleanup();
        return;
    }
    
    const string fileName = "DroneData.txt";
    
    // Create a sample file if it doesn't exist
    ofstream createFile(fileName);
    if (createFile.is_open()) 
    {
        createFile << "Drone Image Data for " << drone_name << "\n";
        createFile << "Timestamp: " << chrono::duration_cast<chrono::seconds>(
            chrono::system_clock::now().time_since_epoch()).count() << "\n";
        createFile << "Position: " << drone.position << "\n";
        createFile << "Speed: " << drone.speed << "\n";
        createFile << "Image: [Binary data would be here]\n";
        createFile.close();
    }
    
    // Open the file to read its content
    ifstream file(fileName, ios::in | ios::binary);
    if (!file.is_open()) 
    {
        cerr << "Error: Could not open file " << fileName << endl;
        closesocket(client_fd);
        WSACleanup();
        return;
    }

    char buffer[BUFFER_SIZE];
    cout << "Sending file: " << fileName << endl;

    // Send the file in chunks
    while (!file.eof()) 
    {
        file.read(buffer, BUFFER_SIZE);
        streamsize bytesRead = file.gcount();

        if (bytesRead > 0)
        {
            unsigned char iv[16];

        // Encrypt chunk using AES
        auto encryptedChunk = aesEncrypt(string(buffer, bytesRead), iv);

        // Send IV first
        if (send(client_fd, (char*)iv, 16, 0) == SOCKET_ERROR)
        {
            cerr << "Error sending IV: " << WSAGetLastError() << endl;
            break;
        }

        // Send encrypted chunk
        if (send(client_fd,
                (char*)encryptedChunk.data(),
                encryptedChunk.size(),
                0) == SOCKET_ERROR)
        {
            cerr << "Error sending encrypted file chunk: "
                << WSAGetLastError() << endl;
            break;
        }
        }
    }

    cout << "File sent successfully." << endl;

    // Close file and clean up
    file.close();
    closesocket(client_fd);
    WSACleanup();
}

// Function to receive control commands (UDP)
void receiveControlCommands()
{
    cout << "Control Command Thread Started" << endl;
    
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed." << endl;
        return;
    }

    SOCKET sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    sockaddr_in clientAddr;
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(drone_port);
    clientAddr.sin_addr.s_addr = INADDR_ANY;

    // Bind the socket to the client address and port
    if (bind(sockfd, (struct sockaddr *)&clientAddr, sizeof(clientAddr)) == SOCKET_ERROR)
    {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closesocket(sockfd);
        WSACleanup();
        return;
    }
    
    cout << "Control Commands listening on port " << drone_port << endl;
    
    char buffer[BUFFER_SIZE];
    sockaddr_in serverAddr;
    int addrLen = sizeof(serverAddr);
    
    fd_set readfds;
    struct timeval timeout;

    while (running)
    {
        // Clear the set and add the socket to it
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        // Wait for data or timeout
        int activity = select(0, &readfds, NULL, NULL, &timeout);

        if (activity == SOCKET_ERROR)
        {
            cerr << "select error: " << WSAGetLastError() << endl;
            break;
        }

        // Check if there's data to receive
        if (activity > 0 && FD_ISSET(sockfd, &readfds))
        {   
            unsigned char iv[16];
            int ivBytes = recvfrom(sockfd, (char*)iv, 16, 0,
                                (struct sockaddr*)&serverAddr, &addrLen);
            if (ivBytes != 16)
            {
                cerr << "Failed to receive IV for control command" << endl;
                continue;
            }
            // Receive control command from the server
            int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr *)&serverAddr, &addrLen);
            if (n == SOCKET_ERROR)
            {
                cerr << "recvfrom failed: " << WSAGetLastError() << endl;
                continue;
            }

            string decrypted = aesDecrypt((unsigned char*)buffer, n, iv);

            //Split timestamp and command
            size_t sep = decrypted.find('|');
            if (sep == string::npos)
            {
                cerr << "Malformed control command" << endl;
                continue;
            }

            time_t msgTime = stol(decrypted.substr(0, sep));
            string command = decrypted.substr(sep + 1);

            // Replay protection check (5 seconds window)
            time_t now = time(nullptr);
            if (labs(now - msgTime) > 5)
            {
                cerr << "Rejected replayed control command" << endl;
                continue;
            }

            // Output the received control command
            cout << "Received Control Command: " << command << endl;
            ltrim(command);
            rtrim(command);
            
            if (command.find("update") != string::npos)
            {
                string valueStr = command.substr(7); // Extract substring after "update "
                ltrim(valueStr);
                rtrim(valueStr);
                try
                {
                    int newSpeed = stoi(valueStr);
                    drone.Speed(newSpeed);
                    cout << "Updated drone speed to: " << newSpeed << endl;
                }
                catch (invalid_argument &e)
                {
                    cerr << "Invalid speed value received: " << valueStr << endl;
                }
            }
            else if(command == "send pic")
            {
                cout << "Received file transfer request" << endl;
                thread fileTransferThread(FileTransferClient);
                fileTransferThread.detach();
            }
            else
            {
                cout << "Received Unknown Control Command: " << command << endl;
            }
        }

        if (!running)
        {
            cout << "Control command thread exiting..." << endl;
            break;
        }
    }

    closesocket(sockfd);
    WSACleanup();
}

// Function to send telemetry data (TCP)
void sendTelemetryData()
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        cerr << "WSAStartup failed." << endl;
        return;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket == INVALID_SOCKET)
    {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        WSACleanup();
        return;
    }

    // No binding for client socket - let system assign port
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(TELEMETRY_PORT);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(clientSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        cerr << "Connection failed: " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }
    
    // Send initial connection message with drone name
    string td = "REGISTER " + drone_name + " " + to_string(drone_port);

    unsigned char iv[16];
    auto encrypted = aesEncrypt(td, iv);

    send(clientSocket, (char*)iv, 16, 0);
    send(clientSocket, (char*)encrypted.data(), encrypted.size(), 0);

    cout << "Established Connection With Server: " << td << endl;

    this_thread::sleep_for(chrono::seconds(2));

    while (running)
    {
        string telemetryData = drone.getTelemetryData();

        unsigned char iv[16];
        auto encryptedData = aesEncrypt(telemetryData, iv);
        // Send IV first
        int sent = send(clientSocket, (char*)iv, 16, 0);
        if (sent <= 0)
        {
            cerr << "Telemetry socket closed, stopping telemetry" << endl;
            break;
        }
        // Send encrypted telemetry
        if (send(clientSocket,
                (char*)encryptedData.data(),
                encryptedData.size(),
                0) == SOCKET_ERROR)
        {
            cerr << "Send failed: " << WSAGetLastError() << endl;
            break;
        }

        cout << "Sent Telemetry Data (AES): " << telemetryData << endl;
        this_thread::sleep_for(chrono::seconds(5));

    }
    
    closesocket(clientSocket);
    WSACleanup();
}

int main(int argc, char *argv[])
{   
    signal(SIGINT, handleSignal);

    if (argc < 3)
    {
        cerr << "Usage: " << argv[0] << " <drone_name> <drone_port>" << endl;
        cerr << "Example: " << argv[0] << " Drone1 9001" << endl;
        return 1;
    }

    drone_name = argv[1];
    drone_port = atoi(argv[2]);

    cout << "Starting Drone: " << drone_name << " on port: " << drone_port << endl;

    drone = Drone(drone_name, drone_port);

    // Start threads
    thread updatePositionThread(&Drone::UpdatePosition, &drone);
    thread controlCommandThread(receiveControlCommands);
    thread telemetryThread(sendTelemetryData);

    // Wait for all threads to finish
    controlCommandThread.join();
    telemetryThread.join();
    updatePositionThread.join();
    cout << "drone shut down gracefully." << endl;

    return 0;
}

