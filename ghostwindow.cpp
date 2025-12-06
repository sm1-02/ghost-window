#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
HINSTANCE g_hInst        = nullptr;
HWND      g_hMainWnd     = nullptr;
HHOOK     g_hMouseHook   = nullptr;
HHOOK     g_hKeyHook     = nullptr;

HWND  g_hLastTarget      = nullptr;
LONG  g_originalExStyle  = 0;
bool  g_hasOriginalStyle = false;
bool  g_targetLocked     = false;

bool  g_transparentOn    = false;
bool  g_topmostOn        = false;

const UINT WM_TRAYICON     = WM_APP + 1;
const UINT ID_TRAY_RESTORE = 1001;
const UINT ID_TRAY_EXIT    = 1002;

const BYTE ALPHA_MIN       = 1;          // almost invisible
const BYTE ALPHA_MAX       = 255;         // fully opaque
const BYTE ALPHA_DEFAULT   = 26;          // ~10% visible (255 * 0.1)
BYTE       g_currentAlpha  = ALPHA_DEFAULT;

// -----------------------------------------------------------------------------
// Utility
// -----------------------------------------------------------------------------
void RemoveHooks()
{
    if (g_hMouseHook)
    {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = nullptr;
    }
    if (g_hKeyHook)
    {
        UnhookWindowsHookEx(g_hKeyHook);
        g_hKeyHook = nullptr;
    }
}

void AddTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    lstrcpyW(nid.szTip, L"Ghost Window");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hWnd)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hWnd;
    nid.uID = 1;
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// -----------------------------------------------------------------------------
// Style / transparency application
// -----------------------------------------------------------------------------
void ApplyWindowStyles()
{
    if (!IsWindow(g_hLastTarget) || !g_hasOriginalStyle)
        return;

    HWND h = g_hLastTarget;

    LONG exStyle = g_originalExStyle;

    if (g_transparentOn)
    {
        exStyle |= WS_EX_LAYERED | WS_EX_TOOLWINDOW;
        exStyle &= ~WS_EX_APPWINDOW;
    }

    SetWindowLong(h, GWL_EXSTYLE, exStyle);

    BYTE alpha = g_transparentOn ? g_currentAlpha : ALPHA_MAX;

    // If WS_EX_LAYERED is present, this sets alpha; otherwise it is harmless.
    SetLayeredWindowAttributes(h, 0, alpha, LWA_ALPHA);

    SetWindowPos(
        h,
        nullptr,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED
    );

    // Topmost / normal
    SetWindowPos(
        h,
        g_topmostOn ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE
    );
}

void UpdateTransparencyByDelta(int delta)
{
    if (!IsWindow(g_hLastTarget) || !g_transparentOn)
        return;

    int a = (int)g_currentAlpha + delta;
    if (a < (int)ALPHA_MIN) a = ALPHA_MIN;
    if (a > (int)ALPHA_MAX) a = ALPHA_MAX;

    g_currentAlpha = (BYTE)a;
    ApplyWindowStyles();
}

// First-time selection of target window.
// After the first selection, g_targetLocked is true and we never change target.
bool EnsureTargetSelected()
{
    if (g_targetLocked)
    {
        if (IsWindow(g_hLastTarget))
            return true;
        // Target was closed; do not pick a new one.
        return false;
    }

    HWND fg = GetForegroundWindow();
    if (!fg)
        return false;

    HWND top = GetAncestor(fg, GA_ROOT);
    if (!top || top == g_hMainWnd)
        return false;

    g_hLastTarget      = top;
    g_originalExStyle  = GetWindowLong(top, GWL_EXSTYLE);
    g_hasOriginalStyle = true;
    g_targetLocked     = true;

    g_currentAlpha     = ALPHA_DEFAULT;
    g_transparentOn    = false;
    g_topmostOn        = false;

    return true;
}

void ToggleTransparency()
{
    if (!EnsureTargetSelected())
        return;

    if (!g_transparentOn && g_currentAlpha == ALPHA_MAX)
        g_currentAlpha = ALPHA_DEFAULT;

    g_transparentOn = !g_transparentOn;
    ApplyWindowStyles();
}

void ToggleTopmost()
{
    if (!EnsureTargetSelected())
        return;

    g_topmostOn = !g_topmostOn;
    ApplyWindowStyles();
}

void ToggleMinimizeForCurrent()
{
    if (!EnsureTargetSelected())
        return;

    WINDOWPLACEMENT wp = { sizeof(WINDOWPLACEMENT) };
    if (!GetWindowPlacement(g_hLastTarget, &wp))
        return;

    if (wp.showCmd == SW_SHOWMINIMIZED)
        ShowWindow(g_hLastTarget, SW_RESTORE);
    else
        ShowWindow(g_hLastTarget, SW_MINIMIZE);
}

void RestoreLastWindow()
{
    if (!IsWindow(g_hLastTarget) || !g_hasOriginalStyle)
        return;

    g_transparentOn = false;
    g_topmostOn     = false;
    g_currentAlpha  = ALPHA_MAX;

    ApplyWindowStyles();

    // Target remains locked; future hotkeys still refer to this window,
    // but it is now in normal style / opacity unless modified again.
}

// -----------------------------------------------------------------------------
// Hooks
// -----------------------------------------------------------------------------
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (altDown && wParam == WM_MOUSEWHEEL && IsWindow(g_hLastTarget) && g_transparentOn)
        {
            MSLLHOOKSTRUCT *p = (MSLLHOOKSTRUCT*)lParam;
            short delta = (short)HIWORD(p->mouseData);

            // Wheel up → more opaque, wheel down → more transparent
            if (delta > 0)
                UpdateTransparencyByDelta(+2);
            else if (delta < 0)
                UpdateTransparencyByDelta(-2);

            return 1; // swallow event
        }
    }

    return CallNextHookEx(g_hMouseHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *p = (KBDLLHOOKSTRUCT*)lParam;
        bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        if (altDown && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
        {
            if (p->vkCode == 'A')
            {
                // Alt + A: toggle transparency of the (first) target window
                ToggleTransparency();
                return 1;
            }
            if (p->vkCode == 'T')
            {
                // Alt + T: toggle always-on-top
                ToggleTopmost();
                return 1;
            }
            if (p->vkCode == 'X')
            {
                // Alt + X: toggle minimize / restore
                ToggleMinimizeForCurrent();
                return 1;
            }
            if (p->vkCode == 'Q')
            {
                // Alt + Q: quit app
                PostMessage(g_hMainWnd, WM_CLOSE, 0, 0);
                return 1;
            }
        }
    }

    return CallNextHookEx(g_hKeyHook, nCode, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Window procedure
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        AddTrayIcon(hWnd);
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONDBLCLK)
        {
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu)
            {
                AppendMenu(
                    hMenu,
                    MF_STRING | (g_hLastTarget && g_hasOriginalStyle ? 0 : MF_GRAYED),
                    ID_TRAY_RESTORE,
                    L"Restore window"
                );
                AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

                SetForegroundWindow(hWnd);
                TrackPopupMenu(
                    hMenu,
                    TPM_RIGHTBUTTON,
                    pt.x, pt.y,
                    0,
                    hWnd,
                    nullptr
                );
                DestroyMenu(hMenu);
            }
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_RESTORE:
            RestoreLastWindow();
            break;
        case ID_TRAY_EXIT:
            DestroyWindow(hWnd);
            break;
        }
        return 0;

    case WM_DESTROY:
        RemoveHooks();
        RemoveTrayIcon(hWnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// WinMain
// -----------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    g_hInst = hInstance;

    // Register invisible window class
    WNDCLASS wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = L"GhostWindowHiddenClass";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);

    if (!RegisterClass(&wc))
        return 1;

    // Create hidden window (no taskbar button)
    g_hMainWnd = CreateWindowEx(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"GhostWindowHiddenWindow",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_hMainWnd)
        return 1;

    // Install global hooks
    g_hMouseHook = SetWindowsHookEx(
        WH_MOUSE_LL,
        LowLevelMouseProc,
        hInstance,
        0
    );

    g_hKeyHook = SetWindowsHookEx(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        hInstance,
        0
    );

    if (!g_hMouseHook || !g_hKeyHook)
    {
        RemoveHooks();
        DestroyWindow(g_hMainWnd);
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
