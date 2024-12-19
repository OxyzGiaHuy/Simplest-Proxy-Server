#include "../include/config.h"

std::vector<std::string> blacklist;
std::mutex blacklist_mutex;
SOCKET serverSocket = INVALID_SOCKET;
HWND hWndEdit, hWndList, hWndStart, hWndStop, hWndUrl, hwndHostRunning, hWndClient;
HWND hWndUserGuide;
std::queue<std::string> List;
std::set<std::string> Clients;
std::vector<std::string> CurrentClients;
bool running = false;
std::deque<std::string> log_entries;
std::mutex log_mutex;
std::set<std::string> active_hosts;
std::mutex active_hosts_mutex;
std::atomic<int> activeConnections(0);