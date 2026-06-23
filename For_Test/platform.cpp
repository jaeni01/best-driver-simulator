#include "platform.h"
#ifdef _WIN32
static DWORD g_origMode = 0;
void platformInit() {
    HWND hw = GetConsoleWindow(); ShowWindow(hw, SW_MAXIMIZE); Sleep(200);
    HANDLE ho = GetStdHandle(STD_OUTPUT_HANDLE); DWORD m = 0;
    GetConsoleMode(ho, &m); SetConsoleMode(ho, m | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    HANDLE hi = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hi, &g_origMode); SetConsoleMode(hi, ENABLE_EXTENDED_FLAGS);
    SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
}
void platformDone() { SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), g_origMode); }
void getTerminalSize(int* c, int* r) {
    CONSOLE_SCREEN_BUFFER_INFO i;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &i);
    *c = i.srWindow.Right - i.srWindow.Left + 1; *r = i.srWindow.Bottom - i.srWindow.Top + 1;
}
int readKey() {
    if (!_kbhit()) return BD_K_NONE;
    int c = _getch();
    if (c == 0 || c == 0xE0) { int e = _getch(); if (e == 72) return BD_K_UP; if (e == 80) return BD_K_DN; if (e == 75) return BD_K_LT; if (e == 77) return BD_K_RT; return BD_K_NONE; }
    return (c == 27) ? BD_K_ESC : c;
}
void pollKeys(KeyState& ks) {
    ks.up = (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
    ks.down = (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;
    ks.left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
    ks.right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
    ks.w = (GetAsyncKeyState('W') & 0x8000) != 0;
    ks.s = (GetAsyncKeyState('S') & 0x8000) != 0;
    ks.a = (GetAsyncKeyState('A') & 0x8000) != 0;
    ks.d = (GetAsyncKeyState('D') & 0x8000) != 0;
    ks.e = (GetAsyncKeyState('E') & 0x8000) != 0;
    ks.g = (GetAsyncKeyState('G') & 0x8000) != 0;
    ks.x = (GetAsyncKeyState('X') & 0x8000) != 0;
    ks.z = (GetAsyncKeyState('Z') & 0x8000) != 0;
    ks.r = (GetAsyncKeyState('R') & 0x8000) != 0;
    ks.q = (GetAsyncKeyState('Q') & 0x8000) != 0;
    ks.v = (GetAsyncKeyState('V') & 0x8000) != 0;
    ks.b = (GetAsyncKeyState('B') & 0x8000) != 0;
    ks.esc = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    ks.space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
    ks.plus = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_ADD) & 0x8000) != 0;
    ks.minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0;
    while (_kbhit()) _getch();
}
#else
static struct termios g_ot; static int g_rm = 0;
void platformInit() { tcgetattr(0, &g_ot); struct termios r = g_ot; r.c_lflag &= ~(ICANON | ECHO); r.c_cc[VMIN] = 0; r.c_cc[VTIME] = 0; tcsetattr(0, TCSANOW, &r); g_rm = 1; }
void platformDone() { if (g_rm) { tcsetattr(0, TCSANOW, &g_ot); g_rm = 0; } }
void getTerminalSize(int* c, int* r) { struct winsize w; ioctl(1, TIOCGWINSZ, &w); *c = w.ws_col; *r = w.ws_row; }
int readKey() { unsigned char c; if (read(0, &c, 1) != 1) return BD_K_NONE; if (c == 27) { unsigned char s[2]; if (read(0, &s[0], 1) != 1) return BD_K_ESC; if (read(0, &s[1], 1) != 1) return BD_K_ESC; if (s[0] == '[') { if (s[1] == 'A') return BD_K_UP; if (s[1] == 'B') return BD_K_DN; if (s[1] == 'C') return BD_K_RT; if (s[1] == 'D') return BD_K_LT; } return BD_K_ESC; } return (int)c; }
void pollKeys(KeyState& ks) {
    int k = readKey();
    while (k != BD_K_NONE) {
        if (k == BD_K_UP) ks.up = true; else if (k == BD_K_DN) ks.down = true;
        else if (k == BD_K_LT) ks.left = true; else if (k == BD_K_RT) ks.right = true;
        else if (k == 'w' || k == 'W') ks.w = true; else if (k == 's' || k == 'S') ks.s = true;
        else if (k == 'a' || k == 'A') ks.a = true; else if (k == 'd' || k == 'D') ks.d = true;
        else if (k == 'e' || k == 'E') ks.e = true; else if (k == 'g' || k == 'G') ks.g = true;
        else if (k == 'x' || k == 'X') ks.x = true; else if (k == 'z' || k == 'Z') ks.z = true;
        else if (k == 'r' || k == 'R') ks.r = true; else if (k == 'q' || k == 'Q') ks.q = true;
        else if (k == 'v' || k == 'V') ks.v = true;
        else if (k == 'b' || k == 'B') ks.b = true;
        else if (k == ' ') ks.space = true;
        else if (k == '+' || k == '=') ks.plus = true;
        else if (k == '-' || k == '_') ks.minus = true;
        else if (k == BD_K_ESC) ks.esc = true;
        k = readKey();
    }
}
#endif