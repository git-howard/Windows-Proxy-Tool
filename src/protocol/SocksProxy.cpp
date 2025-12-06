#include "SocksProxy.h"

bool SocksParser::ParseHandshake(const std::vector<char>& data, int& selectedMethod, bool authRequired) {
    if (data.size() < 2) return false;
    if (data[0] != 0x05) return false; 

    int nMethods = data[1];
    if (data.size() < 2 + nMethods) return false; 

    bool supportsNoAuth = false;
    bool supportsUserPass = false;

    for (int i = 0; i < nMethods; ++i) {
        if (data[2 + i] == 0x00) supportsNoAuth = true;
        if (data[2 + i] == 0x02) supportsUserPass = true;
    }

    if (authRequired) {
        if (supportsUserPass) {
            selectedMethod = 0x02;
        } else {
            selectedMethod = 0xFF;
        }
    } else {
        if (supportsNoAuth) {
            selectedMethod = 0x00;
        } else if (supportsUserPass) {
            selectedMethod = 0x02; // Prefer no auth if not required, but allow auth? 
            // Actually if auth not required, we should prefer 0x00.
        } else {
            selectedMethod = 0xFF; 
        }
    }
    return true;
}

bool SocksParser::ParseAuth(const std::vector<char>& data, SocksAuth& auth, size_t& consumedBytes) {
    // Ver(1) | Ulen(1) | UName(N) | Plen(1) | Pass(N)
    if (data.size() < 2) return false;
    if (data[0] != 0x01) return false; // Version 1 subnegotiation

    size_t pos = 1;
    int ulen = (unsigned char)data[pos++];
    if (data.size() < pos + ulen + 1) return false;
    
    auth.username = std::string(data.begin() + pos, data.begin() + pos + ulen);
    pos += ulen;

    int plen = (unsigned char)data[pos++];
    if (data.size() < pos + plen) return false;

    auth.password = std::string(data.begin() + pos, data.begin() + pos + plen);
    pos += plen;

    consumedBytes = pos;
    return true;
}

bool SocksParser::ParseRequest(const std::vector<char>& data, SocksRequest& request, size_t& consumedBytes) {
    if (data.size() < 4) return false;
    if (data[0] != 0x05) return false;
    
    request.command = data[1]; 
    request.addressType = data[3];

    size_t currentPos = 4;
    if (request.addressType == 1) { // IPv4
        if (data.size() < currentPos + 4 + 2) return false;
        unsigned char ip[4];
        for(int i=0; i<4; ++i) ip[i] = data[currentPos + i];
        request.address = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]);
        currentPos += 4;
    } else if (request.addressType == 3) { // Domain
        if (data.size() < currentPos + 1) return false;
        int len = (unsigned char)data[currentPos];
        currentPos++;
        if (data.size() < currentPos + len + 2) return false;
        request.address = std::string(data.begin() + currentPos, data.begin() + currentPos + len);
        currentPos += len;
    } else if (request.addressType == 4) { // IPv6
        if (data.size() < currentPos + 16 + 2) return false;
        // Not supported for this MVP
        request.address = ""; 
        currentPos += 16;
    } else {
        return false;
    }

    // Port
    unsigned char h = data[currentPos];
    unsigned char l = data[currentPos + 1];
    request.port = (h << 8) | l;
    currentPos += 2;

    consumedBytes = currentPos;
    return true;
}

bool SocksParser::ParseSocks4Request(const std::vector<char>& data, SocksRequest& request, size_t& consumedBytes) {
    if (data.size() < 8) return false;
    if (data[0] != 0x04) return false;

    request.command = data[1]; // 1=CONNECT, 2=BIND
    
    // Port (Big Endian)
    unsigned char h = data[2];
    unsigned char l = data[3];
    request.port = (h << 8) | l;

    // IP
    unsigned char ip[4];
    for(int i=0; i<4; ++i) ip[i] = data[4+i];

    bool isSocks4a = (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] != 0);

    size_t pos = 8;
    // UserID (null terminated)
    while (pos < data.size() && data[pos] != 0) {
        pos++;
    }
    if (pos >= data.size()) return false; // Incomplete
    pos++; // Skip NULL

    if (isSocks4a) {
        // Domain (null terminated)
        size_t domainStart = pos;
        while (pos < data.size() && data[pos] != 0) {
            pos++;
        }
        if (pos >= data.size()) return false; // Incomplete
        request.address = std::string(data.begin() + domainStart, data.begin() + pos);
        request.addressType = 3; // Domain
        pos++; // Skip NULL
    } else {
        request.address = std::to_string(ip[0]) + "." + std::to_string(ip[1]) + "." + std::to_string(ip[2]) + "." + std::to_string(ip[3]);
        request.addressType = 1; // IPv4
    }

    consumedBytes = pos;
    return true;
}
