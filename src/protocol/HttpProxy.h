#pragma once
#include <string>

struct ProxyRequest {
    std::string method;
    std::string host;
    int port;
    std::string path;
    bool isConnect; // true if CONNECT method
    std::string originalRequest; // To forward if not CONNECT
    
    std::string authUser;
    std::string authPass;
};

class HttpParser {
public:
    static bool ParseRequest(const std::string& data, ProxyRequest& request);
private:
    static bool ParseBasicAuth(const std::string& header, std::string& user, std::string& pass);
    static std::string Base64Decode(const std::string& in);
};
