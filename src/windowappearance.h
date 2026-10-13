#pragma once

#include <QWidget>
#include <windows.h>
#include <dwmapi.h>

inline void useAirMirrorTitleBar(QWidget *widget) {
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    int dark = 1;
    if (DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)) != S_OK) {
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    }
    COLORREF caption = RGB(16, 21, 18);
    COLORREF text = RGB(244, 240, 232);
    DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, 36, &text, sizeof(text));
}
