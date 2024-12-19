#include "logging.h"
#include <iostream>
#include <fstream>
#include <ctime>
#include <windows.h>
#include <mutex>

HWND hWndEdit;
std::queue<std::string> List;
const int MAX_LOG_ENTRIES = 100;
std::deque<std::string> log_entries;
std::mutex log_mutex;

std::string getLogFileName()
{
    // Lấy ngày hiện tại
    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "log-%d-%m-%Y.txt", localTime);

    return std::string(buffer);
}

// Hàm ghi log vào file
void logMessageToFile(const std::string &message)
{
    // Lấy tên file log
    std::string logFileName = getLogFileName();

    // Mở file ở chế độ append
    std::fstream logFile(logFileName, std::ios::app);
    if (!logFile.is_open())
    {
        std::cerr << "Failed to open the log file: " << logFileName << "\n";
        return;
    }

    // Lấy thời gian hiện tại để thêm vào log
    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);

    // Định dạng thời gian log
    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", localTime);
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        logFile << "[" << timeBuffer << "] " << message << "\n";
    }

    logFile.close();
}

void logMessage(const std::string &message)
{
    std::time_t now = std::time(nullptr);
    std::tm *localTime = std::localtime(&now);

    char timeBuffer[20];
    std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S %d/%m/%Y", localTime);
    std::string currentTime(timeBuffer);
    std::string tmp;
    tmp = "[" + currentTime + "] " + message + "\r\n";

    List.push(tmp);

    if (List.size() > MAX_LOG_ENTRIES)
    {
        List.pop();
    }

    std::string logContent;
    std::queue<std::string> tempQueue = List;
    while (!tempQueue.empty())
    {
        logContent += tempQueue.front();
        tempQueue.pop();
    }

    SetWindowTextA(hWndEdit, logContent.c_str());

    int textLength = GetWindowTextLengthA(hWndEdit);
    SendMessageA(hWndEdit, EM_SETSEL, textLength, textLength);
    SendMessageA(hWndEdit, WM_VSCROLL, SB_BOTTOM, 0);
}