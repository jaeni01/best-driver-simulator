#pragma once
#ifndef BESTDRIVER_PLATFORM_H
#define BESTDRIVER_PLATFORM_H
#include <cstdio>
#include <cstdarg>
#include <string>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#endif

#define BD_K_NONE  0
#define BD_K_UP    1000
#define BD_K_DN    1001
#define BD_K_LT    1002
#define BD_K_RT    1003
#define BD_K_ESC   27

void platformInit();
void platformDone();
void getTerminalSize(int* cols, int* rows);
int  readKey();

struct KeyState {
    bool up, down, left, right;
    bool w, s, a, d, e, g, x, z, r, q, v, b, esc;
    bool space, plus, minus;
    KeyState() : up(false), down(false), left(false), right(false),
        w(false), s(false), a(false), d(false), e(false), g(false),
        x(false), z(false), r(false), q(false), v(false), b(false), esc(false),
        space(false), plus(false), minus(false) {
    }
};
void pollKeys(KeyState& ks);

class ScreenBuffer {
public:
    void clear() { buf_.clear(); buf_.reserve(131072); }
    void flush() { fwrite(buf_.data(), 1, buf_.size(), stdout); fflush(stdout); }
    void append(const char* s) { buf_ += s; }
    void append(const std::string& s) { buf_ += s; }
    void appendf(const char* fmt, ...) { char tmp[512]; va_list a; va_start(a, fmt); vsnprintf(tmp, sizeof(tmp), fmt, a); va_end(a); buf_ += tmp; }
    void moveCursor(int row, int col) { appendf("\033[%d;%dH", row, col); }
    void clearScreen() { append("\033[2J\033[H"); }
    void hideCursor() { append("\033[?25l"); }
    void showCursor() { append("\033[?25h"); }
    void setBg(int r, int g, int b) { appendf("\033[48;2;%d;%d;%dm", r, g, b); }
    void setFg(int r, int g, int b) { appendf("\033[38;2;%d;%d;%dm", r, g, b); }
    void reset() { append("\033[0m"); }
private:
    std::string buf_;
};

inline void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
#endif