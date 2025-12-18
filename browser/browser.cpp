#include <windows.h>
#include <vector>
#include <string>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Msimg32.lib") // For GradientFill if needed

using namespace Microsoft::WRL;

// --- UI CONSTANTS ---
const int IDC_ADDRESS_BAR = 101;
const int IDC_SIDEBAR_BTN = 102;
const int IDC_NEW_TAB_BTN = 104;
const int IDC_CLEAR_HISTORY_BTN = 105;
const int IDC_TOGGLE_VIEW_BTN = 106;
const int IDC_EXPAND_SIDEBAR = 107;
const int IDC_WIN_MIN = 108;
const int IDC_WIN_MAX = 110;
const int IDC_WIN_CLOSE = 109;

const int HEADER_TOTAL_HEIGHT = 100;
const int SIDEBAR_MIN_WIDTH = 260;
const int SIDEBAR_MAX_WIDTH = 450;
const int TAB_WIDTH = 200;
const int TAB_HEIGHT = 34;
const int TAB_CLOSE_WIDTH = 32;

struct BrowserTab {
    wil::com_ptr<ICoreWebView2Controller> controller;
    wil::com_ptr<ICoreWebView2> webview;
    std::wstring title = L"New Tab";
};

// --- GLOBAL STATE ---
std::vector<BrowserTab> tabs;
std::vector<std::wstring> historyList;
int activeTabIndex = -1;
int currentSidebarWidth = SIDEBAR_MIN_WIDTH;
HWND hEdit, hSidebarBtn, hNewTabBtn, hClearBtn, hToggleViewBtn, hExpandBtn, hBtnMin, hBtnMax, hBtnClose;
WNDPROC OldEditProc;
HFONT hFontMain, hFontSmall;
bool isSidebarOpen = true;
bool isExtrasView = false;
bool isExpanded = false;
bool isVideoFullScreen = false;

// --- REFINED COLOR PALETTE ---
COLORREF colBgHeader = RGB(24, 24, 28);
COLORREF colBgSidebar = RGB(18, 18, 20);
COLORREF colTabActive = RGB(38, 38, 42);
COLORREF colTabInactive = RGB(28, 28, 32);
COLORREF colAccent = RGB(0, 120, 215);
COLORREF colTextMain = RGB(240, 240, 240);
COLORREF colTextDim = RGB(160, 160, 160);

// --- FORWARD DECLARATIONS ---
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
void CreateNewTab(HWND hWnd);
void SwitchToTab(int index, HWND hWnd);
void CloseTab(int index, HWND hWnd);
void UpdateLayout(HWND hWnd);

// --- UTILITY ---
void SyncAddressBar() {
    if (activeTabIndex != -1 && activeTabIndex < (int)tabs.size() && tabs[activeTabIndex].webview) {
        wil::unique_cotaskmem_string url;
        tabs[activeTabIndex].webview->get_Source(&url);
        SetWindowText(hEdit, url.get());
    }
}

void ToggleUIElements(bool show) {
    int mainCmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(hEdit, mainCmd);
    ShowWindow(hSidebarBtn, mainCmd);
    ShowWindow(hNewTabBtn, mainCmd);
    ShowWindow(hBtnMin, mainCmd);
    ShowWindow(hBtnMax, mainCmd);
    ShowWindow(hBtnClose, mainCmd);

    int sidebarControlsCmd = (show && isSidebarOpen) ? SW_SHOW : SW_HIDE;
    ShowWindow(hToggleViewBtn, sidebarControlsCmd);
    ShowWindow(hExpandBtn, sidebarControlsCmd);
}

void UpdateLayout(HWND hWnd) {
    if (activeTabIndex == -1 || activeTabIndex >= (int)tabs.size()) return;
    RECT rc;
    GetClientRect(hWnd, &rc);

    if (isVideoFullScreen) {
        tabs[activeTabIndex].controller->put_Bounds({ 0, 0, rc.right, rc.bottom });
        ToggleUIElements(false);
    }
    else {
        int xOff = isSidebarOpen ? currentSidebarWidth : 0;
        RECT wR = { xOff, HEADER_TOTAL_HEIGHT, rc.right, rc.bottom };
        tabs[activeTabIndex].controller->put_Bounds(wR);

        // Header Buttons (Window Controls)
        MoveWindow(hBtnClose, rc.right - 46, 0, 46, 32, TRUE);
        MoveWindow(hBtnMax, rc.right - 92, 0, 46, 32, TRUE);
        MoveWindow(hBtnMin, rc.right - 138, 0, 46, 32, TRUE);

        // Sidebar Toggle
        MoveWindow(hSidebarBtn, 10, 10, 40, 40, TRUE);

        // Address Bar (Centered & Sized)
        int editW = min(rc.right - 400, 700);
        int editX = (rc.right - editW) / 2;
        MoveWindow(hEdit, editX, 15, editW, 30, TRUE);

        ToggleUIElements(true);
    }
}

LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static bool isPlaceholder = true;
    if (msg == WM_SETFOCUS) {
        if (isPlaceholder) { SetWindowText(hWnd, L""); isPlaceholder = false; }
    }
    else if (msg == WM_KILLFOCUS) {
        wchar_t buf[2];
        if (GetWindowText(hWnd, buf, 2) == 0) { SetWindowText(hWnd, L"Search or enter URL..."); isPlaceholder = true; }
    }
    else if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        wchar_t url[2048]; GetWindowText(hWnd, url, 2048);
        std::wstring urlStr(url);
        if (urlStr.find(L"://") == std::wstring::npos) urlStr = L"https://www.google.com/search?q=" + urlStr;
        if (activeTabIndex != -1 && tabs[activeTabIndex].webview) tabs[activeTabIndex].webview->Navigate(urlStr.c_str());
        return 0;
    }
    return CallWindowProc(OldEditProc, hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    // Enable Visual Styles
    hFontMain = CreateFont(19, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    hFontSmall = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, NULL,
                        LoadCursor(NULL, IDC_ARROW), CreateSolidBrush(colBgHeader), NULL, L"SARF_CORE", NULL };
    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindowEx(0, L"SARF_CORE", L"SARF Browser", WS_POPUP | WS_VISIBLE | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, NULL, NULL, hInstance, NULL);

    // Initial controls creation
    hSidebarBtn = CreateWindow(L"BUTTON", L"☰", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0, hWnd, (HMENU)IDC_SIDEBAR_BTN, hInstance, NULL);
    hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"Search or enter URL...", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_ADDRESS_BAR, hInstance, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMain, TRUE);
    OldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    hNewTabBtn = CreateWindow(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0, hWnd, (HMENU)IDC_NEW_TAB_BTN, hInstance, NULL);
    hBtnClose = CreateWindow(L"BUTTON", L"✕", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_CLOSE, hInstance, NULL);
    hBtnMax = CreateWindow(L"BUTTON", L"▢", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_MAX, hInstance, NULL);
    hBtnMin = CreateWindow(L"BUTTON", L"—", WS_CHILD | WS_VISIBLE | BS_FLAT, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_MIN, hInstance, NULL);

    CreateNewTab(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hToggleViewBtn = CreateWindow(L"BUTTON", L"›", WS_CHILD | WS_VISIBLE | BS_FLAT, SIDEBAR_MIN_WIDTH - 40, HEADER_TOTAL_HEIGHT + 10, 30, 30, hWnd, (HMENU)IDC_TOGGLE_VIEW_BTN, NULL, NULL);
        hExpandBtn = CreateWindow(L"BUTTON", L"RESIZE", WS_CHILD | WS_VISIBLE | BS_FLAT, 10, HEADER_TOTAL_HEIGHT + 10, 70, 26, hWnd, (HMENU)IDC_EXPAND_SIDEBAR, NULL, NULL);
    } break;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkColor(hdc, RGB(45, 45, 48));
        return (LRESULT)CreateSolidBrush(RGB(45, 45, 48));
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        if (isVideoFullScreen) { EndPaint(hWnd, &ps); return 0; }

        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH hHeaderBrush = CreateSolidBrush(colBgHeader);
        RECT headerRect = { 0, 0, rc.right, HEADER_TOTAL_HEIGHT };
        FillRect(hdc, &headerRect, hHeaderBrush);
        DeleteObject(hHeaderBrush);

        SetBkMode(hdc, TRANSPARENT);

        if (isSidebarOpen) {
            RECT sideRect = { 0, HEADER_TOTAL_HEIGHT, currentSidebarWidth, rc.bottom };
            HBRUSH hSide = CreateSolidBrush(colBgSidebar);
            FillRect(hdc, &sideRect, hSide);
            DeleteObject(hSide);

            SelectObject(hdc, hFontMain);
            SetTextColor(hdc, colAccent);
            if (!isExtrasView) {
                TextOut(hdc, 20, HEADER_TOTAL_HEIGHT + 50, L"HISTORY", 7);
                int hy = HEADER_TOTAL_HEIGHT + 85;
                SelectObject(hdc, hFontSmall);
                SetTextColor(hdc, colTextDim);
                for (const auto& url : historyList) {
                    if (hy > rc.bottom - 30) break;
                    RECT hr = { 20, hy, currentSidebarWidth - 20, hy + 25 };
                    DrawText(hdc, url.c_str(), -1, &hr, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    hy += 30;
                }
            }
        }

        // DRAW TABS
        int tx = 10;
        SelectObject(hdc, hFontSmall);
        for (int i = 0; i < (int)tabs.size(); i++) {
            RECT tR = { tx, 66, tx + TAB_WIDTH, 66 + TAB_HEIGHT };
            bool isActive = (i == activeTabIndex);

            HBRUSH hTb = CreateSolidBrush(isActive ? colTabActive : colTabInactive);
            FillRect(hdc, &tR, hTb);
            DeleteObject(hTb);

            // Active Tab Accent
            if (isActive) {
                RECT accentR = { tR.left, tR.bottom - 3, tR.right, tR.bottom };
                HBRUSH hAccent = CreateSolidBrush(colAccent);
                FillRect(hdc, &accentR, hAccent);
                DeleteObject(hAccent);
            }

            SetTextColor(hdc, isActive ? colTextMain : colTextDim);
            RECT textR = { tR.left + 12, tR.top, tR.right - TAB_CLOSE_WIDTH, tR.bottom };
            DrawText(hdc, tabs[i].title.c_str(), -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            RECT closeR = { tR.right - TAB_CLOSE_WIDTH, tR.top, tR.right, tR.bottom };
            DrawText(hdc, L"✕", 1, &closeR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            tx += TAB_WIDTH + 4;
        }
        MoveWindow(hNewTabBtn, tx, 69, 28, 28, TRUE);
        EndPaint(hWnd, &ps);
    } break;

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam), y = HIWORD(lParam);
        if (y >= 66 && y <= 100) {
            int tx = 10;
            for (int i = 0; i < (int)tabs.size(); i++) {
                if (x >= tx && x <= tx + TAB_WIDTH) {
                    if (x >= (tx + TAB_WIDTH - TAB_CLOSE_WIDTH)) CloseTab(i, hWnd);
                    else SwitchToTab(i, hWnd);
                    return 0;
                }
                tx += TAB_WIDTH + 4;
            }
        }
    } break;

    case WM_SIZE: UpdateLayout(hWnd); break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_WIN_CLOSE: PostQuitMessage(0); break;
        case IDC_WIN_MIN: ShowWindow(hWnd, SW_MINIMIZE); break;
        case IDC_WIN_MAX: IsZoomed(hWnd) ? ShowWindow(hWnd, SW_RESTORE) : ShowWindow(hWnd, SW_MAXIMIZE); break;
        case IDC_SIDEBAR_BTN: isSidebarOpen = !isSidebarOpen; UpdateLayout(hWnd); InvalidateRect(hWnd, NULL, TRUE); break;
        case IDC_NEW_TAB_BTN: CreateNewTab(hWnd); break;
        case IDC_TOGGLE_VIEW_BTN: isExtrasView = !isExtrasView; InvalidateRect(hWnd, NULL, TRUE); break;
        case IDC_EXPAND_SIDEBAR:
            isExpanded = !isExpanded;
            currentSidebarWidth = isExpanded ? SIDEBAR_MAX_WIDTH : SIDEBAR_MIN_WIDTH;
            UpdateLayout(hWnd);
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }
        break;

    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        if (isVideoFullScreen) return HTCLIENT;
        RECT winRect; GetClientRect(hWnd, &winRect);

        // Window Controls
        if (pt.y < 32 && pt.x >(winRect.right - 140)) return HTCLIENT;
        // Sidebar Btn
        if (pt.y >= 10 && pt.y <= 50 && pt.x >= 10 && pt.x <= 50) return HTCLIENT;
        // Address Bar
        int editW = min(winRect.right - 400, 700);
        int editX = (winRect.right - editW) / 2;
        if (pt.y >= 15 && pt.y <= 45 && pt.x >= editX && pt.x <= (editX + editW)) return HTCLIENT;
        // Tabs
        if (pt.y >= 66 && pt.y <= 100) return HTCLIENT;

        if (pt.y < HEADER_TOTAL_HEIGHT) return HTCAPTION;
        return DefWindowProc(hWnd, msg, wParam, lParam);
    } break;

    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// Logic for creating tabs, switching, and closing remains functional as per original.
void CreateNewTab(HWND hWnd) {
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                if (!env) return res;
                env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hWnd](HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (ctrl) {
                            BrowserTab nt;
                            nt.controller = ctrl;
                            nt.controller->get_CoreWebView2(&nt.webview);

                            nt.webview->add_ContainsFullScreenElementChanged(Callback<ICoreWebView2ContainsFullScreenElementChangedEventHandler>(
                                [hWnd](ICoreWebView2* sender, IUnknown* args) -> HRESULT {
                                    BOOL fs;
                                    sender->get_ContainsFullScreenElement(&fs);
                                    isVideoFullScreen = fs;
                                    UpdateLayout(hWnd);
                                    InvalidateRect(hWnd, NULL, TRUE);
                                    return S_OK;
                                }).Get(), nullptr);

                            nt.webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                                [hWnd](ICoreWebView2* s, ICoreWebView2SourceChangedEventArgs* a) -> HRESULT {
                                    wil::unique_cotaskmem_string url;
                                    s->get_Source(&url);
                                    historyList.insert(historyList.begin(), std::wstring(url.get()));
                                    if (historyList.size() > 25) historyList.pop_back();
                                    SyncAddressBar();
                                    InvalidateRect(hWnd, NULL, TRUE);
                                    return S_OK;
                                }).Get(), nullptr);

                            nt.webview->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>(
                                [hWnd](ICoreWebView2* s, IUnknown* a) -> HRESULT {
                                    wil::unique_cotaskmem_string t; s->get_DocumentTitle(&t);
                                    for (auto& tab : tabs) if (tab.webview.get() == s) tab.title = t.get();
                                    InvalidateRect(hWnd, NULL, TRUE); return S_OK;
                                }).Get(), nullptr);

                            tabs.push_back(nt);
                            SwitchToTab((int)tabs.size() - 1, hWnd);
                            nt.webview->Navigate(L"https://www.google.com");
                        }
                        return S_OK;
                    }).Get());
                return S_OK;
            }).Get());
}

void SwitchToTab(int index, HWND hWnd) {
    if (index < 0 || index >= (int)tabs.size()) return;
    activeTabIndex = index;
    for (int i = 0; i < (int)tabs.size(); i++) {
        if (i == activeTabIndex) {
            BOOL fs;
            tabs[i].webview->get_ContainsFullScreenElement(&fs);
            isVideoFullScreen = fs;
            UpdateLayout(hWnd);
            tabs[i].controller->put_IsVisible(TRUE);
        }
        else {
            tabs[i].controller->put_IsVisible(FALSE);
        }
    }
    SyncAddressBar();
    InvalidateRect(hWnd, NULL, TRUE);
}

void CloseTab(int index, HWND hWnd) {
    if (index < 0 || index >= (int)tabs.size()) return;
    tabs[index].controller->Close();
    tabs.erase(tabs.begin() + index);
    if (tabs.empty()) PostQuitMessage(0);
    else {
        if (activeTabIndex >= (int)tabs.size()) activeTabIndex = (int)tabs.size() - 1;
        SwitchToTab(activeTabIndex, hWnd);
    }
}