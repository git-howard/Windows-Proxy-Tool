#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <iostream>
#include "core/ProxyServer.h"
#include "core/Config.h"

int main(int argc, char* argv[]) {
    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"WindowsProxyServerUniqueMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cerr << "Error: ProxyServer is already running." << std::endl;
        return 1;
    }

    std::cout << "Starting Windows Proxy Server..." << std::endl;
    
    // Load configuration if file exists
    Config::Instance().Load("proxy.conf");
    Config::Instance().LoadGFWList("gfwlist.txt");

    try {
        std::string bindIP = Config::Instance().GetBindIP();
        int bindPort = Config::Instance().GetBindPort();
        
        ProxyServer server(bindIP, bindPort);
        server.Start();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
