#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <unordered_set>
#include <chrono>

enum class ProxyType {
    Direct,
    Http,
    Socks5
};

struct UpstreamProxy {
    std::string id;
    ProxyType type;
    std::string host;
    int port;
    std::string username;
    std::string password;
};

struct ProxyRule {
    std::string pattern; // e.g. "*.google.com", "baidu.com", "*.*.example.com"
    std::string upstreamId;
};

struct User {
    std::string username;
    std::string password;
};

class Config {
public:
    // Traffic Stats
    struct TrafficStats {
        unsigned long long totalUp = 0;
        unsigned long long totalDown = 0;
        unsigned long long todayUp = 0;
        unsigned long long todayDown = 0;
        std::string lastResetDate; // YYYY-MM-DD
    };

    static Config& Instance() {
        static Config instance;
        return instance;
    }

    void Load(const std::string& filename);
    void LoadGFWList(const std::string& filename);

    void AddUser(const std::string& user, const std::string& pass) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_users[user] = pass;
    }

    bool ValidateUser(const std::string& user, const std::string& pass) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_users.find(user);
        if (it != m_users.end()) {
            return it->second == pass;
        }
        return false;
    }

    void AddUpstream(const UpstreamProxy& proxy) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_upstreams[proxy.id] = proxy;
    }

    void AddRule(const std::string& pattern, const std::string& upstreamId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_rules.push_back({pattern, upstreamId});
    }

    void SetGFWListUpstream(const std::string& upstreamId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_gfwlistUpstreamId = upstreamId;
    }

    void SetServerAddress(const std::string& ip, int port) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_bindIP = ip;
        m_bindPort = port;
    }

    void AddAllowedIP(const std::string& ipRange);

    std::string GetBindIP() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_bindIP;
    }

    int GetBindPort() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_bindPort;
    }

    bool IsIPAllowed(const std::string& ip);

    // Returns empty string if no rule matches (Direct)
    std::string GetUpstreamForDomain(const std::string& domain);

    bool GetUpstream(const std::string& id, UpstreamProxy& outProxy) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_upstreams.find(id);
        if (it != m_upstreams.end()) {
            outProxy = it->second;
            return true;
        }
        return false;
    }

    bool IsAuthEnabled() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_users.empty();
    }

    void AddTraffic(unsigned long long up, unsigned long long down);
    TrafficStats GetTrafficStats();

    // Returns speed in bytes/sec. Calculates based on diff since last call.
    // Note: This should be called periodically (e.g. every 1s) to be meaningful.
    void GetSpeed(unsigned long long& upSpeed, unsigned long long& downSpeed);

private:
    Config() {}
    bool IsGFWListMatch(const std::string& domain);
    bool WildcardMatch(const std::string& pattern, const std::string& text);
    void LoadRuleFile(const std::string& filename, const std::string& upstreamId);
    
    // IP Range structure
    struct IPRange {
        uint32_t start;
        uint32_t end;
    };
    
    // Traffic Stats
    // struct TrafficStats {
    //    unsigned long long totalUp = 0;
    //    unsigned long long totalDown = 0;
    //    unsigned long long todayUp = 0;
    //    unsigned long long todayDown = 0;
    //    std::string lastResetDate; // YYYY-MM-DD
    // };

    void ParseIPRange(const std::string& rangeStr);
    static uint32_t IPToUInt(const std::string& ip);
    
    std::mutex m_mutex;
    std::map<std::string, std::string> m_users;
    std::map<std::string, UpstreamProxy> m_upstreams;
    std::vector<ProxyRule> m_rules;
    
    // Server Config
    std::string m_bindIP = "0.0.0.0";
    int m_bindPort = 8080;
    std::vector<IPRange> m_allowedIPs; // Changed from unordered_set to vector of ranges
    
    TrafficStats m_stats;
    
    // Speed calculation
    unsigned long long m_lastTotalUp = 0;
    unsigned long long m_lastTotalDown = 0;
    std::chrono::steady_clock::time_point m_lastSpeedCheckTime;

    // GFWList support
    std::string m_gfwlistUpstreamId;
    std::unordered_set<std::string> m_gfwlistDomains; // Stores "google.com", "facebook.com"
    std::vector<std::string> m_gfwlistKeywords; // Stores "google", "facebook"
};
