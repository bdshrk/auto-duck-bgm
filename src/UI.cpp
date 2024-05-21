#include "UI.h"

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam,
                            LPARAM lParam) {
    switch (uMsg) {
    case WM_USER + 1:
        // when click on tray icon, open context menu
        switch (lParam) {
        case WM_LBUTTONDOWN:
            createContextMenu(hwnd);
            break;
        case WM_RBUTTONDOWN:
            createContextMenu(hwnd);
            break;
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    case WM_COMMAND:
        // menu item selected in context menu
        switch (LOWORD(wParam)) {
        case ID_TRAYMENU_OPEN_SETTINGS:
            Engine::get()->openSettingsINI();
            break;

        case ID_TRAYMENU_EXIT:
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DestroyIcon(nid.hIcon);
            PostQuitMessage(0);
            quit();
            break;

        case ID_TRAYMENU_RELOAD_SETTINGS:
            Engine::get()->readSettingsINI();
            break;

        case ID_TRAYMENU_TOGGLE:
            Engine::get()->setBypassed(!Engine::get()->getBypassed());
            createTrayIcon(hwnd);
            break;
        }
        break;

    case WM_QUERYENDSESSION:
        // windows is asking if this app can exit when shutdown
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) { // if user has shutdown/logged off of the computer
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DestroyIcon(nid.hIcon);
            PostQuitMessage(0);
            quit();
        }
        break;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void createTrayIcon(HWND hwnd) {
    // delete any existing icon and create a new one
    Shell_NotifyIcon(NIM_DELETE, &nid);

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;

    // choose the correct icon based on bypassed status
    nid.hIcon = LoadIcon(GetModuleHandle(NULL),
                         MAKEINTRESOURCE((!Engine::get()->getBypassed())
                                             ? IDI_ICON_DEFAULT
                                             : IDI_ICON_BYPASSED));
    nid.uCallbackMessage = WM_USER + 1;
    wcscpy_s(nid.szTip, PROG_BRAND_NAME);

    // add the icon to the tray area
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void createContextMenu(HWND hwnd) {
    // make the window foreground otherwise clicking away from the menu will not
    // close it.
    SetForegroundWindow(hwnd);

    HMENU hMenu = LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MENU1));
    HMENU hSubMenu = GetSubMenu(hMenu, 0);

    // set disabled/enabled text
    ModifyMenuW(hSubMenu, ID_TRAYMENU_TOGGLE, MF_BYCOMMAND | MF_STRING,
                ID_TRAYMENU_TOGGLE,
                (Engine::get()->getBypassed()) ? L"Enable" : L"Disable");

    // set status string
    ModifyMenuW(hSubMenu, ID_TRAYMENU_STATUSTEXT,
                MF_BYCOMMAND | MF_STRING | MF_DISABLED, ID_TRAYMENU_STATUSTEXT,
                Engine::get()->getShortStatusString().c_str());

    // show the menu at the appropriate point based on cursor pos
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON, pt.x, pt.y, 0,
                   hwnd, NULL);
}

void runUI() {
    // window class
    WNDCLASS wc = {0};
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = PROG_BRAND_NAME;
    RegisterClass(&wc);

    // create a MESSAGE ONLY window (not visible)
    // https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#message-only-windows
    hwnd = CreateWindow(wc.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE,
                        NULL, NULL, NULL);

    createTrayIcon(hwnd);

    // handle messages while a quit is not requested...
    MSG msg{};
    while (!quitRequested) {
        // cannot use GetMessage as it blocks the while loop so we cant quit
        // from other threads...
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep((DWORD)(UI_POLL_RATE_MS));
    }
}

void createErrorBox(std::wstring &errorString) {
    std::wstring errorStringFormatted = L"Fatal error:\n";
    errorStringFormatted += errorString;

    MessageBoxW(NULL, errorStringFormatted.c_str(), PROG_BRAND_NAME,
                MB_OK | MB_ICONERROR);

    // we should quit if an error has been encountered...
    quit();
}

int main() { return run(); }

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ LPWSTR lpCmdLine, _In_ int nShowCmd) {
    return run();
}

int run() {
    uiThread = std::thread(runUI);

    auto engine = Engine::get();
    if (engine->running())
        createErrorBox(engine->getErrorString());

    if (uiThread.joinable())
        uiThread.join();

    int error = (engine->hasError()) ? 1 : 0;
    engine = nullptr; // to call deconstructor

    return error;
}

void quit() {
    // technically, this can be called from multiple threads, but as the values
    // are only ever getting changed to true, there should be no need for mutex
    quitRequested = true;
    Engine::get()->requestQuit();
}
