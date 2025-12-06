#include "ProxyServer.h"
#include "Connection.h"
#include "Config.h"
#include <iostream>
#include <ws2tcpip.h>
#include <fstream>

#pragma comment(lib, "ws2_32.lib")

ProxyServer::ProxyServer(const std::string& ip, int port) : m_ip(ip), m_port(port), m_running(false), m_listenSocket(INVALID_SOCKET), m_iocp(NULL) {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
}

ProxyServer::~ProxyServer() {
    Stop();
    WSACleanup();
}

void ProxyServer::Start() {
    m_running = true;
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (m_iocp == NULL) throw std::runtime_error("Failed to create IOCP");

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int threadCount = sysInfo.dwNumberOfProcessors * 2;

    for (int i = 0; i < threadCount; ++i) {
        m_threads.emplace_back(&ProxyServer::WorkerThread, this);
    }

    m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listenSocket == INVALID_SOCKET) throw std::runtime_error("Failed to create socket");

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    inet_pton(AF_INET, m_ip.c_str(), &serverAddr.sin_addr);
    serverAddr.sin_port = htons(m_port);

    if (bind(m_listenSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) throw std::runtime_error("Bind failed");
    if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) throw std::runtime_error("Listen failed");

    std::cout << "Server listening on " << m_ip << ":" << m_port << std::endl;

    std::thread acceptThread(&ProxyServer::AcceptThread, this);
    std::thread statsThread(&ProxyServer::StatsThread, this);
    
    acceptThread.join(); 
    statsThread.join();
}

void ProxyServer::Stop() {
    m_running = false;
    if (m_listenSocket != INVALID_SOCKET) {
        closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }
    if (m_iocp) {
        for (size_t i = 0; i < m_threads.size(); ++i) {
            PostQueuedCompletionStatus(m_iocp, 0, 0, NULL);
        }
        CloseHandle(m_iocp);
        m_iocp = NULL;
    }
    for (auto& t : m_threads) {
        if (t.joinable()) t.join();
    }
}

void ProxyServer::RemoveConnection(SOCKET clientSocket) {
    std::lock_guard<std::mutex> lock(m_connMutex);
    m_connections.erase(clientSocket);
}

void ProxyServer::AcceptThread() {
    while (m_running) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(m_listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            if (m_running) Sleep(100);
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        std::string ipStr(clientIP);

        std::cout << "New connection: " << clientSocket << " from " << ipStr << std::endl;

        // Check IP whitelist
        if (!Config::Instance().IsIPAllowed(ipStr)) {
            std::cerr << "Connection denied from " << ipStr << " (not in whitelist)" << std::endl;
            closesocket(clientSocket);
            continue;
        }

        auto connection = std::make_shared<Connection>(clientSocket, this);
        
        {
            std::lock_guard<std::mutex> lock(m_connMutex);
            m_connections[clientSocket] = connection;
        }

        if (CreateIoCompletionPort((HANDLE)clientSocket, m_iocp, (ULONG_PTR)connection.get(), 0) == NULL) {
            std::cerr << "CreateIoCompletionPort failed" << std::endl;
            RemoveConnection(clientSocket);
            continue;
        }

        connection->Start();
    }
}

void ProxyServer::WorkerThread() {
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped;

    while (m_running) {
        BOOL result = GetQueuedCompletionStatus(m_iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE);

        if (overlapped == NULL) {
             if (GetLastError() != WAIT_TIMEOUT) {
                 // Exit or error
             }
             continue;
        }

        PerIoData* ioData = (PerIoData*)CONTAINING_RECORD(overlapped, PerIoData, overlapped);
        
        // IMPORTANT: Use the shared_ptr from ioData to keep connection alive
        auto connection = ioData->connectionRef;

        if (!result || (bytesTransferred == 0 && (ioData->operationType == IO_READ_CLIENT || ioData->operationType == IO_READ_TARGET))) {
            if (connection) {
                connection->OnIoCompleted(ioData, 0);
            } else {
                delete ioData;
            }
            continue;
        }

        if (connection) {
            connection->OnIoCompleted(ioData, bytesTransferred);
        } else {
            // Should not happen if logic is correct
            delete ioData;
        }
    }
}

void ProxyServer::StatsThread() {
    while (m_running) {
        Sleep(1000); // Update every 1 second
        
        auto stats = Config::Instance().GetTrafficStats();
        unsigned long long upSpeed, downSpeed;
        Config::Instance().GetSpeed(upSpeed, downSpeed);
        
        // Output stats to stdout for GUI to parse
        // Format: STATS: TOTAL_UP=... TOTAL_DOWN=... TODAY_UP=... TODAY_DOWN=... SPEED_UP=... SPEED_DOWN=...
        std::cout << "STATS: TOTAL_UP=" << stats.totalUp 
                  << " TOTAL_DOWN=" << stats.totalDown 
                  << " TODAY_UP=" << stats.todayUp 
                  << " TODAY_DOWN=" << stats.todayDown 
                  << " SPEED_UP=" << upSpeed 
                  << " SPEED_DOWN=" << downSpeed << std::endl;

        // Periodic Save (every ~10 seconds, here just every 10th loop or simple write every time since it's small)
        // Let's write every time since it is once per second
        std::ofstream statsFile("traffic.stats");
        if (statsFile.is_open()) {
            statsFile << "DATE=" << stats.lastResetDate << "\n";
            statsFile << "TOTAL_UP=" << stats.totalUp << "\n";
            statsFile << "TOTAL_DOWN=" << stats.totalDown << "\n";
            statsFile << "TODAY_UP=" << stats.todayUp << "\n";
            statsFile << "TODAY_DOWN=" << stats.todayDown << "\n";
        }
    }
}
