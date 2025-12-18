#include <windows.h>
#include <vector>
#include <string>
#include <wrl.h>
#include <wil/com.h>
#include "WebView2.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "Msimg32.lib")

using namespace Microsoft::WRL;

// --- UI CONSTANTS ---
const int IDC_ADDRESS_BAR = 101;
const int IDC_SIDEBAR_BTN = 102;
const int IDC_NEW_TAB_BTN = 104;
const int IDC_CLEAR_HISTORY_BTN = 105;
const int IDC_SETTINGS_BTN = 106;
const int IDC_EXPAND_SIDEBAR = 107;
const int IDC_WIN_MIN = 108;
const int IDC_WIN_MAX = 110;
const int IDC_WIN_CLOSE = 109;

const int HEADER_TOTAL_HEIGHT = 100;
const int SIDEBAR_MIN_WIDTH = 260;
const int SIDEBAR_MAX_WIDTH = 450;
const int TAB_WIDTH = 200;
const int TAB_HEIGHT = 34;

struct BrowserTab {
    wil::com_ptr<ICoreWebView2Controller> controller;
    wil::com_ptr<ICoreWebView2> webview;
    std::wstring title = L"New Tab";
    RECT tabRect;
    RECT closeRect;
};

// --- GLOBAL STATE ---
std::vector<BrowserTab> tabs;
std::vector<std::wstring> historyList;
int activeTabIndex = -1;
int hoveredHistoryIndex = -1;
int currentSidebarWidth = SIDEBAR_MIN_WIDTH;
HWND hEdit, hSidebarBtn, hNewTabBtn, hClearBtn, hSettingsBtn, hExpandBtn, hBtnMin, hBtnMax, hBtnClose;
WNDPROC OldEditProc;
HFONT hFontMain, hFontSmall, hFontSymbols;
bool isSidebarOpen = true;
bool isSettingsView = false;
bool isExpanded = false;
bool isVideoFullScreen = false;

// --- DARK THEME COLORS ---
COLORREF colBgHeader = RGB(24, 24, 28);
COLORREF colBgSidebar = RGB(18, 18, 20);
COLORREF colTabActive = RGB(38, 38, 42);
COLORREF colTabInactive = RGB(28, 28, 32);
COLORREF colAccent = RGB(0, 120, 215);
COLORREF colHoverGlow = RGB(45, 45, 50);
COLORREF colTextMain = RGB(240, 240, 240);
COLORREF colTextDim = RGB(160, 160, 160);
COLORREF colBtnBg = RGB(38, 38, 42);

// --- FORWARD DECLARATIONS ---
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
void CreateNewTab(HWND hWnd);
void SwitchToTab(int index, HWND hWnd);
void CloseTab(int index, HWND hWnd);
void UpdateLayout(HWND hWnd);

void SyncAddressBar() {
    if (activeTabIndex != -1 && activeTabIndex < (int)tabs.size() && tabs[activeTabIndex].webview) {
        wil::unique_cotaskmem_string url;
        tabs[activeTabIndex].webview->get_Source(&url);
        SetWindowText(hEdit, url.get());
    }
}

void ToggleUIElements(bool show) {
    int cmd = show ? SW_SHOW : SW_HIDE;
    ShowWindow(hEdit, cmd);
    ShowWindow(hSidebarBtn, cmd);
    ShowWindow(hNewTabBtn, cmd);
    ShowWindow(hBtnMin, cmd);
    ShowWindow(hBtnMax, cmd);
    ShowWindow(hBtnClose, cmd);
    int sidebarCmd = (show && isSidebarOpen) ? SW_SHOW : SW_HIDE;
    ShowWindow(hSettingsBtn, sidebarCmd);
    ShowWindow(hExpandBtn, sidebarCmd);
    int histCmd = (sidebarCmd == SW_SHOW && !isSettingsView) ? SW_SHOW : SW_HIDE;
    ShowWindow(hClearBtn, histCmd);
}

void UpdateLayout(HWND hWnd) {
    if (activeTabIndex == -1 || activeTabIndex >= (int)tabs.size()) return;
    RECT rc;
    GetClientRect(hWnd, &rc);
    if (isVideoFullScreen) {
        RECT full = { 0, 0, rc.right, rc.bottom };
        tabs[activeTabIndex].controller->put_Bounds(full);
        ToggleUIElements(false);
    }
    else {
        int xOff = isSidebarOpen ? currentSidebarWidth : 0;
        RECT webBounds = { xOff, HEADER_TOTAL_HEIGHT, rc.right, rc.bottom };
        tabs[activeTabIndex].controller->put_Bounds(webBounds);
        MoveWindow(hBtnClose, rc.right - 46, 0, 46, 32, TRUE);
        MoveWindow(hBtnMax, rc.right - 92, 0, 46, 32, TRUE);
        MoveWindow(hBtnMin, rc.right - 138, 0, 46, 32, TRUE);
        MoveWindow(hSidebarBtn, 10, 10, 40, 40, TRUE);
        int editW = min(rc.right - 400, 700);
        int editX = (rc.right - editW) / 2;
        MoveWindow(hEdit, editX, 15, editW, 30, TRUE);
        if (isSidebarOpen) {
            MoveWindow(hExpandBtn, 15, HEADER_TOTAL_HEIGHT + 10, 80, 28, TRUE);
            MoveWindow(hSettingsBtn, currentSidebarWidth - 45, HEADER_TOTAL_HEIGHT + 10, 32, 28, TRUE);
            MoveWindow(hClearBtn, 15, rc.bottom - 45, currentSidebarWidth - 30, 30, TRUE);
        }
        ToggleUIElements(true);
    }
}

LRESULT CALLBACK EditProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        wchar_t url[2048]; GetWindowText(hWnd, url, 2048);
        std::wstring urlStr(url);
        if (urlStr.find(L"://") == std::wstring::npos) urlStr = L"https://www.google.com/search?q=" + urlStr;
        if (activeTabIndex != -1 && tabs[activeTabIndex].webview) tabs[activeTabIndex].webview->Navigate(urlStr.c_str());
        return 0;
    }
    return CallWindowProc(OldEditProc, hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ int nCmdShow) {
    hFontMain = CreateFont(19, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    hFontSmall = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    hFontSymbols = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");

    WNDCLASSEX wcex = { sizeof(WNDCLASSEX), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0, hInstance, NULL,
                        LoadCursor(NULL, IDC_ARROW), CreateSolidBrush(colBgHeader), NULL, L"SARF_CORE", NULL };
    RegisterClassEx(&wcex);

    HWND hWnd = CreateWindowEx(0, L"SARF_CORE", L"SARF Browser", WS_POPUP | WS_VISIBLE | WS_SYSMENU | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800, NULL, NULL, hInstance, NULL);

    hSidebarBtn = CreateWindow(L"BUTTON", L"☰", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_SIDEBAR_BTN, hInstance, NULL);
    hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"Search...", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_ADDRESS_BAR, hInstance, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontMain, TRUE);
    OldEditProc = (WNDPROC)SetWindowLongPtr(hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);

    hNewTabBtn = CreateWindow(L"BUTTON", L"+", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_NEW_TAB_BTN, hInstance, NULL);
    hBtnClose = CreateWindow(L"BUTTON", L"✕", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_CLOSE, hInstance, NULL);
    hBtnMax = CreateWindow(L"BUTTON", L"▢", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_MAX, hInstance, NULL);
    hBtnMin = CreateWindow(L"BUTTON", L"—", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_WIN_MIN, hInstance, NULL);

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
        hExpandBtn = CreateWindow(L"BUTTON", L"RESIZE", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_EXPAND_SIDEBAR, NULL, NULL);
        hSettingsBtn = CreateWindow(L"BUTTON", L"⚙", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_SETTINGS_BTN, NULL, NULL);
        hClearBtn = CreateWindow(L"BUTTON", L"Clear History", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, hWnd, (HMENU)IDC_CLEAR_HISTORY_BTN, NULL, NULL);
    } break;
    case WM_SIZE: UpdateLayout(hWnd); break;
    case WM_MOUSEMOVE: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        int lastHover = hoveredHistoryIndex;
        hoveredHistoryIndex = -1;
        if (isSidebarOpen && !isSettingsView) {
            int hy = HEADER_TOTAL_HEIGHT + 100;
            for (int i = 0; i < (int)historyList.size(); i++) {
                RECT r = { 20, hy, currentSidebarWidth - 20, hy + 25 };
                if (PtInRect(&r, pt)) { hoveredHistoryIndex = i; break; }
                hy += 30;
            }
        }
        if (hoveredHistoryIndex != -1) SetCursor(LoadCursor(NULL, IDC_HAND));
        if (lastHover != hoveredHistoryIndex) InvalidateRect(hWnd, NULL, FALSE);
    } break;
    case WM_LBUTTONDOWN: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        if (hoveredHistoryIndex != -1) {
            tabs[activeTabIndex].webview->Navigate(historyList[hoveredHistoryIndex].c_str());
            return 0;
        }
        for (int i = 0; i < (int)tabs.size(); i++) {
            if (PtInRect(&tabs[i].closeRect, pt)) { CloseTab(i, hWnd); return 0; }
            if (PtInRect(&tabs[i].tabRect, pt)) { SwitchToTab(i, hWnd); return 0; }
        }
    } break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        HBRUSH hBtnBrush = CreateSolidBrush(colBtnBg);
        FillRect(pdis->hDC, &pdis->rcItem, hBtnBrush);
        DeleteObject(hBtnBrush);
        SetTextColor(pdis->hDC, colTextMain);
        SetBkMode(pdis->hDC, TRANSPARENT);
        wchar_t txt[64]; GetWindowText(pdis->hwndItem, txt, 64);
        SelectObject(pdis->hDC, (pdis->CtlID == IDC_SETTINGS_BTN || pdis->CtlID == IDC_SIDEBAR_BTN) ? hFontSymbols : hFontSmall);
        DrawText(pdis->hDC, txt, -1, &pdis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        RECT headerR = { 0, 0, rc.right, HEADER_TOTAL_HEIGHT };
        HBRUSH hHead = CreateSolidBrush(colBgHeader);
        FillRect(hdc, &headerR, hHead);
        DeleteObject(hHead);
        SetBkMode(hdc, TRANSPARENT);
        if (isSidebarOpen) {
            RECT sideR = { 0, HEADER_TOTAL_HEIGHT, currentSidebarWidth, rc.bottom };
            HBRUSH hSide = CreateSolidBrush(colBgSidebar);
            FillRect(hdc, &sideR, hSide);
            DeleteObject(hSide);
            SelectObject(hdc, hFontMain);
            SetTextColor(hdc, colAccent);
            if (isSettingsView) {
                TextOut(hdc, 20, HEADER_TOTAL_HEIGHT + 60, L"SETTINGS", 8);
            }
            else {
                TextOut(hdc, 20, HEADER_TOTAL_HEIGHT + 60, L"HISTORY", 7);
                int hy = HEADER_TOTAL_HEIGHT + 100;
                SelectObject(hdc, hFontSmall);
                for (int i = 0; i < (int)historyList.size(); i++) {
                    if (hy > rc.bottom - 60) break;
                    RECT hr = { 20, hy, currentSidebarWidth - 20, hy + 25 };
                    if (i == hoveredHistoryIndex) {
                        HBRUSH hGlow = CreateSolidBrush(colHoverGlow);
                        FillRect(hdc, &hr, hGlow);
                        DeleteObject(hGlow);
                        SetTextColor(hdc, colAccent);
                    }
                    else {
                        SetTextColor(hdc, colTextDim);
                    }
                    DrawText(hdc, historyList[i].c_str(), -1, &hr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS);
                    hy += 30;
                }
            }
        }
        int tx = 10;
        for (int i = 0; i < (int)tabs.size(); i++) {
            tabs[i].tabRect = { tx, 66, tx + TAB_WIDTH, 100 };
            tabs[i].closeRect = { tabs[i].tabRect.right - 28, 71, tabs[i].tabRect.right - 4, 95 };
            bool active = (i == activeTabIndex);
            HBRUSH hTb = CreateSolidBrush(active ? colTabActive : colTabInactive);
            FillRect(hdc, &tabs[i].tabRect, hTb);
            DeleteObject(hTb);
            if (active) {
                RECT acc = { tabs[i].tabRect.left, tabs[i].tabRect.bottom - 3, tabs[i].tabRect.right, tabs[i].tabRect.bottom };
                HBRUSH hAcc = CreateSolidBrush(colAccent);
                FillRect(hdc, &acc, hAcc);
                DeleteObject(hAcc);
            }
            SetTextColor(hdc, active ? colTextMain : colTextDim);
            RECT textR = { tabs[i].tabRect.left + 10, tabs[i].tabRect.top, tabs[i].tabRect.right - 30, tabs[i].tabRect.bottom };
            DrawText(hdc, tabs[i].title.c_str(), -1, &textR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            DrawText(hdc, L"✕", -1, &tabs[i].closeRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            tx += TAB_WIDTH + 4;
        }
        MoveWindow(hNewTabBtn, tx, 69, 28, 28, TRUE);
        EndPaint(hWnd, &ps);
    } break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_WIN_CLOSE: PostQuitMessage(0); break;
        case IDC_WIN_MIN: ShowWindow(hWnd, SW_MINIMIZE); break;
        case IDC_WIN_MAX: IsZoomed(hWnd) ? ShowWindow(hWnd, SW_RESTORE) : ShowWindow(hWnd, SW_MAXIMIZE); break;
        case IDC_SIDEBAR_BTN: isSidebarOpen = !isSidebarOpen; UpdateLayout(hWnd); InvalidateRect(hWnd, NULL, TRUE); break;
        case IDC_NEW_TAB_BTN: CreateNewTab(hWnd); break;
        case IDC_SETTINGS_BTN: isSettingsView = !isSettingsView; InvalidateRect(hWnd, NULL, TRUE); break;
        case IDC_CLEAR_HISTORY_BTN: historyList.clear(); InvalidateRect(hWnd, NULL, TRUE); break;
        case IDC_EXPAND_SIDEBAR:
            isExpanded = !isExpanded;
            currentSidebarWidth = isExpanded ? SIDEBAR_MAX_WIDTH : SIDEBAR_MIN_WIDTH;
            UpdateLayout(hWnd); InvalidateRect(hWnd, NULL, TRUE);
            break;
        }
        break;
    case WM_NCHITTEST: {
        POINT pt = { LOWORD(lParam), HIWORD(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc; GetClientRect(hWnd, &rc);
        if (pt.y < 32 && pt.x >(rc.right - 140)) return HTCLIENT;
        if (pt.y < HEADER_TOTAL_HEIGHT) {
            if (pt.y > 60 || (pt.x > 10 && pt.x < 50)) return HTCLIENT;
            return HTCAPTION;
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    } break;
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void CreateNewTab(HWND hWnd) {
    CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hWnd](HRESULT res, ICoreWebView2Environment* env) -> HRESULT {
                env->CreateCoreWebView2Controller(hWnd, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hWnd](HRESULT res, ICoreWebView2Controller* ctrl) -> HRESULT {
                        if (ctrl) {
                            BrowserTab nt;
                            nt.controller = ctrl;
                            nt.controller->get_CoreWebView2(&nt.webview);
                            nt.webview->add_SourceChanged(Callback<ICoreWebView2SourceChangedEventHandler>(
                                [hWnd](ICoreWebView2* s, ICoreWebView2SourceChangedEventArgs* a) -> HRESULT {
                                    wil::unique_cotaskmem_string url; s->get_Source(&url);
                                    if (historyList.empty() || historyList[0] != url.get()) {
                                        historyList.insert(historyList.begin(), url.get());
                                        if (historyList.size() > 25) historyList.pop_back();
                                    }
                                    SyncAddressBar(); InvalidateRect(hWnd, NULL, TRUE); return S_OK;
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
    for (int i = 0; i < (int)tabs.size(); i++) tabs[i].controller->put_IsVisible(i == activeTabIndex);
    UpdateLayout(hWnd); SyncAddressBar(); InvalidateRect(hWnd, NULL, TRUE);
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