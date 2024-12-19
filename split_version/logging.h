#pragma once

#include <string>
#include <queue>
#include <deque>
#include <mutex>
#include <windows.h>

void logMessage(const std::string &message);
void logMessageToFile(const std::string &message);
std::string getLogFileName();

extern HWND hWndEdit;
extern std::queue<std::string> List;
extern const int MAX_LOG_ENTRIES;
extern std::deque<std::string> log_entries;
extern std::mutex log_mutex;