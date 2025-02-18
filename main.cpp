#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx10.h"
#include <d3d10_1.h>
#include <d3d10.h>
#include <tchar.h>

#include <vector>
#include <iostream>
#include <string>
#include "IconsFontAwesome5.h"
#include <portaudio.h>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "font1.h" // roboto
#include "font2.h" // fa-solid-900

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "portaudio.lib")

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 128
#define NUM_CHANNELS 1
    
static ID3D10Device* g_pd3dDevice = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D10RenderTargetView* g_mainRenderTargetView = nullptr;

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImVec4 clear_color = ImVec4(0.173f, 0.184f, 0.2f, 1.00f);
bool showwindow = false;
std::string currentText;
char buffer[512] = "";
char buf[512];
std::vector<std::string> msgs;
bool voice_active = false;
static bool autoScroll = true;
char bufer[128];
std::vector<std::string> users_in_voice;
SOCKET client_socket;
bool btn = false;
std::string delimiter = "-=S=-";
std::string name = "test_acc";
bool log_inf = false;
bool log_inf2 = false;
bool log_inf3 = false;
bool log_inf4 = false;

struct Message {
    std::string text;
    std::string sender;
    std::string time;
};

std::vector<Message> chat_msgs;

struct Dimensions { float width; float height; };

Dimensions getWS(HWND& hwnd) {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    float w = windowRect.right - windowRect.left;
    float h = windowRect.bottom - windowRect.top;
    return { w, h };
}

int audio_callback(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    if (send(client_socket, (const char*)inputBuffer, framesPerBuffer * NUM_CHANNELS * 2, 0) == SOCKET_ERROR) {
        return paComplete;
    }

    int bytes_received = recv(client_socket, (char*)outputBuffer, framesPerBuffer * NUM_CHANNELS * 2, 0);
    if (bytes_received <= 0) return paComplete;




    return paContinue;
}

json send_json_request(const json& request) {
    std::string request_str = request.dump(); // Преобразуем JSON в строку
    if (send(client_socket, request_str.c_str(), request_str.size(), 0) == SOCKET_ERROR) {
        std::cerr << "Send failed." << std::endl;
        return json{ {"status", "error"}, {"message", "Send failed"} };
    }

    char buffer[1024];
    int bytes_received = recv(client_socket, buffer, sizeof(buffer), 0);
    if (bytes_received <= 0) {
        std::cerr << "No response from server." << std::endl;
        return json{ {"status", "error"}, {"message", "No response from server"} };
    }

    std::string response_str(buffer, bytes_received);
    try {
        return json::parse(response_str); // Парсим ответ в JSON
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return json{ {"status", "error"}, {"message", "JSON parse error"} };
    }
}

int c = 0;
void server_connect() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_WSAStartup failed_");
        c = 0;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Socket creation failed_");
        WSACleanup();
        c = 0;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    //inet_pton(AF_INET, "evreitop228.ddns.net", &server_addr.sin_addr);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(412);

    msgs.push_back("[INF] init");

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Connect failed_");
        closesocket(client_socket);
        WSACleanup();
        c = 0;
    }
    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) != SOCKET_ERROR) {
        c = 1;
    }
}

void v_chat(HWND& hwnd, PaStream *stream) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImVec2 windowSize = ImVec2(x, y);
    ImGui::Begin("Voice chat", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.173f, 0.184f, 0.2f, 1.0f));
    ImGui::SetWindowSize(ImVec2(windowSize.x * 0.35f, windowSize.y - 40), ImGuiCond_Always);
    ImGui::SetWindowPos(ImVec2(0, 0));
    
    if (log_inf == false) {
        msgs.push_back("[INF] init");
        log_inf = true;
    }

    if (!voice_active) {
        if (c == 0) {
            if (ImGui::Button("Reconnect to server", ImVec2(x * 0.14, y * 0.05))) {
                server_connect();
            }
            
        }
        ImGui::SetCursorPos(ImVec2((x * 0.35f) / 3, y / 2));
        if (c == 1) {
            //if (ImGui::Button("Connect", ImVec2(windowSize.x * 0.35f, windowSize.y - 40))) {
            if (ImGui::Button("Connect", ImVec2(x * 0.06, y * 0.05))) {
                if (Pa_IsStreamActive(stream) == 0) {
                    std::thread audio_thread([&]() {
                        PaError err = Pa_StartStream(stream);
                        if (err != paNoError) {
                            std::cerr << "PortAudio error (StartStream): " << Pa_GetErrorText(err) << std::endl;
                            msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                        }
                        });
                    audio_thread.detach();
                }
                msgs.push_back("[INF] connecting to voice chat");

                voice_active = true;
                json request = { {"command", "voice_connect"}, {"message", name} };
                json voice_vopros = send_json_request(request);
                if (voice_vopros["message"] == 1) {
                    printf("connected\n");

                    request = { {"command", "get_voice_clients"} };
                    json voice_cl = send_json_request(request);
                    printf("voice_cl\n");
                    std::string v_cl = voice_cl["message"];
                    std::cout << v_cl << std::endl;

                    size_t pos = 0;
                    while ((pos = v_cl.find(delimiter)) != std::string::npos) {
                        users_in_voice.push_back(v_cl.substr(0, pos));
                        v_cl.erase(0, pos + delimiter.length());
                    }
                }
            }
        }
    }

    if (voice_active == true) {
        ImGui::Begin("Voice chat main", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.173f, 0.184f, 0.2f, 1.0f));
        ImGui::SetWindowSize(ImVec2(windowSize.x * 0.35f, windowSize.y - 40), ImGuiCond_Always);
        ImGui::SetWindowPos(ImVec2(0, 0));

        ImGui::SetCursorPos(ImVec2((x * 0.35f) / 3, 100));
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Voice chat room");
        for (const auto& user : users_in_voice) {
            ImGui::Text("%s", user.c_str());
        }

        ImGui::PopStyleColor();
        ImGui::SetCursorPos(ImVec2(((x * 0.35f) / 2) - 100, y - 200));
        if (btn == true) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            if (Pa_IsStreamActive(stream) == 0) {
                std::thread audio_thread([&]() {
                    PaError err = Pa_StartStream(stream);
                    if (err != paNoError) {
                        std::cerr << "PortAudio error (StartStream): " << Pa_GetErrorText(err) << std::endl;
                        msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                    }
                    msgs.push_back("[INF] mic on");
                    });
                audio_thread.detach();
            }

        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f)); 
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
            if (Pa_IsStreamActive(stream) == 1) {
                msgs.push_back("[INF] mic off");
                Pa_StopStream(stream);
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        if (ImGui::Button(ICON_FA_MICROPHONE, ImVec2(x * 0.03, y * 0.05))) {
            printf("mic");
            btn = !btn;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, 30.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        if (ImGui::Button(ICON_FA_PHONE, ImVec2(x * 0.03, y * 0.05))) {
            if (Pa_IsStreamActive(stream) == 1) {
                PaError err = Pa_StopStream(stream);
                if (err != paNoError) {
                    std::cerr << "PortAudio error (StartStream): " << Pa_GetErrorText(err) << std::endl;
                    msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                }
                msgs.push_back("[INF] voice ended");
            }
            voice_active = false;
            users_in_voice.clear(); 
            
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        if (btn) { ImGui::Text("Mic on"); }
        else { ImGui::Text("Mic off"); }
        ImGui::End();
    }



    ImGui::PopStyleColor();
    ImGui::End();

    ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.173f, 0.184f, 0.2f, 1.0f));
    ImGui::SetWindowSize(ImVec2(windowSize.x * 0.65f, windowSize.y - 40), ImGuiCond_Always);
    ImGui::SetWindowPos(ImVec2(windowSize.x * 0.35f, 0));
    if (log_inf2 == false) {
        msgs.push_back("[INF] Init 2");
        log_inf2 = true;
    }
    ImGui::BeginChild("ChatScroll", ImVec2(0, windowSize.y - 80), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (!chat_msgs.empty()) {
        for (const auto& mSg : chat_msgs) {
            ImGui::Text("[%s] %s: %s", mSg.time.c_str(), mSg.sender.c_str(), mSg.text.c_str());
        }
        if (ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
            autoScroll = false;
        }
        if (autoScroll) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(windowSize.y - 60);
    ImGui::PushItemWidth(-1);
    if (ImGui::InputText(" ", buf, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (std::string(buf) == "" || std::string(buf) == " ") {

        }
        else {
            chat_msgs.push_back({ std::string(buf), "test_acc", "time" });
            msgs.push_back("[INF] Message sended: " + std::string(buf));
            buf[0] = '\0'; 
            autoScroll = true;
            ImGui::SetKeyboardFocusHere(-1);
        }
    }
    ImGui::SetItemDefaultFocus();
    ImGui::PopItemWidth();
    ImGui::PopStyleColor();
    ImGui::End();

}

void console(ImGuiIO& io) {
    ImGui::SetNextWindowSize(ImVec2(800, 410));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.3f, 0.184f, 0.5f, 0.4f));
    ImGui::Begin("Console", nullptr, ImGuiWindowFlags_NoScrollbar);
    if (log_inf3 == false) {
        msgs.push_back("[INF] console started");
        log_inf3 = true;
    }
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);    
    ImGui::BeginChild("F3", ImVec2(600, 300), false);

    if (!msgs.empty()) {
        for (const auto& msg : msgs) {
            ImGui::Text("%s", msg.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SetCursorPosY(380);
    if (ImGui::InputText("Enter ur command", buffer, 512, ImGuiInputTextFlags_EnterReturnsTrue)) {
        msgs.push_back("[INF] user command: " + std::string(buffer));
        if (std::string(buffer).substr(0, std::string(buffer).find(" ")) == "connect") {
            msgs.push_back("[connecting...]");
        }
        buffer[0] = '\0';
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// Main code
int main(int, char**)
{
    setlocale(LC_ALL, "RU");
    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ZIPcord DX10", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);
    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
    }
    PaStream* stream;
    err = Pa_OpenDefaultStream(&stream, NUM_CHANNELS, NUM_CHANNELS, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER, audio_callback, nullptr);
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    float baseFontSize = 20.0f;
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf, font_ttf_size, baseFontSize, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    //io.Fonts->AddFontFromFileTTF("Roboto-VariableFont_wdth,wght.ttf", baseFontSize, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = baseFontSize;
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf1, font_ttf_size1, baseFontSize, &icons_config, icons_ranges);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.137f, 0.153f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.137f, 0.153f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.173f, 0.184f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.137f, 0.153f, 0.165f, 1.0f);
    style.PopupRounding = 10;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->Build();
    //ImGui::StyleColorsLight();
    msgs.push_back("[INF] Styles loaded");
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX10_Init(g_pd3dDevice);
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle messages (inputs, window resize, etc.)
        // See the WndProc() function below for our to dispatch events to the Win32 backend.
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX10_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            v_chat(hwnd, stream);
            if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F3)) {
                showwindow = !showwindow;
            }

            if (showwindow) {
                console(io);
            }
        }

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDevice->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDevice->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }

    // Cleanup0
    ImGui_ImplDX10_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    if (stream && Pa_IsStreamActive(stream)) {
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
    }

    Pa_Terminate();
    if (client_socket != INVALID_SOCKET) {
        closesocket(client_socket);
    }
    WSACleanup();
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D10_CREATE_DEVICE_DEBUG;
    HRESULT res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D10Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}