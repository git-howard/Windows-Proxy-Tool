#pragma once
#include <string>
#include <vector>
#include <thread>
#include <map>
#include <mutex>
#include <memory>
#include <winsock2.h>
#include <windows.h>

class Connection;

class ProxyServer {
public:
    ProxyServer(const std::string& ip, int port);
    ~ProxyServer();
    void Start();
    void Stop();
    
    HANDLE GetIOCP() const { return m_iocp; }
    void RemoveConnection(SOCKET clientSocket);

private:
    void AcceptThread();
    void WorkerThread();
    void StatsThread();

    std::string m_ip;
    int m_port;
    SOCKET m_listenSocket;
    HANDLE m_iocp;
    std::vector<std::thread> m_threads;
    bool m_running;
    
    std::mutex m_connMutex;
    std::map<SOCKET, std::shared_ptr<Connection>> m_connections;
};
