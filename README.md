# Windows Proxy Server & Manager

A lightweight, high-performance HTTP/SOCKS5 proxy server for Windows, written in C++ (server) and C# (GUI manager).

## Features

- **Dual Protocol Support**: Handles both HTTP/HTTPS and SOCKS5/SOCKS4 connections.
- **High Performance**: Built with Windows IOCP (I/O Completion Ports) for efficient async I/O handling.
- **Upstream Proxy**: Supports chaining to upstream proxies (HTTP/SOCKS5) with authentication.
- **Rule-Based Routing**:
  - **Direct**: Direct connection for specific domains.
  - **Proxy**: Route specific domains through upstream.
  - **GFWList**: Automatic integration with GFWList for smart routing.
- **Authentication**: Basic authentication support for client connections.
- **Local Bypass**: Automatically bypasses authentication for local connections (127.0.0.1) and system proxy traffic.
- **Traffic Monitoring**: Real-time speed display, traffic statistics, and visual charts.
- **System Integration**:
  - One-click "Set System Proxy" to route all Windows traffic.
  - Minimize to System Tray.
  - Auto-start server on launch.

## Components

1.  **ProxyServer.exe**: The core proxy engine (C++).
2.  **ProxyManager.exe**: The graphical management interface (C# / WPF).

## Installation & Usage

### 1. Prerequisites
- Windows 10/11 (64-bit recommended)
- .NET Framework 4.5 or later (usually pre-installed)
- [Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) (if not statically linked)

### 2. Running
1.  Navigate to the release folder (e.g., `build_static/Release`).
2.  Ensure `ProxyServer.exe` and `ProxyManager.exe` are in the same directory.
3.  Run **`ProxyManager.exe`**.
    - The server will start automatically.
    - You can see the logs and traffic stats in the main window.

### 3. Configuration (GUI)
The configuration is stored in `proxy.conf` and can be edited directly in the GUI.

**Basic Config:**
```conf
# Bind Address (Default: 0.0.0.0 8080)
SERVER 0.0.0.0 8080

# Client Whitelist (Optional)
# ALLOW_IP 127.0.0.1,192.168.1.0/24

# Authentication (Optional)
# USER admin 123456
```

**Upstream Proxy (Optional):**
Add an upstream server (e.g., a local Shadowsocks/V2Ray node):
```conf
# UPSTREAM <id> <type> <host> <port> [user] [pass]
# Type: SOCKS5 or HTTP
UPSTREAM local_ss SOCKS5 127.0.0.1 1080
```

**Routing Rules:**
```conf
# Route google.com through 'local_ss' upstream
RULE google.com local_ss

# Use GFWList for automatic routing
GFWLIST_UPSTREAM local_ss
```

### 4. System Proxy
- Check **"Set System Proxy"** in the GUI to automatically configure Windows to use this proxy for all supported applications.
- Uncheck or exit the application to restore system settings.

### 5. Minimize to Tray
- Clicking the "Close" (X) button or Minimize button will hide the application to the system tray.
- Double-click the tray icon to restore.
- Right-click the tray icon and select **Exit** to fully close the application and stop the server.

## Building from Source

### Requirements
- Visual Studio 2022 (C++ Desktop Development & .NET Desktop Development)
- CMake 3.10+

### Steps
1.  **Generate Project**:
    ```powershell
    mkdir build
    cd build
    cmake ..
    ```
2.  **Build**:
    ```powershell
    cmake --build . --config Release
    ```
3.  **Build GUI** (if not built by CMake):
    ```powershell
    csc /target:winexe /out:Release\ProxyManager.exe /reference:PresentationCore.dll ... src\gui\SimpleGui.cs
    ```

## License
MIT License
