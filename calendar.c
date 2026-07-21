// calendar.c - Win32 Calendar Day View
// Compile: gcc -o calendar.exe calendar.c -lgdi32 -luser32 -mwindows

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>   /* GET_X_LPARAM, GET_Y_LPARAM */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* --- Zoom & Canvas Configuration --- */
static float g_fZoom = 1.0f;
static int g_iCanvasWidth = 1000;
static int g_iCanvasHeight = 1440;
static const int g_iTimeColWidth = 65;
static int g_iScrollX = 0;
static int g_iScrollY = 300;
static int g_iClientW = 800;
static int g_iClientH = 600;

/* --- Event Data Structure --- */
typedef struct {
    char title[256];
    int startMinute;
    int durationMinutes;
    unsigned int color;
    int columnOffset;
} CalendarEvent;

#define MAX_EVENTS 100
static CalendarEvent g_aEvents[MAX_EVENTS];
static int g_eventCount = 0;

/* --- Interaction State --- */
static int g_dragMode = 0;
static int g_dragIndex = -1;
static int g_dragOffsetY = 0;
static int g_origStart = 0;
static int g_origDuration = 0;

/* --- Window Handles --- */
static HWND g_hGUI = NULL;
static HWND g_hBtnZoomIn = NULL;
static HWND g_hBtnZoomOut = NULL;
static HFONT g_hFontHour = NULL;
static HFONT g_hFontEvent = NULL;

/* ==============================================================================
   COLOR HELPERS
   ============================================================================== */

static COLORREF RGB2COLOR(unsigned int rgb) {
    unsigned char r = (rgb >> 16) & 0xFF;
    unsigned char g = (rgb >> 8) & 0xFF;
    unsigned char b = rgb & 0xFF;
    return RGB(r, g, b);
}

static unsigned int DarkenColor(unsigned int rgb, int percent) {
    unsigned char r = (unsigned char)(((rgb >> 16) & 0xFF) * (1 - percent / 100.0f));
    unsigned char g = (unsigned char)(((rgb >> 8) & 0xFF) * (1 - percent / 100.0f));
    unsigned char b = (unsigned char)((rgb & 0xFF) * (1 - percent / 100.0f));
    return (r << 16) | (g << 8) | b;
}

/* ==============================================================================
   EVENT MANAGEMENT
   ============================================================================== */

void AddEvent(const char* title, int startMin, int duration, unsigned int color, int colOffset) {
    if (g_eventCount >= MAX_EVENTS) return;
    strncpy(g_aEvents[g_eventCount].title, title, sizeof(g_aEvents[g_eventCount].title) - 1);
    g_aEvents[g_eventCount].title[sizeof(g_aEvents[g_eventCount].title) - 1] = '\0';
    g_aEvents[g_eventCount].startMinute = startMin;
    g_aEvents[g_eventCount].durationMinutes = duration;
    g_aEvents[g_eventCount].color = color;
    g_aEvents[g_eventCount].columnOffset = colOffset;
    g_eventCount++;
}

static void InitEvents(void) {
    AddEvent("Team Standup Meeting", 540, 45, 0x039BE5, 0);
    AddEvent("Product Design Review", 600, 90, 0x33B679, 0);
    AddEvent("Lunch with Sarah", 720, 60, 0xE67C73, 0);
    AddEvent("Deep Work: AutoIt GDI", 810, 120, 0x8E24AA, 0);
    AddEvent("Client Sync Call", 960, 45, 0xF4511E, 0);
}

/* ==============================================================================
   TIME CONVERSION
   ============================================================================== */

static void MinToTimeString(int minutes, char* buffer, size_t bufSize) {
    int h = minutes / 60;
    int m = minutes % 60;
    const char* apm = (h >= 12) ? "PM" : "AM";
    if (h > 12) h -= 12;
    if (h == 0) h = 12;
    snprintf(buffer, bufSize, "%d:%02d%s", h, m, apm);
}

/* ==============================================================================
   SCROLLBAR MANAGEMENT
   ============================================================================== */

static void UpdateScrollBars(void) {
    int iEffectiveWidth = (g_iClientW > g_iCanvasWidth) ? g_iClientW : g_iCanvasWidth;

    int maxScrollX = iEffectiveWidth - g_iClientW;
    if (maxScrollX < 0) maxScrollX = 0;
    if (g_iScrollX > maxScrollX) g_iScrollX = maxScrollX;
    if (g_iScrollX < 0) g_iScrollX = 0;

    int maxScrollY = g_iCanvasHeight - g_iClientH;
    if (maxScrollY < 0) maxScrollY = 0;
    if (g_iScrollY > maxScrollY) g_iScrollY = maxScrollY;
    if (g_iScrollY < 0) g_iScrollY = 0;

    SCROLLINFO siVert;
    siVert.cbSize = sizeof(siVert);
    siVert.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siVert.nMin = 0;
    siVert.nMax = g_iCanvasHeight;
    siVert.nPage = g_iClientH;
    siVert.nPos = g_iScrollY;
    siVert.nTrackPos = 0;
    SetScrollInfo(g_hGUI, SB_VERT, &siVert, TRUE);

    SCROLLINFO siHorz;
    siHorz.cbSize = sizeof(siHorz);
    siHorz.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    siHorz.nMin = 0;
    siHorz.nMax = iEffectiveWidth;
    siHorz.nPage = g_iClientW;
    siHorz.nPos = g_iScrollX;
    siHorz.nTrackPos = 0;
    SetScrollInfo(g_hGUI, SB_HORZ, &siHorz, TRUE);
}

static void SetZoom(float newZoom) {
    if (newZoom < 0.6f) newZoom = 0.6f;
    if (newZoom > 2.5f) newZoom = 2.5f;
    if (newZoom == g_fZoom) return;

    int centerMin = (g_iScrollY + (g_iClientH / 2)) / (int)g_fZoom;
    g_fZoom = newZoom;
    g_iCanvasHeight = (int)(1440 * g_fZoom);
    g_iScrollY = (int)((centerMin * g_fZoom) - (g_iClientH / 2));

    UpdateScrollBars();
    InvalidateRect(g_hGUI, NULL, FALSE);
}

/* ==============================================================================
   GDI DRAWING
   ============================================================================== */

static void DrawCalendar(HDC hdc) {
    int iEffectiveWidth = (g_iClientW > g_iCanvasWidth) ? g_iClientW : g_iCanvasWidth;

    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, g_iClientW, g_iClientH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, bitmap);

    /* 1. Background */
    HBRUSH bgBrush = CreateSolidBrush(RGB(255, 255, 255));
    RECT clientRect = {0, 0, g_iClientW, g_iClientH};
    FillRect(memDC, &clientRect, bgBrush);
    DeleteObject(bgBrush);

    /* 2. Setup */
    SetBkMode(memDC, TRANSPARENT);
    SelectObject(memDC, g_hFontHour);

    /* 3. Time Grid & Labels */
    HPEN penHour = CreatePen(PS_SOLID, 1, RGB(224, 224, 224));
    HPEN penHalf = CreatePen(PS_SOLID, 1, RGB(240, 240, 240));
    HPEN oldPen = (HPEN)SelectObject(memDC, penHour);

    for (int iHour = 0; iHour <= 24; iHour++) {
        int y = (int)((iHour * 60) * g_fZoom) - g_iScrollY;

        if (y >= -60 && y <= g_iClientH) {
            SelectObject(memDC, penHour);
            MoveToEx(memDC, g_iTimeColWidth - g_iScrollX, y, NULL);
            LineTo(memDC, iEffectiveWidth - g_iScrollX, y);

            if (iHour > 0 && iHour < 24) {
                char sTimeLabel[32];
                int hour12 = (iHour > 12 ? iHour - 12 : (iHour == 0 ? 12 : iHour));
                snprintf(sTimeLabel, sizeof(sTimeLabel), "%d %s", hour12, iHour >= 12 ? "PM" : "AM");
                if (iHour == 12) snprintf(sTimeLabel, sizeof(sTimeLabel), "12 PM");

                SetTextColor(memDC, RGB(112, 117, 122));
                RECT textRect = {0, y - 10, g_iTimeColWidth - 8 - g_iScrollX, y + 10};
                DrawTextA(memDC, sTimeLabel, -1, &textRect, DT_RIGHT | DT_TOP | DT_SINGLELINE);
            }

            if (iHour < 24) {
                int halfY = (int)((iHour * 60 + 30) * g_fZoom) - g_iScrollY;
                SelectObject(memDC, penHalf);
                MoveToEx(memDC, g_iTimeColWidth - g_iScrollX, halfY, NULL);
                LineTo(memDC, iEffectiveWidth - g_iScrollX, halfY);
            }
        }
    }

    /* Vertical divider */
    SelectObject(memDC, penHour);
    MoveToEx(memDC, g_iTimeColWidth - g_iScrollX, 0, NULL);
    LineTo(memDC, g_iTimeColWidth - g_iScrollX, g_iClientH);

    /* 4. Event Blocks */
    SelectObject(memDC, g_hFontEvent);

    for (int i = 0; i < g_eventCount; i++) {
        int startMin = g_aEvents[i].startMinute;
        int duration = g_aEvents[i].durationMinutes;
        unsigned int color = g_aEvents[i].color;

        int boxTop = (int)(startMin * g_fZoom) - g_iScrollY;
        int boxBottom = (int)((startMin + duration) * g_fZoom) - g_iScrollY;
        int boxLeft = g_iTimeColWidth + 8 - g_iScrollX + (g_aEvents[i].columnOffset * 200);
        int boxRight = iEffectiveWidth - 15 - g_iScrollX;

        if (boxBottom > 0 && boxTop < g_iClientH) {
            HBRUSH brushEvent = CreateSolidBrush(RGB2COLOR(color));
            HPEN penEvent = CreatePen(PS_SOLID, 1, RGB2COLOR(DarkenColor(color, 20)));

            SelectObject(memDC, brushEvent);
            SelectObject(memDC, penEvent);

            RoundRect(memDC, boxLeft, boxTop, boxRight, boxBottom, 8, 8);

            SetTextColor(memDC, RGB(255, 255, 255));
            int boxHeight = boxBottom - boxTop;
            RECT textRect = {boxLeft + 8, boxTop + 4, boxRight - 12, boxBottom - 6};

            char sEventTime[64];
            char startTimeStr[32], endTimeStr[32];
            MinToTimeString(startMin, startTimeStr, sizeof(startTimeStr));
            MinToTimeString(startMin + duration, endTimeStr, sizeof(endTimeStr));
            snprintf(sEventTime, sizeof(sEventTime), "%s - %s", startTimeStr, endTimeStr);

            if (boxHeight > 18) {
                DrawTextA(memDC, g_aEvents[i].title, -1, &textRect,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
                textRect.top += 16;
                SetTextColor(memDC, RGB(240, 240, 240));
                DrawTextA(memDC, sEventTime, -1, &textRect,
                          DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            }

            if (boxHeight > 24) {
                HPEN penGrip = CreatePen(PS_SOLID, 1, RGB2COLOR(DarkenColor(color, 35)));
                SelectObject(memDC, penGrip);
                int midX = boxLeft + ((boxRight - boxLeft) / 2);

                MoveToEx(memDC, midX - 12, boxTop + 3, NULL);
                LineTo(memDC, midX + 12, boxTop + 3);
                MoveToEx(memDC, midX - 12, boxBottom - 3, NULL);
                LineTo(memDC, midX + 12, boxBottom - 3);

                DeleteObject(penGrip);
            }

            DeleteObject(brushEvent);
            DeleteObject(penEvent);
        }
    }

    /* 5. Current Time Line */
    SYSTEMTIME st;
    GetLocalTime(&st);
    int currentMin = (st.wHour * 60) + st.wMinute;
    int nowY = (int)(currentMin * g_fZoom) - g_iScrollY;

    if (nowY >= 0 && nowY <= g_iClientH) {
        HPEN penRed = CreatePen(PS_SOLID, 2, RGB(234, 67, 53));
        HBRUSH brushRed = CreateSolidBrush(RGB(234, 67, 53));

        SelectObject(memDC, penRed);
        SelectObject(memDC, brushRed);

        MoveToEx(memDC, g_iTimeColWidth - g_iScrollX, nowY, NULL);
        LineTo(memDC, iEffectiveWidth - g_iScrollX, nowY);
        Ellipse(memDC, g_iTimeColWidth - 5 - g_iScrollX, nowY - 5,
                g_iTimeColWidth + 5 - g_iScrollX, nowY + 5);

        DeleteObject(penRed);
        DeleteObject(brushRed);
    }

    /* 6. Blit & Cleanup */
    BitBlt(hdc, 0, 0, g_iClientW, g_iClientH, memDC, 0, 0, SRCCOPY);

    SelectObject(memDC, oldBmp);
    SelectObject(memDC, oldPen);
    DeleteObject(bitmap);
    DeleteObject(penHour);
    DeleteObject(penHalf);
    DeleteDC(memDC);
}

/* ==============================================================================
   WINDOW PROCEDURE
   ============================================================================== */

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            g_hGUI = hWnd;
            g_hBtnZoomIn = CreateWindowA("BUTTON", "+",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                8, 8, 24, 24, hWnd, (HMENU)1, NULL, NULL);
            g_hBtnZoomOut = CreateWindowA("BUTTON", "-",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                35, 8, 24, 24, hWnd, (HMENU)2, NULL, NULL);

            {
                HFONT hFontBtn = CreateFontA(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
                SendMessageA(g_hBtnZoomIn, WM_SETFONT, (WPARAM)hFontBtn, TRUE);
                SendMessageA(g_hBtnZoomOut, WM_SETFONT, (WPARAM)hFontBtn, TRUE);

                g_hFontHour = CreateFontA(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
                g_hFontEvent = CreateFontA(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
            }

            InitEvents();
            UpdateScrollBars();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            DrawCalendar(hdc);
            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_SIZE:
            g_iClientW = LOWORD(lParam);
            g_iClientH = HIWORD(lParam);
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_iScrollY -= (delta > 0 ? 60 : -60);
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_VSCROLL: {
            int req = LOWORD(wParam);
            switch (req) {
                case SB_LINEUP: g_iScrollY -= 30; break;
                case SB_LINEDOWN: g_iScrollY += 30; break;
                case SB_PAGEUP: g_iScrollY -= g_iClientH; break;
                case SB_PAGEDOWN: g_iScrollY += g_iClientH; break;
                case SB_THUMBTRACK: {
                    SCROLLINFO si;
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(hWnd, SB_VERT, &si);
                    g_iScrollY = si.nTrackPos;
                    break;
                }
            }
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_HSCROLL: {
            int req = LOWORD(wParam);
            switch (req) {
                case SB_LINELEFT: g_iScrollX -= 30; break;
                case SB_LINERIGHT: g_iScrollX += 30; break;
                case SB_PAGELEFT: g_iScrollX -= g_iClientW; break;
                case SB_PAGERIGHT: g_iScrollX += g_iClientW; break;
                case SB_THUMBTRACK: {
                    SCROLLINFO si;
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(hWnd, SB_HORZ, &si);
                    g_iScrollX = si.nTrackPos;
                    break;
                }
            }
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, FALSE);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == 1)
                SetZoom(g_fZoom + 0.2f);
            else if (LOWORD(wParam) == 2)
                SetZoom(g_fZoom - 0.2f);
            return 0;

        case WM_LBUTTONDOWN: {
            int mouseX = GET_X_LPARAM(lParam) + g_iScrollX;
            int mouseY = GET_Y_LPARAM(lParam) + g_iScrollY;
            int iEffectiveWidth = (g_iClientW > g_iCanvasWidth) ? g_iClientW : g_iCanvasWidth;

            for (int i = g_eventCount - 1; i >= 0; i--) {
                int top = (int)(g_aEvents[i].startMinute * g_fZoom);
                int bottom = (int)((g_aEvents[i].startMinute + g_aEvents[i].durationMinutes) * g_fZoom);
                int left = g_iTimeColWidth + 8 + (g_aEvents[i].columnOffset * 200);
                int right = iEffectiveWidth - 15;

                if (mouseX >= left && mouseX <= right && mouseY >= top && mouseY <= bottom) {
                    g_dragIndex = i;
                    g_origStart = g_aEvents[i].startMinute;
                    g_origDuration = g_aEvents[i].durationMinutes;

                    if ((mouseY - top) <= 6)
                        g_dragMode = 2;
                    else if ((bottom - mouseY) <= 6)
                        g_dragMode = 3;
                    else {
                        g_dragMode = 1;
                        g_dragOffsetY = mouseY - top;
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mouseX = GET_X_LPARAM(lParam) + g_iScrollX;
            int mouseY = GET_Y_LPARAM(lParam) + g_iScrollY;
            int iEffectiveWidth = (g_iClientW > g_iCanvasWidth) ? g_iClientW : g_iCanvasWidth;

            if (g_dragMode > 0 && g_dragIndex != -1) {
                int currentMin = (int)(mouseY / g_fZoom);
                currentMin = (int)(round(currentMin / 15.0)) * 15;

                switch (g_dragMode) {
                    case 1: {
                        int newStart = (int)(round((mouseY - g_dragOffsetY) / g_fZoom / 15.0)) * 15;
                        if (newStart < 0) newStart = 0;
                        if ((newStart + g_aEvents[g_dragIndex].durationMinutes) > 1440)
                            newStart = 1440 - g_aEvents[g_dragIndex].durationMinutes;
                        g_aEvents[g_dragIndex].startMinute = newStart;
                        break;
                    }
                    case 2: {
                        if (currentMin < 0) currentMin = 0;
                        int endMin = g_origStart + g_origDuration;
                        if ((endMin - currentMin) >= 15) {
                            g_aEvents[g_dragIndex].startMinute = currentMin;
                            g_aEvents[g_dragIndex].durationMinutes = endMin - currentMin;
                        }
                        break;
                    }
                    case 3: {
                        if (currentMin > 1440) currentMin = 1440;
                        if ((currentMin - g_origStart) >= 15)
                            g_aEvents[g_dragIndex].durationMinutes = currentMin - g_origStart;
                        break;
                    }
                }
                InvalidateRect(hWnd, NULL, FALSE);
                return 0;
            }

            BOOL onHandle = FALSE;
            for (int i = g_eventCount - 1; i >= 0; i--) {
                int top = (int)(g_aEvents[i].startMinute * g_fZoom);
                int bottom = (int)((g_aEvents[i].startMinute + g_aEvents[i].durationMinutes) * g_fZoom);
                int left = g_iTimeColWidth + 8 + (g_aEvents[i].columnOffset * 200);
                int right = iEffectiveWidth - 15;

                if (mouseX >= left && mouseX <= right && mouseY >= top && mouseY <= bottom) {
                    if ((mouseY - top) <= 6 || (bottom - mouseY) <= 6) {
                        SetCursor(LoadCursor(NULL, IDC_SIZENS));
                        onHandle = TRUE;
                    }
                    break;
                }
            }
            if (!onHandle) SetCursor(LoadCursor(NULL, IDC_ARROW));
            return 0;
        }

        case WM_LBUTTONUP:
            g_dragMode = 0;
            g_dragIndex = -1;
            return 0;

        case WM_LBUTTONDBLCLK: {
            int mouseX = GET_X_LPARAM(lParam) + g_iScrollX;
            int mouseY = GET_Y_LPARAM(lParam) + g_iScrollY;

            if (mouseX > g_iTimeColWidth) {
                int startMin = (int)(round(mouseY / g_fZoom / 30.0)) * 30;
                if (startMin > (1440 - 60)) startMin = 1440 - 60;

                unsigned int colors[] = {0x039BE5, 0x33B679, 0x8E24AA, 0xF4511E, 0xE67C73};
                unsigned int color = colors[g_eventCount % 5];

                char title[32];
                snprintf(title, sizeof(title), "New Event %d", g_eventCount + 1);
                AddEvent(title, startMin, 60, color, 0);
                InvalidateRect(hWnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_DESTROY:
            DeleteObject(g_hFontHour);
            DeleteObject(g_hFontEvent);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

/* ==============================================================================
   MAIN ENTRY POINT
   ============================================================================== */

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "CalendarClass";
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Failed to register window class!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_hGUI = CreateWindowExA(
        WS_EX_COMPOSITED,
        "CalendarClass",
        "Calendar Day View (Win32 C)",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        NULL, NULL, hInstance, NULL);

    if (!g_hGUI) {
        MessageBoxA(NULL, "Failed to create window!", "Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hGUI, nShowCmd);
    UpdateWindow(g_hGUI);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}