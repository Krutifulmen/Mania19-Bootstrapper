#pragma once
#include <windows.h>
#include <wininet.h>
#include <string>
#include <sstream>
#include <ctime>

#pragma comment(lib, "wininet.lib")

namespace ManiaDiscordRPC
{
    static const char* DISCORD_CLIENT_ID = "1561830104496209970";

    inline std::string FetchPlaceNameFromApi(int placeId)
    {
        if (placeId <= 0) return "Mania Game";

        const char* hosts[] = { "mania19.xyz", "127.0.0.1" };
        for (int i = 0; i < 2; ++i)
        {
            std::string url = "http://" + std::string(hosts[i]) + "/places/api-get-details?assetId=" + std::to_string(placeId);

            HINTERNET hNet = InternetOpenA("ManiaRPC/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
            if (!hNet) continue;

            HINTERNET hUrl = InternetOpenUrlA(hNet, url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
            if (hUrl)
            {
                char buffer[2048];
                DWORD bytesRead = 0;
                std::string response;
                while (InternetReadFile(hUrl, buffer, sizeof(buffer) - 1, &bytesRead) && bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';
                    response.append(buffer, bytesRead);
                }
                InternetCloseHandle(hUrl);
                InternetCloseHandle(hNet);

                // Parse "Name":"..." from JSON
                size_t nPos = response.find("\"Name\":");
                if (nPos == std::string::npos) nPos = response.find("\"name\":");
                if (nPos != std::string::npos)
                {
                    size_t q1 = response.find("\"", nPos + 7);
                    if (q1 != std::string::npos)
                    {
                        size_t q2 = response.find("\"", q1 + 1);
                        if (q2 != std::string::npos)
                        {
                            std::string name = response.substr(q1 + 1, q2 - q1 - 1);
                            if (!name.empty()) return name;
                        }
                    }
                }
            }
            else
            {
                InternetCloseHandle(hNet);
            }
        }

        return "Place #" + std::to_string(placeId);
    }

    inline bool SendIPC(HANDLE hPipe, uint32_t opcode, const std::string& json)
    {
        if (hPipe == INVALID_HANDLE_VALUE || !hPipe) return false;
        uint32_t length = static_cast<uint32_t>(json.length());
        DWORD written = 0;
        if (!WriteFile(hPipe, &opcode, sizeof(opcode), &written, NULL)) return false;
        if (!WriteFile(hPipe, &length, sizeof(length), &written, NULL)) return false;
        if (!WriteFile(hPipe, json.c_str(), length, &written, NULL)) return false;

        uint32_t respOp = 0, respLen = 0;
        DWORD bytesRead = 0;
        if (ReadFile(hPipe, &respOp, sizeof(respOp), &bytesRead, NULL) &&
            ReadFile(hPipe, &respLen, sizeof(respLen), &bytesRead, NULL))
        {
            if (respLen > 0)
            {
                std::string buf(respLen, '\0');
                ReadFile(hPipe, &buf[0], respLen, &bytesRead, NULL);
            }
            return true;
        }
        return false;
    }

    inline HANDLE ConnectDiscord()
    {
        for (int i = 0; i < 10; ++i)
        {
            wchar_t pipeName[64];
            swprintf_s(pipeName, L"\\\\.\\pipe\\discord-ipc-%d", i);
            HANDLE hPipe = CreateFileW(pipeName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (hPipe != INVALID_HANDLE_VALUE)
            {
                std::string handshake = "{\"v\":1,\"client_id\":\"" + std::string(DISCORD_CLIENT_ID) + "\"}";
                if (SendIPC(hPipe, 0, handshake))
                {
                    return hPipe;
                }
                CloseHandle(hPipe);
            }
        }
        return INVALID_HANDLE_VALUE;
    }

    inline std::string EscapeJson(const std::string& s)
    {
        std::string res;
        for (char c : s)
        {
            if (c == '"') res += "\\\"";
            else if (c == '\\') res += "\\\\";
            else if (c == '\n' || c == '\r' || c == '\t') res += " ";
            else res += c;
        }
        return res;
    }

    inline void SetActivity(HANDLE hPipe, DWORD pid, const std::string& placeName, int userId, time_t startTime)
    {
        if (hPipe == INVALID_HANDLE_VALUE || !hPipe) return;

        std::string escapedPlace = EscapeJson(placeName);
        std::ostringstream ss;
        ss << "{"
           << "\"cmd\":\"SET_ACTIVITY\","
           << "\"args\":{"
           << "\"pid\":" << pid << ","
           << "\"activity\":{"
           << "\"details\":\"Playing Mania\","
           << "\"state\":\"" << escapedPlace << "\","
           << "\"timestamps\":{\"start\":" << startTime << "},"
           << "\"assets\":{\"large_image\":\"mania_logo\",\"large_text\":\"Mania\"},"
           << "\"buttons\":["
           << "{\"label\":\"Play Mania\",\"url\":\"http://mania19.xyz\"},"
           << "{\"label\":\"View Profile\",\"url\":\"http://mania19.xyz/users/" << (userId > 0 ? userId : 1) << "/profile\"}"
           << "]"
           << "}},"
           << "\"nonce\":\"" << startTime << "\""
           << "}";

        SendIPC(hPipe, 1, ss.str());
    }

    inline void ClearActivity(HANDLE hPipe, DWORD pid)
    {
        if (hPipe == INVALID_HANDLE_VALUE || !hPipe) return;
        std::ostringstream ss;
        ss << "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" << pid << ",\"activity\":null},\"nonce\":\"clear\"}";
        SendIPC(hPipe, 1, ss.str());
    }

    inline void MonitorGameProcess(DWORD pid, HANDLE hProcess, int placeId, int userId)
    {
        std::string placeName = FetchPlaceNameFromApi(placeId);
        HANDLE hPipe = ConnectDiscord();
        time_t startTime = time(NULL);

        if (hPipe != INVALID_HANDLE_VALUE)
        {
            SetActivity(hPipe, pid, placeName, userId, startTime);
        }

        while (true)
        {
            DWORD waitRes = WaitForSingleObject(hProcess, 1500);
            if (waitRes != WAIT_TIMEOUT)
            {
                break;
            }

            if (hPipe == INVALID_HANDLE_VALUE)
            {
                hPipe = ConnectDiscord();
                if (hPipe != INVALID_HANDLE_VALUE)
                {
                    SetActivity(hPipe, pid, placeName, userId, startTime);
                }
            }
        }

        if (hPipe != INVALID_HANDLE_VALUE)
        {
            ClearActivity(hPipe, pid);
            CloseHandle(hPipe);
        }
        CloseHandle(hProcess);
    }
}
