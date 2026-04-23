#include <windows.h>
#include <uiautomation.h>
#include <stdio.h>

IUIAutomation* uia = nullptr;

void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event != EVENT_OBJECT_FOCUS) return;
    
    char clsName[256];
    GetClassNameA(hwnd, clsName, 256);
    if (!strstr(clsName, "Chrome") && !strstr(clsName, "msedge") && !strstr(clsName, "Mozilla")) return;

    if (uia) {
        IUIAutomationElement* el = nullptr;
        if (SUCCEEDED(uia->GetFocusedElement(&el)) && el) {
            BSTR className = nullptr;
            BSTR id = nullptr;
            BSTR name = nullptr;
            el->get_CurrentClassName(&className);
            el->get_CurrentAutomationId(&id);
            el->get_CurrentName(&name);
            
            FILE* f = fopen("focus_log.txt", "a");
            if (f) {
                fprintf(f, "Focus: Class='%ws' ID='%ws' Name='%ws'\n", 
                    className ? className : L"", 
                    id ? id : L"", 
                    name ? name : L"");
                fclose(f);
            }
            if (className) SysFreeString(className);
            if (id) SysFreeString(id);
            if (name) SysFreeString(name);
            el->Release();
        }
    }
}

int main() {
    CoInitialize(nullptr);
    CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    
    HWINEVENTHOOK hook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS, nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    UnhookWinEvent(hook);
    if (uia) uia->Release();
    CoUninitialize();
    return 0;
}
