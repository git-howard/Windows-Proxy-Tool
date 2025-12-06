#include "HttpProxy.h"
#include <sstream>
#include <vector>
#include <algorithm>
#include <iostream>

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

bool HttpParser::ParseRequest(const std::string& data, ProxyRequest& request) {
    std::istringstream stream(data);
    std::string line;
    if (!std::getline(stream, line)) return false;

    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::istringstream lineStream(line);
    std::string method, url, version;
    lineStream >> method >> url >> version;

    request.method = method;
    request.originalRequest = data; 
    
    std::string hostHeader;

    // Parse Headers for Auth
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string value = line.substr(colon + 1);
            // trim value
            size_t first = value.find_first_not_of(' ');
            if (first != std::string::npos) value = value.substr(first);

            // Case insensitive compare key
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            if (key == "proxy-authorization") {
                ParseBasicAuth(value, request.authUser, request.authPass);
            } else if (key == "host") {
                hostHeader = value;
            }
        }
    }

    if (method == "CONNECT") {
        request.isConnect = true;
        request.path = ""; // CONNECT has no path usually
        size_t colonPos = url.find(':');
        if (colonPos != std::string::npos) {
            request.host = url.substr(0, colonPos);
            try {
                request.port = std::stoi(url.substr(colonPos + 1));
            } catch (...) {
                return false;
            }
        } else {
            request.host = url;
            request.port = 443; 
        }
    } else {
        request.isConnect = false;
        size_t protocolPos = url.find("://");
        std::string tempUrl = url;
        if (protocolPos != std::string::npos) {
            tempUrl = url.substr(protocolPos + 3);
        }

        size_t slashPos = tempUrl.find('/');
        std::string hostPort = (slashPos != std::string::npos) ? tempUrl.substr(0, slashPos) : tempUrl;
        request.path = (slashPos != std::string::npos) ? tempUrl.substr(slashPos) : "/";

        size_t colonPos = hostPort.find(':');
        if (colonPos != std::string::npos) {
            request.host = hostPort.substr(0, colonPos);
            try {
                request.port = std::stoi(hostPort.substr(colonPos + 1));
            } catch (...) {
                request.port = 80;
            }
        } else {
            request.host = hostPort;
            request.port = 80;
        }

        if (request.host.empty() && !hostHeader.empty()) {
             size_t c = hostHeader.find(':');
             if (c != std::string::npos) {
                 request.host = hostHeader.substr(0, c);
                 try { request.port = std::stoi(hostHeader.substr(c + 1)); } catch(...) { request.port = 80; }
             } else {
                 request.host = hostHeader;
                 request.port = 80;
             }
        }
    }

    return !request.host.empty();
}

bool HttpParser::ParseBasicAuth(const std::string& header, std::string& user, std::string& pass) {
    // Header value: Basic <base64>
    if (header.substr(0, 6) != "Basic ") return false;
    std::string b64 = header.substr(6);
    std::string decoded = Base64Decode(b64);
    
    size_t colon = decoded.find(':');
    if (colon != std::string::npos) {
        user = decoded.substr(0, colon);
        pass = decoded.substr(colon + 1);
        return true;
    }
    return false;
}

std::string HttpParser::Base64Decode(const std::string& in) {
    std::string out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;

    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}
