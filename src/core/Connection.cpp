#include "Connection.h"
#include "ProxyServer.h"
#include "../protocol/HttpProxy.h"
#include "../protocol/SocksProxy.h"
#include "Config.h"
#include <iostream>
#include <ws2tcpip.h>
#include <sstream>
#include <iomanip>

// Helper for Basic Auth
static std::string Base64Encode(const std::string& in) {
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

Connection::Connection(SOCKET clientSocket, ProxyServer* server) 
    : m_clientSocket(clientSocket), m_server(server), m_targetSocket(INVALID_SOCKET), 
      m_protocolState(ProtocolState::Initial), m_clientProtocol(ClientProtocol::Http), m_isConnectMethod(false),
      m_usingUpstream(false), m_upstreamStep(0) {
}

Connection::~Connection() {
    Close();
}

void Connection::Start() {
    PostReceive(m_clientSocket, IO_READ_CLIENT);
}

void Connection::Close() {
    if (m_clientSocket != INVALID_SOCKET) {
        shutdown(m_clientSocket, SD_BOTH);
        closesocket(m_clientSocket);
        m_clientSocket = INVALID_SOCKET;
    }
    if (m_targetSocket != INVALID_SOCKET) {
        shutdown(m_targetSocket, SD_BOTH);
        closesocket(m_targetSocket);
        m_targetSocket = INVALID_SOCKET;
    }
}

void Connection::PostReceive(SOCKET sock, IO_OPERATION op) {
    if (sock == INVALID_SOCKET) return;

    PerIoData* ioData = new PerIoData();
    ZeroMemory(&(ioData->overlapped), sizeof(OVERLAPPED));
    ioData->wsaBuf.len = 8192;
    ioData->wsaBuf.buf = ioData->buffer;
    ioData->operationType = op;
    ioData->connectionRef = shared_from_this();

    DWORD flags = 0;
    if (WSARecv(sock, &(ioData->wsaBuf), 1, NULL, &flags, &(ioData->overlapped), NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != ERROR_IO_PENDING) {
            delete ioData;
            Close();
        }
    }
}

void Connection::PostSend(SOCKET sock, const char* buffer, int len, IO_OPERATION op) {
    if (sock == INVALID_SOCKET) return;

    PerIoData* ioData = new PerIoData();
    ZeroMemory(&(ioData->overlapped), sizeof(OVERLAPPED));
    if (len > 8192) len = 8192; 
    memcpy(ioData->buffer, buffer, len);
    ioData->wsaBuf.len = len;
    ioData->wsaBuf.buf = ioData->buffer;
    ioData->operationType = op;
    ioData->connectionRef = shared_from_this();

    if (WSASend(sock, &(ioData->wsaBuf), 1, NULL, 0, &(ioData->overlapped), NULL) == SOCKET_ERROR) {
        if (WSAGetLastError() != ERROR_IO_PENDING) {
            delete ioData;
            Close();
        }
    }
}

void Connection::OnIoCompleted(PerIoData* ioData, DWORD bytesTransferred) {
    auto self = ioData->connectionRef;

    if (bytesTransferred == 0) {
        Close();
        m_server->RemoveConnection(m_clientSocket);
        delete ioData;
        return;
    }

    switch (ioData->operationType) {
        case IO_READ_CLIENT:
            // Read from Client -> Will write to Target (Upload)
            Config::Instance().AddTraffic(bytesTransferred, 0);

            if (m_protocolState != ProtocolState::Relay) {
                OnClientRead(ioData, bytesTransferred);
            } else {
                PostSend(m_targetSocket, ioData->buffer, bytesTransferred, IO_WRITE_TARGET);
                PostReceive(m_clientSocket, IO_READ_CLIENT);
            }
            break;
            
        case IO_READ_TARGET:
            // Read from Target -> Will write to Client (Download)
            Config::Instance().AddTraffic(0, bytesTransferred);

            if (m_protocolState == ProtocolState::UpstreamHandshake) {
                 m_upstreamBuffer.insert(m_upstreamBuffer.end(), ioData->buffer, ioData->buffer + bytesTransferred);
                 HandleUpstreamHandshake(ioData, 0);
            } else {
                PostSend(m_clientSocket, ioData->buffer, bytesTransferred, IO_WRITE_CLIENT);
                PostReceive(m_targetSocket, IO_READ_TARGET);
            }
            break;
            
        case IO_WRITE_CLIENT:
        case IO_WRITE_TARGET:
            break;
    }

    delete ioData;
}

void Connection::OnClientRead(PerIoData* ioData, DWORD bytesTransferred) {
    m_requestBuffer.insert(m_requestBuffer.end(), ioData->buffer, ioData->buffer + bytesTransferred);

    switch (m_protocolState) {
        case ProtocolState::Initial:
            HandleInitialRequest(ioData, bytesTransferred);
            break;
        case ProtocolState::Http:
            HandleHttpRequest(ioData, 0); 
            break;
        case ProtocolState::SocksHandshake:
            HandleSocksHandshake(ioData, 0);
            break;
        case ProtocolState::SocksAuth:
            HandleSocksAuth(ioData, 0);
            break;
        case ProtocolState::SocksRequest:
            HandleSocksRequest(ioData, 0);
            break;
        case ProtocolState::Socks4Request:
            HandleSocks4Request(ioData, 0);
            break;
        default:
            Close();
            break;
    }
}

void Connection::HandleInitialRequest(PerIoData* ioData, DWORD bytesTransferred) {
    if (m_requestBuffer.empty()) {
        PostReceive(m_clientSocket, IO_READ_CLIENT);
        return;
    }

    unsigned char version = static_cast<unsigned char>(m_requestBuffer[0]);
    if (version == 0x05) {
        m_protocolState = ProtocolState::SocksHandshake;
        m_clientProtocol = ClientProtocol::Socks5;
        HandleSocksHandshake(ioData, 0);
    } else if (version == 0x04) {
        m_protocolState = ProtocolState::Socks4Request;
        m_clientProtocol = ClientProtocol::Socks4;
        HandleSocks4Request(ioData, 0);
    } else {
        m_protocolState = ProtocolState::Http;
        m_clientProtocol = ClientProtocol::Http;
        HandleHttpRequest(ioData, 0);
    }
}

void Connection::HandleHttpRequest(PerIoData* ioData, DWORD bytesTransferred) {
    std::string data(m_requestBuffer.begin(), m_requestBuffer.end());
    size_t headerEnd = data.find("\r\n\r\n");
    
    if (headerEnd != std::string::npos) {
        ProxyRequest req;
        if (HttpParser::ParseRequest(data, req)) {
            // Auth Check
            if (Config::Instance().IsAuthEnabled()) {
                bool isLocal = IsClientLocal();
                if (!isLocal && (req.authUser.empty() || !Config::Instance().ValidateUser(req.authUser, req.authPass))) {
                     std::string response = "HTTP/1.1 407 Proxy Authentication Required\r\nProxy-Authenticate: Basic realm=\"Proxy\"\r\n\r\n";
                     PostSend(m_clientSocket, response.c_str(), response.length(), IO_WRITE_CLIENT);
                     Close();
                     return;
                }
            }

            // Handle Update Config
            if (!req.isConnect && req.path == "/update-config") {
                 Config::Instance().Load("proxy.conf");
                 Config::Instance().LoadGFWList("gfwlist.txt");
                 std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 15\r\nConnection: close\r\n\r\nConfig Updated\n";
                 PostSend(m_clientSocket, response.c_str(), response.length(), IO_WRITE_CLIENT);
                 Close();
                 return;
            }

            m_isConnectMethod = req.isConnect;

            std::string upstreamId = Config::Instance().GetUpstreamForDomain(req.host);
            bool connected = false;
            if (!upstreamId.empty()) {
                connected = ConnectToUpstream(upstreamId, req.host, req.port);
            } else {
                ConnectToTarget(req.host, req.port);
                connected = (m_targetSocket != INVALID_SOCKET);
            }
            
            if (connected) {
                if (m_usingUpstream) {
                    // Wait for upstream handshake
                    if (!req.isConnect) {
                        m_pendingClientData = req.originalRequest;
                    }
                } else {
                    m_protocolState = ProtocolState::Relay;
                    if (req.isConnect) {
                        std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
                        PostSend(m_clientSocket, response.c_str(), response.length(), IO_WRITE_CLIENT);
                    } else {
                        PostSend(m_targetSocket, req.originalRequest.c_str(), req.originalRequest.length(), IO_WRITE_TARGET);
                    }
                    StartRelay();
                }
            } else {
                 Close();
            }
        } else {
            Close();
        }
        m_requestBuffer.clear();
    } else {
        PostReceive(m_clientSocket, IO_READ_CLIENT);
    }
}

void Connection::HandleSocksHandshake(PerIoData* ioData, DWORD bytesTransferred) {
    int selectedMethod = 0;
    bool authRequired = Config::Instance().IsAuthEnabled();
    
    if (authRequired && IsClientLocal()) {
        authRequired = false;
    }

    if (SocksParser::ParseHandshake(m_requestBuffer, selectedMethod, authRequired)) {
        if (selectedMethod == 0xFF) {
            char reply[] = { 0x05, (char)0xFF };
            PostSend(m_clientSocket, reply, 2, IO_WRITE_CLIENT);
            Close();
        } else if (selectedMethod == 0x02) {
            char reply[] = { 0x05, 0x02 };
            PostSend(m_clientSocket, reply, 2, IO_WRITE_CLIENT);
            m_protocolState = ProtocolState::SocksAuth;
            m_requestBuffer.clear();
            PostReceive(m_clientSocket, IO_READ_CLIENT);
        } else {
            char reply[] = { 0x05, 0x00 };
            PostSend(m_clientSocket, reply, 2, IO_WRITE_CLIENT);
            m_protocolState = ProtocolState::SocksRequest;
            m_requestBuffer.clear();
            PostReceive(m_clientSocket, IO_READ_CLIENT);
        }
    } else {
        if (m_requestBuffer.size() > 256) Close();
        else PostReceive(m_clientSocket, IO_READ_CLIENT);
    }
}

void Connection::HandleSocksAuth(PerIoData* ioData, DWORD bytesTransferred) {
    SocksAuth auth;
    size_t consumed = 0;
    if (SocksParser::ParseAuth(m_requestBuffer, auth, consumed)) {
        if (Config::Instance().ValidateUser(auth.username, auth.password)) {
            char reply[] = { 0x01, 0x00 }; // Success
            PostSend(m_clientSocket, reply, 2, IO_WRITE_CLIENT);
            m_protocolState = ProtocolState::SocksRequest;
            m_requestBuffer.clear();
            PostReceive(m_clientSocket, IO_READ_CLIENT);
        } else {
            char reply[] = { 0x01, 0x01 }; // Fail
            PostSend(m_clientSocket, reply, 2, IO_WRITE_CLIENT);
            Close();
        }
    } else {
        if (m_requestBuffer.size() > 512) Close();
        else PostReceive(m_clientSocket, IO_READ_CLIENT);
    }
}

void Connection::HandleSocksRequest(PerIoData* ioData, DWORD bytesTransferred) {
    SocksRequest req;
    size_t consumed = 0;
    if (SocksParser::ParseRequest(m_requestBuffer, req, consumed)) {
        if (req.command == 1) { // Connect
             std::string upstreamId = Config::Instance().GetUpstreamForDomain(req.address);
             bool connected = false;
             if (!upstreamId.empty()) {
                 connected = ConnectToUpstream(upstreamId, req.address, req.port);
             } else {
                 ConnectToTarget(req.address, req.port);
                 connected = (m_targetSocket != INVALID_SOCKET);
             }

             if (connected) {
                 if (!m_usingUpstream) {
                    char reply[] = { 0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
                    PostSend(m_clientSocket, reply, 10, IO_WRITE_CLIENT);
                    m_protocolState = ProtocolState::Relay;
                    m_requestBuffer.clear();
                    StartRelay();
                 } else {
                     // Upstream handshake in progress...
                 }
             } else {
                 char reply[] = { 0x05, 0x04, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
                 PostSend(m_clientSocket, reply, 10, IO_WRITE_CLIENT);
                 Close();
             }
        } else {
             char reply[] = { 0x05, 0x07, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
             PostSend(m_clientSocket, reply, 10, IO_WRITE_CLIENT);
             Close();
        }
    } else {
         if (m_requestBuffer.size() > 1024) Close();
         else PostReceive(m_clientSocket, IO_READ_CLIENT);
    }
}

void Connection::HandleSocks4Request(PerIoData* ioData, DWORD bytesTransferred) {
    SocksRequest req;
    size_t consumed = 0;
    if (SocksParser::ParseSocks4Request(m_requestBuffer, req, consumed)) {
        if (req.command == 1) { // Connect
             std::string upstreamId = Config::Instance().GetUpstreamForDomain(req.address);
             bool connected = false;
             if (!upstreamId.empty()) {
                 connected = ConnectToUpstream(upstreamId, req.address, req.port);
             } else {
                 ConnectToTarget(req.address, req.port);
                 connected = (m_targetSocket != INVALID_SOCKET);
             }

             if (connected) {
                 if (!m_usingUpstream) {
                    char reply[] = { 0x00, 0x5A, 0, 0, 0, 0, 0, 0 };
                    PostSend(m_clientSocket, reply, 8, IO_WRITE_CLIENT);
                    m_protocolState = ProtocolState::Relay;
                    m_requestBuffer.clear();
                    StartRelay();
                 } else {
                     // Upstream handshake in progress...
                 }
             } else {
                 char reply[] = { 0x00, 0x5B, 0, 0, 0, 0, 0, 0 };
                 PostSend(m_clientSocket, reply, 8, IO_WRITE_CLIENT);
                 Close();
             }
        } else {
             char reply[] = { 0x00, 0x5B, 0, 0, 0, 0, 0, 0 };
             PostSend(m_clientSocket, reply, 8, IO_WRITE_CLIENT);
             Close();
        }
    } else {
         if (m_requestBuffer.size() > 1024) Close();
         else PostReceive(m_clientSocket, IO_READ_CLIENT);
    }
}

void Connection::ConnectToTarget(const std::string& host, int port) {
    struct addrinfo hints = {0}, *result = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    std::string portStr = std::to_string(port);
    
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &result) != 0) return;
    
    m_targetSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_targetSocket == INVALID_SOCKET) {
        freeaddrinfo(result);
        return;
    }
    
    if (connect(m_targetSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        closesocket(m_targetSocket);
        m_targetSocket = INVALID_SOCKET;
        freeaddrinfo(result);
        return;
    }
    
    freeaddrinfo(result);
    CreateIoCompletionPort((HANDLE)m_targetSocket, m_server->GetIOCP(), (ULONG_PTR)this, 0);
}

bool Connection::ConnectToUpstream(const std::string& upstreamId, const std::string& targetHost, int targetPort) {
    if (!Config::Instance().GetUpstream(upstreamId, m_currentUpstream)) return false;
    
    std::cout << "Connecting to upstream " << upstreamId << " (" << m_currentUpstream.host << ") for target: " << targetHost << std::endl;

    // 1. Connect TCP to Upstream
    ConnectToTarget(m_currentUpstream.host, m_currentUpstream.port);
    if (m_targetSocket == INVALID_SOCKET) return false;
    
    m_usingUpstream = true;
    m_protocolState = ProtocolState::UpstreamHandshake;
    m_upstreamStep = 0;

    if (m_currentUpstream.type == ProxyType::Socks5) {
        // 2. Start SOCKS5 Handshake with Upstream
        std::vector<char> handshake;
        handshake.push_back(0x05);
        if (!m_currentUpstream.username.empty()) {
            handshake.push_back(0x02); // 2 methods
            handshake.push_back(0x00); // No Auth
            handshake.push_back(0x02); // User/Pass
        } else {
            handshake.push_back(0x01); // 1 method
            handshake.push_back(0x00); // No Auth
        }
        PostSend(m_targetSocket, handshake.data(), handshake.size(), IO_WRITE_TARGET);
        
        PostReceive(m_targetSocket, IO_READ_TARGET);
        
        m_requestBuffer.clear();
        // Construct SOCKS5 CONNECT request to Upstream
        // 05 01 00 <ATYP> <Addr> <Port>
        m_requestBuffer.push_back(0x05);
        m_requestBuffer.push_back(0x01); // CONNECT
        m_requestBuffer.push_back(0x00); // RSV

        // Check if targetHost is IPv4
        struct in_addr addr;
        if (inet_pton(AF_INET, targetHost.c_str(), &addr) == 1) {
            m_requestBuffer.push_back(0x01); // IPv4
            unsigned char* bytes = (unsigned char*)&addr;
            for(int i=0; i<4; ++i) m_requestBuffer.push_back(bytes[i]);
        } else {
            m_requestBuffer.push_back(0x03); // Domain
            m_requestBuffer.push_back((char)targetHost.length());
            for(char c : targetHost) m_requestBuffer.push_back(c);
        }

        m_requestBuffer.push_back((targetPort >> 8) & 0xFF);
        m_requestBuffer.push_back(targetPort & 0xFF);
    } else if (m_currentUpstream.type == ProxyType::Http) {
        // HTTP Proxy Connect
        std::stringstream ss;
        ss << "CONNECT " << targetHost << ":" << targetPort << " HTTP/1.1\r\n";
        ss << "Host: " << targetHost << ":" << targetPort << "\r\n";
        if (!m_currentUpstream.username.empty()) {
            std::string auth = m_currentUpstream.username + ":" + m_currentUpstream.password;
            ss << "Proxy-Authorization: Basic " << Base64Encode(auth) << "\r\n";
        }
        ss << "\r\n";
        std::string req = ss.str();
        PostSend(m_targetSocket, req.c_str(), req.length(), IO_WRITE_TARGET);
        
        // We don't use m_requestBuffer for sending in HTTP case (sent directly), 
        // but we can use m_upstreamBuffer to receive response.
        m_upstreamBuffer.clear();
        PostReceive(m_targetSocket, IO_READ_TARGET);
    }

    return true;
}

void Connection::HandleUpstreamHandshake(PerIoData* ioData, DWORD bytesTransferred) {
    if (m_currentUpstream.type == ProxyType::Socks5) {
        if (m_upstreamStep == 0) {
            // Expect 05 XX
            if (m_upstreamBuffer.size() >= 2) {
                if (m_upstreamBuffer[0] == 0x05) {
                    if (m_upstreamBuffer[1] == 0x00) {
                        // No Auth required, proceed to Send Request
                        PostSend(m_targetSocket, m_requestBuffer.data(), m_requestBuffer.size(), IO_WRITE_TARGET);
                        m_upstreamStep = 2; 
                        m_upstreamBuffer.clear();
                        PostReceive(m_targetSocket, IO_READ_TARGET);
                    } else if (m_upstreamBuffer[1] == 0x02) {
                        // Auth Required
                        std::vector<char> auth;
                        auth.push_back(0x01); // Ver
                        auth.push_back((char)m_currentUpstream.username.length());
                        for(char c : m_currentUpstream.username) auth.push_back(c);
                        auth.push_back((char)m_currentUpstream.password.length());
                        for(char c : m_currentUpstream.password) auth.push_back(c);
                        
                        PostSend(m_targetSocket, auth.data(), auth.size(), IO_WRITE_TARGET);
                        m_upstreamStep = 1; // Wait for Auth Response
                        m_upstreamBuffer.clear();
                        PostReceive(m_targetSocket, IO_READ_TARGET);
                    } else {
                        Close();
                    }
                } else {
                    Close();
                }
            } else {
                 PostReceive(m_targetSocket, IO_READ_TARGET);
            }
        } else if (m_upstreamStep == 1) {
            // Expect 01 00
             if (m_upstreamBuffer.size() >= 2) {
                if (m_upstreamBuffer[0] == 0x01 && m_upstreamBuffer[1] == 0x00) {
                    // Auth Success, Send Request
                    PostSend(m_targetSocket, m_requestBuffer.data(), m_requestBuffer.size(), IO_WRITE_TARGET);
                    m_upstreamStep = 2; 
                    m_upstreamBuffer.clear();
                    PostReceive(m_targetSocket, IO_READ_TARGET);
                } else {
                    // Auth Failed
                    Close();
                }
             } else {
                 PostReceive(m_targetSocket, IO_READ_TARGET);
             }
        } else if (m_upstreamStep == 2) {
            // Expect 05 00 00 01 ...
            if (m_upstreamBuffer.size() >= 4) {
                if (m_upstreamBuffer[1] == 0x00) {
                    // Connected!
                    m_protocolState = ProtocolState::Relay;
                    
                    // Reply to Client
                    if (m_clientProtocol == ClientProtocol::Socks4) {
                        char reply[] = { 0x00, 0x5A, 0, 0, 0, 0, 0, 0 };
                        PostSend(m_clientSocket, reply, 8, IO_WRITE_CLIENT);
                    } else if (m_clientProtocol == ClientProtocol::Socks5) {
                        char reply[] = { 0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
                        PostSend(m_clientSocket, reply, 10, IO_WRITE_CLIENT);
                    } else {
                        // HTTP Client
                        if (m_isConnectMethod) {
                            std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
                            PostSend(m_clientSocket, response.c_str(), response.length(), IO_WRITE_CLIENT);
                        }
                        // For non-CONNECT, we forward original request below
                    }
                    
                    if (!m_pendingClientData.empty()) {
                        PostSend(m_targetSocket, m_pendingClientData.c_str(), m_pendingClientData.length(), IO_WRITE_TARGET);
                        m_pendingClientData.clear();
                    }
                    
                    StartRelay();
                } else {
                    Close();
                }
            } else {
                 PostReceive(m_targetSocket, IO_READ_TARGET);
            }
        }
    } else if (m_currentUpstream.type == ProxyType::Http) {
        std::string resp(m_upstreamBuffer.begin(), m_upstreamBuffer.end());
        if (resp.find("\r\n\r\n") != std::string::npos) {
            if (resp.find("200") != std::string::npos) {
                 // Connected!
                m_protocolState = ProtocolState::Relay;
                
                // Reply to Client
                if (m_clientProtocol == ClientProtocol::Socks4) {
                    char reply[] = { 0x00, 0x5A, 0, 0, 0, 0, 0, 0 };
                    PostSend(m_clientSocket, reply, 8, IO_WRITE_CLIENT);
                } else if (m_clientProtocol == ClientProtocol::Socks5) {
                    char reply[] = { 0x05, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
                    PostSend(m_clientSocket, reply, 10, IO_WRITE_CLIENT);
                } else {
                     // HTTP Client
                    if (m_isConnectMethod) {
                        std::string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
                        PostSend(m_clientSocket, response.c_str(), response.length(), IO_WRITE_CLIENT);
                    }
                }
                
                if (!m_pendingClientData.empty()) {
                    PostSend(m_targetSocket, m_pendingClientData.c_str(), m_pendingClientData.length(), IO_WRITE_TARGET);
                    m_pendingClientData.clear();
                }

                StartRelay();
            } else {
                Close();
            }
        } else {
            if (m_upstreamBuffer.size() > 4096) Close();
            else PostReceive(m_targetSocket, IO_READ_TARGET);
        }
    }
}

void Connection::StartRelay() {
    PostReceive(m_clientSocket, IO_READ_CLIENT);
    PostReceive(m_targetSocket, IO_READ_TARGET);
}

bool Connection::IsClientLocal() {
    struct sockaddr_in addr;
    int addrLen = sizeof(addr);
    if (getpeername(m_clientSocket, (struct sockaddr*)&addr, &addrLen) == 0) {
        // Check for 127.0.0.1
        if (addr.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
            return true;
        }
    }
    return false;
}
