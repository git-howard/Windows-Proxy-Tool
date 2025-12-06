#include "Config.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <ws2tcpip.h>
#include <ctime>
#include <iomanip>

// Helper to trim whitespace
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (std::string::npos == first) {
        return str;
    }
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void Config::Load(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_users.clear();
    m_upstreams.clear();
    m_rules.clear();
    m_gfwlistUpstreamId.clear();
    m_allowedIPs.clear();
    // Note: We don't clear gfwlist domains here, as they are loaded separately usually.
    // But if reload all, we might want to reload gfwlist too if path is provided.
    // For now, assume gfwlist is loaded via LoadGFWList.

    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::istringstream iss(line);
        std::string type;
        iss >> type;
        
        if (type == "USER") {
            std::string user, pass;
            iss >> user >> pass;
            if (!user.empty() && !pass.empty()) {
                m_users[user] = pass;
            }
        } else if (type == "UPSTREAM") {
            std::string id, ptypeStr, host;
            int port;
            iss >> id >> ptypeStr >> host >> port;
            
            UpstreamProxy proxy;
            proxy.id = id;
            proxy.host = host;
            proxy.port = port;
            std::transform(ptypeStr.begin(), ptypeStr.end(), ptypeStr.begin(), ::tolower);
            if (ptypeStr == "socks5") proxy.type = ProxyType::Socks5;
            else proxy.type = ProxyType::Http; 
            
            std::string user, pass;
            if (iss >> user >> pass) {
                proxy.username = user;
                proxy.password = pass;
            }
            m_upstreams[id] = proxy;
        } else if (type == "RULE") {
            std::string domain, upstreamId;
            iss >> domain >> upstreamId;
            if (!domain.empty() && !upstreamId.empty()) {
                m_rules.push_back({domain, upstreamId});
            }
        } else if (type == "RULE_FILE") {
            std::string filename, upstreamId;
            iss >> filename >> upstreamId;
            if (!filename.empty() && !upstreamId.empty()) {
                LoadRuleFile(filename, upstreamId);
            }
        } else if (type == "GFWLIST_UPSTREAM") {
            iss >> m_gfwlistUpstreamId;
        } else if (type == "SERVER") {
            std::string ip;
            int port;
            iss >> ip >> port;
            if (!ip.empty() && port > 0) {
                m_bindIP = ip;
                m_bindPort = port;
            }
        } else if (type == "ALLOW_IP" || type == "WHITELIST") {
            std::string ranges;
            std::getline(iss, ranges); // Read rest of line
            
            std::stringstream rss(ranges);
            std::string segment;
            while (std::getline(rss, segment, ',')) {
                segment = trim(segment);
                if (!segment.empty()) {
                    ParseIPRange(segment);
                }
            }
        }
    }
    std::cout << "Configuration loaded from " << filename << std::endl;

    // Load traffic stats
    std::ifstream statsFile("traffic.stats");
    if (statsFile.is_open()) {
        std::string line;
        while (std::getline(statsFile, line)) {
            std::istringstream iss(line);
            std::string key;
            if (std::getline(iss, key, '=')) {
                std::string value;
                if (std::getline(iss, value)) {
                    if (key == "TOTAL_UP") m_stats.totalUp = std::stoull(value);
                    else if (key == "TOTAL_DOWN") m_stats.totalDown = std::stoull(value);
                    else if (key == "TODAY_UP") m_stats.todayUp = std::stoull(value);
                    else if (key == "TODAY_DOWN") m_stats.todayDown = std::stoull(value);
                    else if (key == "DATE") m_stats.lastResetDate = value;
                }
            }
        }
    }
}

void Config::AddAllowedIP(const std::string& ipRange) {
    std::lock_guard<std::mutex> lock(m_mutex);
    ParseIPRange(ipRange);
}

bool Config::IsIPAllowed(const std::string& ip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_allowedIPs.empty()) return true;
    
    uint32_t ipVal = IPToUInt(ip);
    if (ipVal == 0) return false; // Invalid IP

    for (const auto& range : m_allowedIPs) {
        if (ipVal >= range.start && ipVal <= range.end) {
            return true;
        }
    }
    return false;
}

uint32_t Config::IPToUInt(const std::string& ip) {
    struct in_addr addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr) == 1) {
        return ntohl(addr.s_addr);
    }
    return 0;
}

void Config::ParseIPRange(const std::string& rangeStr) {
    // Check for CIDR format: 10.0.0.0/24
    size_t slashPos = rangeStr.find('/');
    if (slashPos != std::string::npos) {
        std::string ipStr = rangeStr.substr(0, slashPos);
        std::string maskStr = rangeStr.substr(slashPos + 1);
        
        uint32_t ip = IPToUInt(ipStr);
        int maskBits = std::stoi(maskStr);
        
        if (ip != 0 && maskBits >= 0 && maskBits <= 32) {
            uint32_t mask = (maskBits == 0) ? 0 : (~0U << (32 - maskBits));
            uint32_t start = ip & mask;
            uint32_t end = start | (~mask);
            m_allowedIPs.push_back({start, end});
        }
        return;
    }
    
    // Check for Range format: 10.0.0.1-10.0.0.10
    size_t dashPos = rangeStr.find('-');
    if (dashPos != std::string::npos) {
        std::string startIPStr = rangeStr.substr(0, dashPos);
        std::string endIPStr = rangeStr.substr(dashPos + 1);
        
        uint32_t start = IPToUInt(startIPStr);
        uint32_t end = IPToUInt(endIPStr);
        
        if (start != 0 && end != 0 && start <= end) {
            m_allowedIPs.push_back({start, end});
        }
        return;
    }
    
    // Single IP
    uint32_t ip = IPToUInt(rangeStr);
    if (ip != 0) {
        m_allowedIPs.push_back({ip, ip});
    }
}

void Config::AddTraffic(unsigned long long up, unsigned long long down) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Get current date
    time_t now = time(0);
    tm* ltm = localtime(&now);
    std::stringstream ss;
    ss << (1900 + ltm->tm_year) << "-" << std::setw(2) << std::setfill('0') << (1 + ltm->tm_mon) << "-" << std::setw(2) << ltm->tm_mday;
    std::string currentDate = ss.str();

    if (m_stats.lastResetDate != currentDate) {
        m_stats.todayUp = 0;
        m_stats.todayDown = 0;
        m_stats.lastResetDate = currentDate;
    }

    m_stats.totalUp += up;
    m_stats.totalDown += down;
    m_stats.todayUp += up;
    m_stats.todayDown += down;
    
    // Save periodically could be done here or in a separate thread, 
    // but for simplicity we save on every significant update or let the main loop handle saving.
    // Let's save if it's been a while or large chunk? 
    // For performance, avoid file I/O on every packet.
    // We'll rely on manual save or periodic save from main thread.
}

Config::TrafficStats Config::GetTrafficStats() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

void Config::GetSpeed(unsigned long long& upSpeed, unsigned long long& downSpeed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastSpeedCheckTime).count();
    
    if (duration > 0) {
        // Calculate speed in bytes per second
        upSpeed = (m_stats.totalUp - m_lastTotalUp) * 1000 / duration;
        downSpeed = (m_stats.totalDown - m_lastTotalDown) * 1000 / duration;
    } else {
        upSpeed = 0;
        downSpeed = 0;
    }

    m_lastTotalUp = m_stats.totalUp;
    m_lastTotalDown = m_stats.totalDown;
    m_lastSpeedCheckTime = now;
}

void Config::LoadGFWList(const std::string& filename) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_gfwlistDomains.clear();
    m_gfwlistKeywords.clear();

    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "GFWList file not found: " << filename << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        if (line[0] == '!' || line[0] == '[') continue; // Comments

        // Basic AutoProxy parsing
        std::string rule = line;
        
        // Whitelist @@ - ignore for now or implement whitelist logic (complex)
        if (rule.substr(0, 2) == "@@") continue;

        // Regex /.../ - ignore for now
        if (rule[0] == '/') continue;

        if (rule.substr(0, 2) == "||") {
            // Domain suffix match: ||google.com
            std::string domain = rule.substr(2);
            m_gfwlistDomains.insert(domain);
        } else if (rule[0] == '|') {
            // Exact start match |http://... - ignore for simple domain proxying
            // Or |https://...
        } else if (rule.find('.') == std::string::npos) {
            // Keyword? e.g. "google"
             m_gfwlistKeywords.push_back(rule);
        } else {
            // Assume domain suffix or exact match
            // .google.com -> google.com
            if (rule[0] == '.') rule = rule.substr(1);
            m_gfwlistDomains.insert(rule);
        }
    }
    std::cout << "GFWList loaded. Domains: " << m_gfwlistDomains.size() << std::endl;
}

void Config::LoadRuleFile(const std::string& filename, const std::string& upstreamId) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "Rule file not found: " << filename << std::endl;
        return;
    }
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        m_rules.push_back({line, upstreamId});
    }
    std::cout << "Loaded rules from " << filename << std::endl;
}

std::string Config::GetUpstreamForDomain(const std::string& domain) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // 1. Check User Rules
    for (const auto& rule : m_rules) {
        if (WildcardMatch(rule.pattern, domain)) {
            return rule.upstreamId;
        }
    }

    // 2. Check GFWList
    if (!m_gfwlistUpstreamId.empty() && IsGFWListMatch(domain)) {
        return m_gfwlistUpstreamId;
    }

    return "";
}

bool Config::WildcardMatch(const std::string& pattern, const std::string& text) {
    // Simple wildcard matching: * matches any sequence, ? matches single char
    // However, for domain matching, we usually care about *.example.com or just example.com
    
    // If no wildcard, assume suffix match (original behavior) for backward compatibility
    // But better to be explicit. If pattern has no * or ?, treat as suffix match or exact match?
    // The user asked for *.baidu.com support.
    
    // Let's implement a robust glob matcher
    size_t pIdx = 0, tIdx = 0;
    size_t pLen = pattern.length();
    size_t tLen = text.length();
    size_t starIdx = std::string::npos;
    size_t matchIdx = 0;

    // Special handling for legacy "google.com" style rules (implied suffix match)
    if (pattern.find('*') == std::string::npos && pattern.find('?') == std::string::npos) {
        if (text.length() >= pattern.length()) {
            if (text.compare(text.length() - pattern.length(), pattern.length(), pattern) == 0) {
                 if (text.length() == pattern.length() || text[text.length() - pattern.length() - 1] == '.') {
                    return true;
                }
            }
        }
        return false;
    }

    while (tIdx < tLen) {
        if (pIdx < pLen && (pattern[pIdx] == '?' || pattern[pIdx] == text[tIdx])) {
            pIdx++;
            tIdx++;
        } else if (pIdx < pLen && pattern[pIdx] == '*') {
            starIdx = pIdx;
            matchIdx = tIdx;
            pIdx++;
        } else if (starIdx != std::string::npos) {
            pIdx = starIdx + 1;
            matchIdx++;
            tIdx = matchIdx;
        } else {
            return false;
        }
    }

    while (pIdx < pLen && pattern[pIdx] == '*') {
        pIdx++;
    }

    return pIdx == pLen;
}

bool Config::IsGFWListMatch(const std::string& domain) {
    // Check exact domain
    if (m_gfwlistDomains.count(domain)) return true;

    // Check parent domains
    // e.g. www.google.com -> check google.com, com
    std::string temp = domain;
    size_t dotPos = temp.find('.');
    while (dotPos != std::string::npos) {
        temp = temp.substr(dotPos + 1);
        if (m_gfwlistDomains.count(temp)) return true;
        dotPos = temp.find('.');
    }

    // Check keywords (slow)
    // for (const auto& kw : m_gfwlistKeywords) {
    //    if (domain.find(kw) != std::string::npos) return true;
    // }

    return false;
}
