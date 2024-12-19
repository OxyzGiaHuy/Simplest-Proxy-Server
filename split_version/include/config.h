#ifndef CONFIG_H
#define CONFIG_H

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <set>
#include <deque>
#include <atomic>

#define WM_SOCKET WM_USER + 1
#define DEFAULT_PORT "8888"
#define BUFFER_SIZE 4096
#define MAX_LOG_ENTRIES 30

extern std::vector<std::string> blacklist;
extern std::mutex blacklist_mutex;
extern SOCKET serverSocket;
extern HWND hWndEdit, hWndList, hWndStart, hWndStop, hWndUrl, hwndHostRunning, hWndClient;
extern HWND hWndUserGuide;
extern std::queue<std::string> List;
extern std::set<std::string> Clients;
extern std::vector<std::string> CurrentClients;
extern bool running;
extern std::deque<std::string> log_entries;
extern std::mutex log_mutex;
extern std::set<std::string> active_hosts;
extern std::mutex active_hosts_mutex;
extern std::atomic<int> activeConnections;

#endif