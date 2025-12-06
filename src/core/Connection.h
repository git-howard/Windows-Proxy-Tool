#pragma once
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <memory>
#include <string>
#include "../protocol/SocksProxy.h"
#include "Config.h"

enum IO_OPERATION {
    IO_READ_CLIENT,
    IO_WRITE_CLIENT,
    IO_READ_TARGET,
    IO_WRITE_TARGET
};

enum class ProtocolState {
    Initial,
    Http,
    SocksHandshake,
    SocksAuth, // New
    SocksRequest,
    Socks4Request, // New
    UpstreamHandshake, // New
    Relay
};

enum class ClientProtocol {
    Http,
    Socks5,
    Socks4
};

class Connection;

struct PerIoData {
    OVERLAPPED overlapped;
    WSABUF wsaBuf;
    char buffer[8192];
    IO_OPERATION operationType;
    std::shared_ptr<Connection> connectionRef;
};

class ProxyServer;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(SOCKET clientSocket, ProxyServer* server);
    ~Connection();

    void Start();
    void OnIoCompleted(PerIoData* ioData, DWORD bytesTransferred);

    SOCKET GetClientSocket() const { return m_clientSocket; }

private:
    void OnClientRead(PerIoData* ioData, DWORD bytesTransferred);
    void OnTargetRead(PerIoData* ioData, DWORD bytesTransferred);

    void HandleInitialRequest(PerIoData* ioData, DWORD bytesTransferred);
    void HandleHttpRequest(PerIoData* ioData, DWORD bytesTransferred);
    void HandleSocksHandshake(PerIoData* ioData, DWORD bytesTransferred);
    void HandleSocksAuth(PerIoData* ioData, DWORD bytesTransferred);
    void HandleSocksRequest(PerIoData* ioData, DWORD bytesTransferred);
    void HandleSocks4Request(PerIoData* ioData, DWORD bytesTransferred);
    void HandleUpstreamHandshake(PerIoData* ioData, DWORD bytesTransferred);

    bool ConnectToUpstream(const std::string& upstreamId, const std::string& targetHost, int targetPort);
    void ConnectToTarget(const std::string& host, int port);
    void StartRelay();
    void PostReceive(SOCKET sock, IO_OPERATION op);
    void PostSend(SOCKET sock, const char* buffer, int len, IO_OPERATION op);
    void Close();
    
    // Helper to check if client is local
    bool IsClientLocal();

    SOCKET m_clientSocket;
    SOCKET m_targetSocket;
    ProxyServer* m_server;
    
    std::vector<char> m_requestBuffer;
    std::vector<char> m_upstreamBuffer; // Buffer for upstream handshake

    ProtocolState m_protocolState;
    ClientProtocol m_clientProtocol;
    bool m_isConnectMethod;
    
    // Upstream context
    bool m_usingUpstream;
    UpstreamProxy m_currentUpstream;
    int m_upstreamStep; // 0=Handshake, 1=Auth, 2=Request
    
    std::string m_pendingClientData; // Data to send to target after upstream handshake
};
