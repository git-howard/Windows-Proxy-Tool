#pragma once
#include <vector>
#include <string>

struct SocksRequest {
    int command; // 1=Connect
    int addressType; // 1=IPv4, 3=Domain, 4=IPv6
    std::string address;
    int port;
};

struct SocksAuth {
    std::string username;
    std::string password;
};

class SocksParser {
public:
    // Returns true if valid handshake, selectedMethod is output
    // supportsAuth: if server supports auth
    static bool ParseHandshake(const std::vector<char>& data, int& selectedMethod, bool authRequired);
    
    static bool ParseAuth(const std::vector<char>& data, SocksAuth& auth, size_t& consumedBytes);

    // Returns true if valid request, consumedBytes tells how many bytes used
    static bool ParseRequest(const std::vector<char>& data, SocksRequest& request, size_t& consumedBytes);

    // Returns true if valid SOCKS4 request
    static bool ParseSocks4Request(const std::vector<char>& data, SocksRequest& request, size_t& consumedBytes);
};
