# Autonomous-DroneGuard
Autonomous DroneGuard is a secure multi-drone control and telemetry system
implemented in C++ using TCP and UDP sockets. The project demonstrates
real-world networking concepts, applied cryptography, concurrency,
and secure systems design.

The system consists of:
- A central control server
- Multiple autonomous drone clients

Each drone registers with the server, sends encrypted telemetry data,
receives encrypted control commands, and supports encrypted file transfer.

--------------------------------------------------

Key Features
------------
1. Multi-Drone Support
   - Multiple drones can connect simultaneously
   - Each drone runs as a separate process
   - Server tracks drones by name, IP, and control port

2. Secure Communication
   - AES-128-CBC encryption (OpenSSL EVP)
   - Random IV per message
   - No plaintext transmission

3. Replay Attack Protection (UDP)
   - Control commands include timestamps
   - Commands outside a time window are rejected

4. Thread-Safe Server Design
   - Shared drone registry protected using mutexes
   - Safe concurrent access from multiple threads

5. Telemetry System (TCP)
   - Drones periodically send encrypted telemetry
   - Server logs telemetry to a file

6. Secure File Transfer (TCP)
   - Server can request files from drones
   - File data encrypted chunk-by-chunk with AES

7. Graceful Shutdown
   - Signal handling for clean drone shutdown
   - Threads exit safely without corruption

--------------------------------------------------

Architecture
------------
Server:
- UDP: Control Commands
- TCP: Telemetry Reception
- TCP: File Transfer Reception
- Multi-threaded handling of drones

Drone Client:
- TCP: Telemetry Sender
- UDP: Control Command Listener
- TCP: File Transfer Sender
- Position updated continuously based on speed

--------------------------------------------------

Compilation
-----------
This project requires a C++17 compiler, Winsock, and OpenSSL.

Windows (MinGW / MSYS2):
-----------------------
Compile server:
g++ server.cpp -o server -lws2_32 -lssl -lcrypto -std=c++17

Compile drone client:
g++ main.cpp -o drone -lws2_32 -lssl -lcrypto -std=c++17



How to Run
----------
1. Start the server:
   ./server

2. Start drones (each in its own terminal or background process):

   ./drone alpha 5000 > alpha.log 2>&1
   ./drone beta  5001 > beta.log  2>&1
   ./drone gamma 5002 > gamma.log 2>&1

3. Use the server console to:
   - View connected drones
   - Update drone speed
   - Request encrypted file transfer

--------------------------------------------------

Logging
-------
- Drone telemetry and debug output are written to per-drone log files
- Server logs telemetry to telemetry.log

--------------------------------------------------

Security Notes
--------------
- AES keys are currently pre-shared (for demonstration)
- Replay protection implemented using timestamps
- Message authentication (HMAC) and session key exchange
  can be added as future improvements

--------------------------------------------------

Technologies Used
-----------------
- C++17
- Winsock (Windows networking)
- OpenSSL (AES encryption)
- Multithreading (std::thread, std::mutex)
- TCP / UDP socket programming

--------------------------------------------------

Future Enhancements
-------------------
- Session-based key exchange (ECDH)
- HMAC for message integrity
- Web or terminal-based dashboard
- Persistent drone authentication
- Structured logging (JSON)

--------------------------------------------------
