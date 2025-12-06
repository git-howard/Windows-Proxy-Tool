using System;
using System.Windows;
using System.Windows.Controls;
using System.IO;
using System.Diagnostics;
using System.Net;
using System.Windows.Shapes;
using System.Windows.Media;
using System.Collections.Generic;
using System.Linq;
using System.ComponentModel;
using System.Text;
using Microsoft.Win32;
using System.Runtime.InteropServices;
using System.Threading;

namespace ProxyGui {
    public class SimpleGui : Window {
        private TextBox configBox;
        private TextBox logBox;
        private Button startButton;
        private CheckBox systemProxyCheck;
        private CheckBox autoStartCheck;
        private Process proxyProcess;
        private System.Windows.Forms.NotifyIcon notifyIcon;
        
        // Stats Labels
        private TextBlock statsBlock;
        private TextBlock speedBlock;
        
        // Chart
        private Canvas chartCanvas;
        private Polyline upLine;
        private Polyline downLine;
        private List<Line> gridLines = new List<Line>();
        private List<TextBlock> gridLabels = new List<TextBlock>();
        private List<double> upHistory = new List<double>();
        private List<double> downHistory = new List<double>();
        private const int MaxChartPoints = 60;
        private const double ChartHeight = 100;

        private const string ConfigFile = "proxy.conf";
        private const string GfwListFile = "gfwlist.txt";
        private const string ProxyExe = "ProxyServer.exe"; 
        private const string IconFile = "proxy.ico";

        public SimpleGui() {
            Title = "Windows 代理管理器";
            Width = 600;
            Height = 520; // Increased height
            WindowStartupLocation = WindowStartupLocation.CenterScreen;
            
            if (File.Exists(IconFile)) {
                try {
                    Icon = System.Windows.Media.Imaging.BitmapFrame.Create(new Uri(System.IO.Path.GetFullPath(IconFile)));
                } catch {}
            }

            var grid = new Grid();
            grid.Margin = new Thickness(10);
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) }); 
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); 
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // Stats Row
            grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto }); // Chart Row
            grid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) }); 

            // Config Editor
            configBox = new TextBox {
                AcceptsReturn = true,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                FontFamily = new System.Windows.Media.FontFamily("Consolas")
            };
            LoadConfig();
            grid.Children.Add(configBox);
            Grid.SetRow(configBox, 0);

            // Buttons
            var btnPanel = new StackPanel { Orientation = Orientation.Horizontal, Margin = new Thickness(5) };
            
            var applyBtn = new Button { Content = "应用配置", Width = 80, Margin = new Thickness(5) };
            applyBtn.Click += (s, e) => UpdateServer();
            btnPanel.Children.Add(applyBtn);

            var updateGfwBtn = new Button { Content = "更新 GFWList", Width = 90, Margin = new Thickness(5) };
            updateGfwBtn.Click += (s, e) => UpdateGfwList();
            btnPanel.Children.Add(updateGfwBtn);

            // Removed standalone UpdateServer button as it is now combined with Apply
            // var updateServerBtn = new Button { Content = "重启服务", Width = 80, Margin = new Thickness(5) };
            // updateServerBtn.Click += (s, e) => UpdateServer();
            // btnPanel.Children.Add(updateServerBtn);

            startButton = new Button { Content = "启动服务", Width = 80, Margin = new Thickness(5) };
            startButton.Click += (s, e) => ToggleServer();
            btnPanel.Children.Add(startButton);

            var exitBtn = new Button { Content = "退出程序", Width = 80, Margin = new Thickness(5) };
            exitBtn.Click += (s, e) => {
                if (systemProxyCheck.IsChecked == true) SetSystemProxy(false);
                StopServer();
                if (notifyIcon != null) notifyIcon.Visible = false;
                System.Windows.Application.Current.Shutdown();
            };
            btnPanel.Children.Add(exitBtn);

            var helpBtn = new Button { Content = "帮助", Width = 60, Margin = new Thickness(5) };
            helpBtn.Click += (s, e) => ShowHelp();
            btnPanel.Children.Add(helpBtn);

            var checkPanel = new StackPanel { Orientation = Orientation.Vertical, VerticalAlignment = VerticalAlignment.Center };

            systemProxyCheck = new CheckBox { Content = "系统代理", Margin = new Thickness(5, 0, 0, 0) };
            systemProxyCheck.Checked += (s, e) => SetSystemProxy(true);
            systemProxyCheck.Unchecked += (s, e) => SetSystemProxy(false);
            checkPanel.Children.Add(systemProxyCheck);

            autoStartCheck = new CheckBox { Content = "开机自启", Margin = new Thickness(5, 2, 0, 0) };
            autoStartCheck.IsChecked = IsAutoStartEnabled();
            autoStartCheck.Click += (s, e) => SetAutoStart(autoStartCheck.IsChecked == true);
            checkPanel.Children.Add(autoStartCheck);

            btnPanel.Children.Add(checkPanel);

            grid.Children.Add(btnPanel);
            Grid.SetRow(btnPanel, 1);

            // Stats Panel
            var statsPanel = new StackPanel { Orientation = Orientation.Vertical, Margin = new Thickness(5) };
            
            statsBlock = new TextBlock {
                Text = "状态: 等待服务...",
                Margin = new Thickness(2),
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                FontWeight = FontWeights.Bold
            };
            statsPanel.Children.Add(statsBlock);

            speedBlock = new TextBlock {
                Text = "速率: -- KB/s 上传 | -- KB/s 下载",
                Margin = new Thickness(2),
                FontFamily = new System.Windows.Media.FontFamily("Consolas"),
                Foreground = System.Windows.Media.Brushes.Blue
            };
            statsPanel.Children.Add(speedBlock);

            grid.Children.Add(statsPanel);
            Grid.SetRow(statsPanel, 2);

            // Chart
            chartCanvas = new Canvas { 
                Height = ChartHeight, 
                Background = new SolidColorBrush(Color.FromRgb(240, 240, 240)),
                Margin = new Thickness(5),
                ClipToBounds = true
            };
            
            // Initialize Grid Lines (e.g. 3 lines: 25%, 50%, 75%)
            for (int i = 0; i < 3; i++) {
                var line = new Line { 
                    Stroke = Brushes.LightGray, 
                    StrokeThickness = 1, 
                    StrokeDashArray = new DoubleCollection { 2, 2 } 
                };
                chartCanvas.Children.Add(line);
                gridLines.Add(line);

                var label = new TextBlock { 
                    FontSize = 10, 
                    Foreground = Brushes.Gray,
                    Margin = new Thickness(2, 0, 0, 0)
                };
                chartCanvas.Children.Add(label);
                gridLabels.Add(label);
            }

            upLine = new Polyline { Stroke = Brushes.Green, StrokeThickness = 1 };
            downLine = new Polyline { Stroke = Brushes.Blue, StrokeThickness = 1 };
            
            chartCanvas.Children.Add(upLine);
            chartCanvas.Children.Add(downLine);
            
            grid.Children.Add(chartCanvas);
            Grid.SetRow(chartCanvas, 3);

            // Logs
            logBox = new TextBox {
                IsReadOnly = true,
                VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
                Background = System.Windows.Media.Brushes.Black,
                Foreground = System.Windows.Media.Brushes.LightGreen,
                FontFamily = new System.Windows.Media.FontFamily("Consolas")
            };
            grid.Children.Add(logBox);
            Grid.SetRow(logBox, 4);

            Content = grid;
            
            // We handle Closing manually for tray support
            // Closed += (s, e) => StopServer(); 

            // Auto Start
            Loaded += (s, e) => StartServer();

            // Tray Icon
            SetupTrayIcon();
        }

        private void ShowHelp() {
            var helpWindow = new Window {
                Title = "帮助 - Windows 代理管理器",
                Width = 800,
                Height = 700,
                WindowStartupLocation = WindowStartupLocation.CenterScreen
            };

            var mainGrid = new Grid();
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
            mainGrid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

            var webBrowser = new WebBrowser { Margin = new Thickness(10) };

            string markdownContent = "未找到 使用说明.md 文件。";
            if (File.Exists("使用说明.md")) {
                markdownContent = File.ReadAllText("使用说明.md");
            }

            // Simple Markdown to HTML converter
            string htmlContent = ConvertMarkdownToHtml(markdownContent);
            webBrowser.NavigateToString(htmlContent);

            mainGrid.Children.Add(webBrowser);
            
            // Author Info
            var infoPanel = new StackPanel { Orientation = Orientation.Vertical, Margin = new Thickness(10) };
            infoPanel.Children.Add(new TextBlock { Text = "版本: 1.0", FontWeight = FontWeights.Bold, HorizontalAlignment = HorizontalAlignment.Center, Margin = new Thickness(0, 0, 0, 5) });
            infoPanel.Children.Add(new TextBlock { Text = "作者: howard chang", FontWeight = FontWeights.Bold, HorizontalAlignment = HorizontalAlignment.Center });
            
            var linkBlock = new TextBlock { Text = "https://github.com/git-howard", Foreground = System.Windows.Media.Brushes.Blue,  HorizontalAlignment = HorizontalAlignment.Center, Cursor = System.Windows.Input.Cursors.Hand, TextDecorations = TextDecorations.Underline };
            linkBlock.MouseDown += (s, e) => {
                try { Process.Start("https://github.com/git-howard"); } catch { }
            };
            infoPanel.Children.Add(linkBlock);
            
            mainGrid.Children.Add(infoPanel);
            Grid.SetRow(infoPanel, 1);

            helpWindow.Content = mainGrid;
            helpWindow.ShowDialog();
        }

        private string ConvertMarkdownToHtml(string markdown) {
            var sb = new StringBuilder();
            sb.Append("<html><head><meta http-equiv='Content-Type' content='text/html;charset=UTF-8'><style>");
            sb.Append("body { font-family: 'Microsoft YaHei', 'Segoe UI', sans-serif; line-height: 1.6; padding: 20px; color: #333; }");
            sb.Append("h1, h2, h3 { color: #007ACC; border-bottom: 1px solid #eee; padding-bottom: 5px; }");
            sb.Append("code { background-color: #f0f0f0; padding: 2px 4px; border-radius: 3px; font-family: Consolas, monospace; }");
            sb.Append("pre { background-color: #f6f8fa; padding: 10px; border-radius: 5px; overflow-x: auto; border: 1px solid #e1e4e8; }");
            sb.Append("pre code { background-color: transparent; padding: 0; }");
            sb.Append("blockquote { border-left: 4px solid #dfe2e5; padding-left: 10px; color: #6a737d; margin: 10px 0; }");
            sb.Append("ul, ol { padding-left: 20px; }");
            sb.Append("a { color: #0366d6; text-decoration: none; }");
            sb.Append("a:hover { text-decoration: underline; }");
            sb.Append("</style></head><body>");

            var lines = markdown.Split(new[] { "\r\n", "\n" }, StringSplitOptions.None);
            bool inCodeBlock = false;
            bool inList = false;

            foreach (var line in lines) {
                string trimmed = line.Trim();

                // Code Block
                if (trimmed.StartsWith("```")) {
                    if (inCodeBlock) {
                        sb.Append("</code></pre>");
                    } else {
                        sb.Append("<pre><code>");
                    }
                    inCodeBlock = !inCodeBlock;
                    continue;
                }

                if (inCodeBlock) {
                    sb.Append(System.Web.HttpUtility.HtmlEncode(line) + "\n");
                    continue;
                }

                // Headers
                if (trimmed.StartsWith("# ")) {
                    sb.Append("<h1>" + System.Web.HttpUtility.HtmlEncode(trimmed.Substring(2)) + "</h1>");
                } else if (trimmed.StartsWith("## ")) {
                    sb.Append("<h2>" + System.Web.HttpUtility.HtmlEncode(trimmed.Substring(3)) + "</h2>");
                } else if (trimmed.StartsWith("### ")) {
                    sb.Append("<h3>" + System.Web.HttpUtility.HtmlEncode(trimmed.Substring(4)) + "</h3>");
                } 
                // List Items
                else if (trimmed.StartsWith("- ")) {
                    if (!inList) {
                        sb.Append("<ul>");
                        inList = true;
                    }
                    // Handle links in list items [text](#link) - basic support
                    string content = System.Web.HttpUtility.HtmlEncode(trimmed.Substring(2));
                    content = System.Text.RegularExpressions.Regex.Replace(content, @"\[(.*?)\]\((.*?)\)", "<a href='$2'>$1</a>");
                    // Handle bold **text**
                    content = System.Text.RegularExpressions.Regex.Replace(content, @"\*\*(.*?)\*\*", "<b>$1</b>");
                    // Handle inline code `text`
                     content = System.Text.RegularExpressions.Regex.Replace(content, @"`(.*?)`", "<code>$1</code>");

                    sb.Append("<li>" + content + "</li>");
                } 
                // Blockquote
                else if (trimmed.StartsWith("> ")) {
                    sb.Append("<blockquote>" + System.Web.HttpUtility.HtmlEncode(trimmed.Substring(2)) + "</blockquote>");
                }
                // Empty line
                else if (string.IsNullOrWhiteSpace(line)) {
                    if (inList) {
                        sb.Append("</ul>");
                        inList = false;
                    }
                    // sb.Append("<br>"); // Optional: ignore empty lines or add spacing
                } 
                // Normal Paragraph
                else {
                    if (inList) {
                        sb.Append("</ul>");
                        inList = false;
                    }
                    string content = System.Web.HttpUtility.HtmlEncode(line);
                    // Handle bold **text**
                    content = System.Text.RegularExpressions.Regex.Replace(content, @"\*\*(.*?)\*\*", "<b>$1</b>");
                    // Handle inline code `text`
                    content = System.Text.RegularExpressions.Regex.Replace(content, @"`(.*?)`", "<code>$1</code>");
                    
                    sb.Append("<p>" + content + "</p>");
                }
            }
            
            if (inList) sb.Append("</ul>");

            sb.Append("</body></html>");
            return sb.ToString();
        }

        private void SetupTrayIcon() {
            notifyIcon = new System.Windows.Forms.NotifyIcon();
            notifyIcon.Text = "Windows 代理管理器";
            
            if (File.Exists(IconFile)) {
                try {
                    notifyIcon.Icon = new System.Drawing.Icon(IconFile);
                } catch {
                    notifyIcon.Icon = System.Drawing.SystemIcons.Application;
                }
            } else {
                notifyIcon.Icon = System.Drawing.SystemIcons.Application;
            }
            
            notifyIcon.Visible = true;
            
            notifyIcon.DoubleClick += (s, e) => {
                Show();
                WindowState = WindowState.Normal;
            };

            var contextMenu = new System.Windows.Forms.ContextMenu();
            contextMenu.MenuItems.Add("打开", (s, e) => {
                Show();
                WindowState = WindowState.Normal;
            });
            contextMenu.MenuItems.Add("退出", (s, e) => {
                if (systemProxyCheck.IsChecked == true) SetSystemProxy(false);
                StopServer();
                notifyIcon.Visible = false;
                System.Windows.Application.Current.Shutdown();
            });
            notifyIcon.ContextMenu = contextMenu;
        }

        protected override void OnStateChanged(EventArgs e) {
            if (WindowState == WindowState.Minimized) {
                Hide();
            }
            base.OnStateChanged(e);
        }

        protected override void OnClosing(CancelEventArgs e) {
            e.Cancel = true;
            Hide();
            base.OnClosing(e);
        }

        private void LoadConfig() {
             if (File.Exists(ConfigFile)) {
                configBox.Text = File.ReadAllText(ConfigFile);
            } else {
                configBox.Text = "# Windows 代理配置\n" +
                                 "# SERVER <ip> <port>\n" +
                                 "# WHITELIST <ip_range> (支持 IP, CIDR, Range)\n" + 
                                 "# USER <name> <pass>\n" +
                                 "# UPSTREAM <id> <type> <host> <port>\n" +
                                 "# RULE <pattern> <upstreamId>\n" +
                                 "# RULE_FILE <file> <upstreamId>\n" +
                                 "# GFWLIST_UPSTREAM <upstreamId>\n\n" +
                                 "SERVER 0.0.0.0 8080\n" +
                                 "# WHITELIST 127.0.0.1,192.168.1.0/24\n" +
                                 "USER admin 123456\n";
            }
        }

        private void UpdateGfwList() {
            Log("正在下载 GFWList...");
            new System.Threading.Thread(() => {
                try {
                    // Enable TLS 1.2 (required for GitHub)
                    // 3072 is the enum value for Tls12 in .NET 4.5+
                    ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;

                    using (var client = new WebClient()) {
                        string url = "https://raw.githubusercontent.com/gfwlist/gfwlist/master/gfwlist.txt";
                        string base64Content = client.DownloadString(url);
                        byte[] data = Convert.FromBase64String(base64Content);
                        string decoded = Encoding.UTF8.GetString(data);
                        File.WriteAllText(GfwListFile, decoded);
                        Log("GFWList 更新成功。");
                    }
                } catch (Exception ex) {
                    Log("GFWList 更新失败: " + ex.Message);
                }
            }).Start();
        }

        private void UpdateServer() {
            try {
                File.WriteAllText(ConfigFile, configBox.Text);
                Log("配置已保存。");
            } catch (Exception ex) {
                Log("配置保存失败: " + ex.Message);
                return;
            }

            // Update System Proxy if port changed
            if (systemProxyCheck.IsChecked == true) {
                SetSystemProxy(true);
            }

            if (proxyProcess != null && !proxyProcess.HasExited) {
                StopServer();
                // Give a small delay for port release if needed, but let's try direct restart first.
                System.Threading.Thread.Sleep(200); 
            }
            StartServer();
            Log("配置已应用，服务已重启。");
        }

        private void ToggleServer() {
            if (proxyProcess == null || proxyProcess.HasExited) {
                StartServer();
            } else {
                StopServer();
            }
        }

        // Job Object Definitions
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr CreateJobObject(IntPtr lpJobAttributes, string lpName);

        [DllImport("kernel32.dll")]
        static extern bool SetInformationJobObject(IntPtr hJob, int JobObjectInfoClass, IntPtr lpJobObjectInfo, uint cbJobObjectInfoLength);

        [DllImport("kernel32.dll")]
        static extern bool AssignProcessToJobObject(IntPtr hJob, IntPtr hProcess);

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
            public Int64 PerProcessUserTimeLimit;
            public Int64 PerJobUserTimeLimit;
            public UInt32 LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public UInt32 ActiveProcessLimit;
            public UIntPtr Affinity;
            public UInt32 PriorityClass;
            public UInt32 SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct IO_COUNTERS {
            public UInt64 ReadOperationCount;
            public UInt64 WriteOperationCount;
            public UInt64 OtherOperationCount;
            public UInt64 ReadTransferCount;
            public UInt64 WriteTransferCount;
            public UInt64 OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
            public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        const int JobObjectExtendedLimitInformation = 9;
        const UInt32 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;

        private IntPtr hJob = IntPtr.Zero;

        private void StartServer() {
            if (!File.Exists(ProxyExe)) {
                Log("错误: 未找到 ProxyServer.exe (" + ProxyExe + ")");
                return;
            }

            try {
                var startInfo = new ProcessStartInfo {
                    FileName = ProxyExe,
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                    RedirectStandardError = true,
                    CreateNoWindow = true
                };

                proxyProcess = new Process { StartInfo = startInfo };
                proxyProcess.OutputDataReceived += (s, e) => Log(e.Data);
                proxyProcess.ErrorDataReceived += (s, e) => Log(e.Data);
                proxyProcess.Start();
                
                // Assign to Job Object
                if (hJob == IntPtr.Zero) {
                    hJob = CreateJobObject(IntPtr.Zero, null);
                    var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
                    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                    
                    int length = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
                    IntPtr pInfo = Marshal.AllocHGlobal(length);
                    Marshal.StructureToPtr(info, pInfo, false);
                    SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, pInfo, (uint)length);
                    Marshal.FreeHGlobal(pInfo);
                }
                AssignProcessToJobObject(hJob, proxyProcess.Handle);

                proxyProcess.BeginOutputReadLine();
                proxyProcess.BeginErrorReadLine();

                startButton.Content = "停止服务";
                Log("服务已启动。");
                
                if (systemProxyCheck.IsChecked == true) {
                    SetSystemProxy(true);
                }
            } catch (Exception ex) {
                Log("启动服务失败: " + ex.Message);
            }
        }

        private void StopServer() {
            if (proxyProcess != null && !proxyProcess.HasExited) {
                proxyProcess.Kill();
                proxyProcess = null;
            }
            startButton.Content = "启动服务";
            Log("服务已停止。");
            
            if (systemProxyCheck.IsChecked == true) {
                SetSystemProxy(false); // Only disable if we enabled it (but actually we should probably always clear on exit if we managed it)
                // Actually better to check if we are stopping to exit app or just toggling
            }
        }
        
        [DllImport("wininet.dll")]
        public static extern bool InternetSetOption(IntPtr hInternet, int dwOption, IntPtr lpBuffer, int dwBufferLength);
        public const int INTERNET_OPTION_SETTINGS_CHANGED = 39;
        public const int INTERNET_OPTION_REFRESH = 37;

        private void SetSystemProxy(bool enable) {
            try {
                string proxyServer = "127.0.0.1:8080"; // Default
                
                // Try to parse port from config
                // SERVER 0.0.0.0 8080
                foreach(var line in configBox.Text.Split('\n')) {
                    var parts = line.Trim().Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                    if (parts.Length >= 3 && parts[0] == "SERVER") {
                        // We use local loopback for system proxy even if bound to 0.0.0.0
                        proxyServer = "127.0.0.1:" + parts[2]; 
                        break;
                    }
                }

                RegistryKey registry = Registry.CurrentUser.OpenSubKey("Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings", true);
                if (enable) {
                    registry.SetValue("ProxyEnable", 1);
                    registry.SetValue("ProxyServer", proxyServer);
                    Log("系统代理已设置为 " + proxyServer);
                } else {
                    registry.SetValue("ProxyEnable", 0);
                    Log("系统代理已关闭。");
                }
                registry.Close();

                // Notify system that proxy settings have changed
                InternetSetOption(IntPtr.Zero, INTERNET_OPTION_SETTINGS_CHANGED, IntPtr.Zero, 0);
                InternetSetOption(IntPtr.Zero, INTERNET_OPTION_REFRESH, IntPtr.Zero, 0);
                
            } catch (Exception ex) {
                Log("设置系统代理失败: " + ex.Message);
            }
        }
        
        private bool IsAutoStartEnabled() {
            try {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", false)) {
                    return key.GetValue("WindowsProxyManager") != null;
                }
            } catch { return false; }
        }

        private void SetAutoStart(bool enable) {
            try {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", true)) {
                    if (enable) {
                        // Use current executable path
                        string path = System.Reflection.Assembly.GetExecutingAssembly().Location;
                        key.SetValue("WindowsProxyManager", "\"" + path + "\"");
                        Log("已开启开机自启。");
                    } else {
                        key.DeleteValue("WindowsProxyManager", false);
                        Log("已关闭开机自启。");
                    }
                }
            } catch (Exception ex) {
                Log("设置开机自启失败: " + ex.Message);
            }
        }

        private void Log(string message) {
            if (string.IsNullOrEmpty(message)) return;
            
            // Check for stats line
            if (message.StartsWith("STATS:")) {
                ParseStats(message);
                return;
            }

            Dispatcher.Invoke(() => {
                logBox.AppendText(message + "\n");
                logBox.ScrollToEnd();
            });
        }

        private void ParseStats(string line) {
            // STATS: TOTAL_UP=... TOTAL_DOWN=... TODAY_UP=... TODAY_DOWN=... SPEED_UP=... SPEED_DOWN=...
            try {
                var parts = line.Substring(6).Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
                long totalUp = 0, totalDown = 0, todayUp = 0, todayDown = 0;
                long speedUp = 0, speedDown = 0;
                
                foreach (var part in parts) {
                    var kv = part.Split('=');
                    if (kv.Length == 2) {
                        long val;
                        if (long.TryParse(kv[1], out val)) {
                            if (kv[0] == "TOTAL_UP") totalUp = val;
                            else if (kv[0] == "TOTAL_DOWN") totalDown = val;
                            else if (kv[0] == "TODAY_UP") todayUp = val;
                            else if (kv[0] == "TODAY_DOWN") todayDown = val;
                            else if (kv[0] == "SPEED_UP") speedUp = val;
                            else if (kv[0] == "SPEED_DOWN") speedDown = val;
                        }
                    }
                }

                Dispatcher.Invoke(() => {
                    statsBlock.Text = string.Format(
                        "Today Up: {0} | Today Down: {1} | Total Up: {2} | Total Down: {3}",
                        FormatBytes(todayUp), FormatBytes(todayDown), FormatBytes(totalUp), FormatBytes(totalDown));
                    
                    speedBlock.Text = string.Format(
                        "Speed: {0}/s Up (Green) | {1}/s Down (Blue)",
                        FormatBytes(speedUp), FormatBytes(speedDown));

                    UpdateChart(speedUp, speedDown);
                });

            } catch {}
        }

        private void UpdateChart(long up, long down) {
            upHistory.Add((double)up);
            downHistory.Add((double)down);
            
            if (upHistory.Count > MaxChartPoints) upHistory.RemoveAt(0);
            if (downHistory.Count > MaxChartPoints) downHistory.RemoveAt(0);
            
            double maxVal = 1024; // Min scale 1KB
            if (upHistory.Any()) maxVal = Math.Max(maxVal, upHistory.Max());
            if (downHistory.Any()) maxVal = Math.Max(maxVal, downHistory.Max());
            
            // Update Points
            var upPoints = new PointCollection();
            var downPoints = new PointCollection();
            
            double widthStep = chartCanvas.ActualWidth / (MaxChartPoints - 1);
            if (widthStep < 1) widthStep = 5; // Default if not rendered yet
            
            // Update Grid Lines
            for (int i = 0; i < gridLines.Count; i++) {
                double percent = 0.25 * (i + 1); // 0.25, 0.5, 0.75
                double y = ChartHeight - (ChartHeight * percent);
                
                gridLines[i].X1 = 0;
                gridLines[i].X2 = chartCanvas.ActualWidth;
                gridLines[i].Y1 = y;
                gridLines[i].Y2 = y;

                long val = (long)(maxVal * percent);
                gridLabels[i].Text = FormatBytes(val) + "/s";
                Canvas.SetTop(gridLabels[i], y - 12);
            }

            for (int i = 0; i < upHistory.Count; i++) {
                double x = i * widthStep;
                double yUp = ChartHeight - (upHistory[i] / maxVal * ChartHeight);
                double yDown = ChartHeight - (downHistory[i] / maxVal * ChartHeight);
                
                upPoints.Add(new Point(x, yUp));
                downPoints.Add(new Point(x, yDown));
            }
            
            upLine.Points = upPoints;
            downLine.Points = downPoints;
        }

        private string FormatBytes(long bytes) {
            string[] suffixes = { "B", "KB", "MB", "GB", "TB" };
            int counter = 0;
            decimal number = (decimal)bytes;
            while (Math.Round(number / 1024) >= 1) {
                number = number / 1024;
                counter++;
            }
            return string.Format("{0:n1}{1}", number, suffixes[counter]);
        }

        [DllImport("user32.dll")]
        [return: MarshalAs(UnmanagedType.Bool)]
        static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
        private const int SW_RESTORE = 9;

        // IPC Constants
        private const string IpcChannelName = "WindowsProxyManagerIpc";
        private const int WM_USER = 0x0400;
        private const int WM_SHOW_WINDOW = WM_USER + 1;

        protected override void OnSourceInitialized(EventArgs e) {
            base.OnSourceInitialized(e);
            var source = System.Windows.Interop.HwndSource.FromHwnd(new System.Windows.Interop.WindowInteropHelper(this).Handle);
            source.AddHook(WndProc);
        }

        private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled) {
            if (msg == WM_SHOW_WINDOW) {
                Show();
                WindowState = WindowState.Normal;
                Activate();
                handled = true;
            }
            return IntPtr.Zero;
        }

        [DllImport("user32.dll", SetLastError = true)]
        static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        static extern IntPtr SendMessage(IntPtr hWnd, UInt32 Msg, IntPtr wParam, IntPtr lParam);

        [STAThread]
        public static void Main() {
            bool createdNew;
            using (Mutex mutex = new Mutex(true, "WindowsProxyManagerUniqueMutexName", out createdNew)) {
                if (createdNew) {
                    var app = new Application();
                    app.Run(new SimpleGui());
                } else {
                    // Try to find window by Title
                    IntPtr hWnd = FindWindow(null, "Windows 代理管理器");
                    if (hWnd != IntPtr.Zero) {
                        SendMessage(hWnd, WM_SHOW_WINDOW, IntPtr.Zero, IntPtr.Zero);
                        SetForegroundWindow(hWnd);
                    } else {
                        // If window not found (maybe just initializing or title changed), try process search again as fallback
                        Process current = Process.GetCurrentProcess();
                        foreach (Process process in Process.GetProcessesByName(current.ProcessName)) {
                            if (process.Id != current.Id) {
                                if (process.MainWindowHandle != IntPtr.Zero) {
                                    ShowWindow(process.MainWindowHandle, SW_RESTORE);
                                    SetForegroundWindow(process.MainWindowHandle);
                                    return;
                                }
                            }
                        }
                        MessageBox.Show("程序已经在运行中！", "Windows Proxy Manager", MessageBoxButton.OK, MessageBoxImage.Information);
                    }
                }
            }
        }
    }
}
