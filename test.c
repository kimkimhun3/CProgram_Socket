#include <windows.h>

#define IDC_IPADDRESS   101
#define IDC_PORTNUMBER  102
#define IDC_RUNBUTTON   103
#define IDC_STOPBUTTON  104

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char CLASS_NAME[] = "Sample Window Class";

    WNDCLASS wc = { };

    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Network Input Application",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 200,
        NULL, NULL, hInstance, NULL
    );

    if (hwnd == NULL) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);

    // Create IP Address Input Field
    HWND hwndIpAddress = CreateWindowEx(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        20, 20, 150, 25,
        hwnd, (HMENU)IDC_IPADDRESS, hInstance, NULL
    );

    // Create Port Number Input Field
    HWND hwndPortNumber = CreateWindowEx(
        0, "EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER,
        20, 60, 150, 25,
        hwnd, (HMENU)IDC_PORTNUMBER, hInstance, NULL
    );

    // Create Run Button
    HWND hwndRunButton = CreateWindowEx(
        0, "BUTTON", "Run",
        WS_CHILD | WS_VISIBLE,
        20, 100, 80, 30,
        hwnd, (HMENU)IDC_RUNBUTTON, hInstance, NULL
    );

    // Create Stop Button
    HWND hwndStopButton = CreateWindowEx(
        0, "BUTTON", "Stop",
        WS_CHILD | WS_VISIBLE,
        110, 100, 80, 30,
        hwnd, (HMENU)IDC_STOPBUTTON, hInstance, NULL
    );

    MSG msg = { };
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_RUNBUTTON) {
            MessageBox(hwnd, "Run button clicked!", "Info", MB_OK);
        } else if (LOWORD(wParam) == IDC_STOPBUTTON) {
            MessageBox(hwnd, "Stop button clicked!", "Info", MB_OK);
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Drawing code here, I don't know how it supposed to happen with that code, buut sometime it supposed to be the wrong one.
        // if we compare between Linux and Window, then it will be worn
        EndPaint(hwnd, &ps);
    }
    return 0;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
