#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <process.h>
#include <iostream>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

// --- Forwarding ---
#pragma comment(linker, "/export:GetFileVersionInfoA=C:\\Windows\\System32\\version.GetFileVersionInfoA")
#pragma comment(linker, "/export:GetFileVersionInfoSizeA=C:\\Windows\\System32\\version.GetFileVersionInfoSizeA")
#pragma comment(linker, "/export:VerQueryValueA=C:\\Windows\\System32\\version.VerQueryValueA")

// --- เพิ่มส่วน Global Variables ---
SOCKET bridgeSocket = INVALID_SOCKET;
uintptr_t baseAddress = (uintptr_t)GetModuleHandle(NULL);

// ฟังก์ชันสำหรับ "ส่ง" ของตัวเกม (Clear Text -> Gepard -> Server)
// คุณต้องหา Address นี้จาก x64dbg (เช่น CSession::SendPacket)
typedef void(__thiscall* tSendPacket)(void* ecx, int len, char* buf);
tSendPacket GameSendPacket = (tSendPacket)(baseAddress + 0x00XXXXXX); // <-- ใส่ Address ที่หาได้ที่นี่
void* g_ecx_pointer = nullptr; // เก็บ pointer ของ session ไว้ใช้ส่งกลับ
void* g_session_ptr = nullptr;

// define สำหรับการเชื่อมต่อกับบอท
typedef int (WSAAPI* connect_t)(SOCKET s, const struct sockaddr* name, int namelen);
connect_t pOriginalConnect = NULL;
BYTE origBytes[5];

// --- Memory Helper ---
void Patch(void* dest, void* src, int len) {
    DWORD old;
    VirtualProtect(dest, len, PAGE_EXECUTE_READWRITE, &old);
    memcpy(dest, src, len);
    VirtualProtect(dest, len, old, &old);
}

// --- Hook Function ---
int WSAAPI MyConnectHook(SOCKET s, const struct sockaddr* name, int namelen) {
    struct sockaddr_in* addr = (struct sockaddr_in*)name;
    unsigned short port = ntohs(addr->sin_port);

    // 🎯 ถ้า Port เกิน 5000 ให้เลี้ยวเข้าบอททันที
    if (port >= 5000) {
        printf("[REDIRECT] Port %d -> 127.0.0.1:6991\n", port);
        addr->sin_addr.s_addr = inet_addr("127.0.0.1");
        addr->sin_port = htons(6991);
    }

    // Unhook ชั่วคราวเพื่อเรียกฟังก์ชันจริง
    Patch(pOriginalConnect, origBytes, 5);
    int res = pOriginalConnect(s, name, namelen);
    
    // Re-hook กลับ
    BYTE jmp[5] = { 0xE9 };
    *(DWORD*)(jmp + 1) = (DWORD)MyConnectHook - (DWORD)pOriginalConnect - 5;
    Patch(pOriginalConnect, jmp, 5);

    return res;
}

/* // --- Step 2 & 3: รับคำสั่งจาก OpenKore แล้วสั่งเกมส่ง Packet ---
void ListenFromOpenKore() {
    char recvBuf[2048];
    while (true) {
        if (bridgeSocket != INVALID_SOCKET) {
            int bytesReceived = recv(bridgeSocket, recvBuf, sizeof(recvBuf), 0);
            if (bytesReceived > 0 && g_ecx_pointer != nullptr) {
                // Step 3: เรียกฟังก์ชันในเกมเพื่อส่ง Packet (ผ่าน Gepard 100%)
                GameSendPacket(g_ecx_pointer, bytesReceived, recvBuf);
            }
        }
        Sleep(10);
    }
} */

// --- Step 2 & 3: รับคำสั่งจาก OpenKore แล้วส่งเข้า Game Engine ---
void __cdecl OpenKoreListener(void* p) {
    char recvBuf[4096];
    while (true) {
        if (bridgeSocket != INVALID_SOCKET) {
            int bytes = recv(bridgeSocket, recvBuf, sizeof(recvBuf), 0);
            if (bytes > 0 && g_session_ptr != nullptr) {
                // Step 3: สั่งให้ Game Client เป็นคนส่ง (Gepard จะ Encrypt ให้เอง)
                // เราเรียกใช้ฟังก์ชันภายในเกมตรงๆ เพื่อความเนียน
                GameSendPacket(g_session_ptr, bytes, recvBuf);
            }
        }
        Sleep(10); // ป้องกัน CPU Overload
    }
}

// --- Step 1: Hook ฟังก์ชันเพื่อส่ง Clear Text ไป OpenKore ---
void __fastcall HookedSendPacket(void* ecx, void* edx, int len, char* buf) {
    g_ecx_pointer = ecx; // เก็บไว้ใช้สำหรับ Step 3

    // ส่ง Clear Text ไปให้ OpenKore อ่าน (Step 1)
    if (bridgeSocket != INVALID_SOCKET) {
        send(bridgeSocket, buf, len, 0);
    }

    // ปล่อยให้เกมทำงานปกติ (Gepard จะรับช่วงต่อตรงนี้)
    GameSendPacket(ecx, len, buf);
}

// --- ฟังก์ชันเชื่อมต่อกับ OpenKore Bridge ---
void ConnectToOpenKore() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    bridgeSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(12345);

    while (connect(bridgeSocket, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        Sleep(2000); // พยายามต่อจนกว่า OpenKore จะเปิด
    }
    _beginthread(OpenKoreListener, 0, NULL); // เริ่มรอรับคำสั่งทันทีที่ต่อติด
}

// --- Step 1: ปรับปรุงจุด LogPacket เดิมของคุณ ---
void LogPacket(int packetID, char* buf, int len, void* ecx) {
    // เก็บ Session Pointer ไว้ใช้ส่งกลับใน Step 3
    if (ecx != nullptr) g_session_ptr = ecx;

    // ส่ง Clear Text ไปให้ OpenKore (Step 1)
    if (bridgeSocket != INVALID_SOCKET) {
        // ส่ง Packet ID + Data ไปให้บอทตัดสินใจ
        send(bridgeSocket, buf, len, 0);
    }

    // ส่วนนี้คือ Log เดิมของคุณ (ปล่อยไว้ดู Debug)
    // printf("Packet ID: 0x%04X, Len: %d\n", packetID, len);
}

void Start() {
    // 💡 รอจนกว่าตัวเกมจะเข้าหน้า Login (เพื่อให้ Gepard คลายการป้องกันบางส่วน)
    Sleep(5000); 
    
    HMODULE ws2 = GetModuleHandleA("ws2_32.dll");
    pOriginalConnect = (connect_t)GetProcAddress(ws2, "connect");

    if (pOriginalConnect) {
        memcpy(origBytes, pOriginalConnect, 5);
        BYTE jmp[5] = { 0xE9 };
        *(DWORD*)(jmp + 1) = (DWORD)MyConnectHook - (DWORD)pOriginalConnect - 5;
        Patch(pOriginalConnect, jmp, 5);
        printf("[✔] Universal Bridge Active!\n");
    }
}

BOOL APIENTRY DllMain(HMODULE h, DWORD r, LPVOID lp) {
    if (r == DLL_PROCESS_ATTACH) CreateThread(0, 0, (LPTHREAD_START_ROUTINE)Start, 0, 0, 0);
    return 1;
}

