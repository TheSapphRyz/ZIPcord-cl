#define WIN32_LEAN_AND_MEAN
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
#include <fstream>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "logo.hpp"
#include <mutex>
#include <commdlg.h>

#define MAX_BUFFER_SIZE 1048576 // 1MB

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "portaudio.lib")
#pragma comment(lib, "comdlg32.lib")

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 1

static ID3D10Device* g_pd3dDevice = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
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
PaStream* stream = nullptr; // Global stream
char buf[512] = "";
std::vector<std::string> msgs;
bool voice_active = false;
static bool autoScroll = true;
char bufer[128];
std::vector<std::string> users_in_voice;
SOCKET client_socket = INVALID_SOCKET;
bool btn = false;
std::string delimiter = "-=S=-";
std::string name = "test_acc";
bool log_inf = false;
bool log_inf2 = false;
bool log_inf3 = false;
bool log_inf4 = false;
bool log_inf5 = false;
bool shouldRender = true;

struct Message {
    std::string text;
    std::string sender;
    std::string time;
    ID3D10ShaderResourceView* media = nullptr;
    bool isImage = false;
    bool isVideo = false;
};

std::vector<Message> chat_msgs;
char c = 0;

struct AudioData {
    std::array<float, 16384> buffer;
    size_t writePos = 0, readPos = 0;
    std::atomic<bool> hasData{ false };
    std::mutex audioMutex;
};
AudioData audioData;

struct Dimensions { float width; float height; };

Dimensions getWS(HWND& hwnd) {
    RECT windowRect;
    GetWindowRect(hwnd, &windowRect);
    float w = windowRect.right - windowRect.left;
    float h = windowRect.bottom - windowRect.top;
    return { w, h };
}

std::string base64_encode(const unsigned char* data, size_t length) {
    static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((length + 2) / 3 * 4);
    for (size_t i = 0; i < length; i += 3) {
        unsigned char b1 = data[i];
        unsigned char b2 = (i + 1 < length) ? data[i + 1] : 0;
        unsigned char b3 = (i + 2 < length) ? data[i + 2] : 0;
        result.push_back(base64_chars[b1 >> 2]);
        result.push_back(base64_chars[((b1 & 0x03) << 4) | (b2 >> 4)]);
        result.push_back((i + 1 < length) ? base64_chars[((b2 & 0x0F) << 2) | (b3 >> 6)] : '=');
        result.push_back((i + 2 < length) ? base64_chars[b3 & 0x3F] : '=');
    }
    return result;
}

std::vector<unsigned char> base64_decode(const std::string& encoded) {
    static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> result;
    result.reserve(encoded.length() * 3 / 4);

    std::vector<int> lookup(256, -1);
    for (int i = 0; i < 64; ++i) lookup[base64_chars[i]] = i;

    int val = 0, bits = -8;
    for (char c : encoded) {
        if (c == '=') break;
        if (lookup[c] == -1) continue;
        val = (val << 6) + lookup[c];
        bits += 6;
        if (bits >= 0) {
            result.push_back((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return result;
}

std::vector<std::string> split(std::string s, std::string delimiter) {
    size_t pos_start = 0, pos_end, delim_len = delimiter.length();
    std::string token;
    std::vector<std::string> res;
    while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
        token = s.substr(pos_start, pos_end - pos_start);
        pos_start = pos_end + delim_len;
        res.push_back(token);
    }
    res.push_back(s.substr(pos_start));
    return res;
}

int audio_callback(const void* inputBuffer, void* outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData) {
    if (btn && voice_active) {
        if (send(client_socket, (const char*)inputBuffer, framesPerBuffer * NUM_CHANNELS * sizeof(short), 0) == SOCKET_ERROR) {
            return paComplete;
        }
    }
    if (voice_active) {
        int bytes_received = recv(client_socket, (char*)outputBuffer, framesPerBuffer * NUM_CHANNELS * sizeof(short), 0);
        if (bytes_received <= 0) {
            return paComplete;
        }
    }
    else {
        memset(outputBuffer, 0, framesPerBuffer * NUM_CHANNELS * sizeof(short));
    }
    return paContinue;
}

void receive_messages() {
    std::vector<char> buffer(MAX_BUFFER_SIZE);
    while (true) {
        int bytes_received = recv(client_socket, buffer.data(), MAX_BUFFER_SIZE, 0);
        if (bytes_received <= 0) {
            std::cerr << "Connection closed or error: " << WSAGetLastError() << std::endl;
            msgs.push_back("[ERR] Connection closed");
            voice_active = false;
            c = 0;
            shouldRender = true;
            break;
        }

        // Проверяем, является ли сообщение JSON (текстовым)
        if (buffer[0] == '{' && buffer[bytes_received - 1] == '}') {
            std::string message(buffer.data(), bytes_received);
            std::cout << "Processing JSON message: " << message << "\n";
            try {
                json received_json = json::parse(message);
                if (received_json.contains("type")) {
                    std::string type = received_json["type"];
                    if (type == "chat_msg") {
                        std::string sender = received_json["username"];
                        std::string message = received_json["message"];
                        std::string time = received_json["time"];
                        chat_msgs.push_back({ message, sender, time });
                        msgs.push_back("[INF] Received chat message from " + sender + ": " + message);
                        shouldRender = true;
                    }
                    else if (type == "voice_connect") {
                        if (received_json["message"] == 1) {
                            msgs.push_back("[INF] Connected to voice chat");
                            shouldRender = true;
                        }
                    }
                    else if (type == "get_voice_clients") {
                        for (const auto& i : split(received_json["message"], "-=S=-")) {
                            users_in_voice.push_back(std::string(i));
                            shouldRender = true;
                        }
                    }
                    else if (type == "disconnect") {
                        if (received_json["message"] == 1) {
                            voice_active = false;
                            shouldRender = false;
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                std::cerr << "JSON parse error: " << e.what() << "\n";
            }
        }
        else {
            if (voice_active) {
                std::lock_guard<std::mutex> lock(audioData.audioMutex);
                short* audio = (short*)buffer.data();
                size_t samples = bytes_received / sizeof(short);
                for (size_t i = 0; i < samples && audioData.writePos < audioData.buffer.size(); ++i) {
                    audioData.buffer[audioData.writePos] = audio[i] / 32767.0f; 
                    audioData.writePos = (audioData.writePos + 1) % audioData.buffer.size();
                }
                audioData.hasData = true;

                std::cout << "Audio data queued, samples: " << samples << "\n";
                msgs.push_back("[INF] Audio data received, samples: " + std::to_string(samples));
                shouldRender = true;
            }
        }
    }
}

json send_json_request(const json& request) {
    std::string request_str = request.dump();
    if (send(client_socket, request_str.c_str(), request_str.size(), 0) == SOCKET_ERROR) {
        std::cerr << "Send failed." << std::endl;
        return json{ {"status", "error"}, {"message", "Send failed"} };
    }
    std::vector<char> buffer(1024);
    int bytes_received = recv(client_socket, buffer.data(), buffer.size(), 0);
    if (bytes_received <= 0) return json{ {"status", "error"}, {"message", "No response from server"} };
    std::string response_str(buffer.data(), bytes_received);
    try {
        return json::parse(response_str);
    }
    catch (const std::exception& e) {
        std::cerr << "JSON parse error: " << e.what() << std::endl;
        return json{ {"status", "error"}, {"message", "JSON parse error"} };
    }
}

int server_connect() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_WSAStartup failed_");
        c = 0;
        shouldRender = true;
        return 1;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Socket creation failed_");
        WSACleanup();
        c = 0;
        shouldRender = true;
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(412);

    msgs.push_back("[INF] init");
    shouldRender = true;

    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Connect failed_");
        closesocket(client_socket);
        WSACleanup();
        c = 0;
        shouldRender = true;
        return 1;
    }
    c = 1;
    shouldRender = true;
    return 0;
}

void v_chat(HWND& hwnd, PaStream* stream) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImVec2 windowSize = ImVec2(x, y);
    ImGui::Begin("Voice chat", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.173f, 0.184f, 0.2f, 1.0f));
    ImGui::SetWindowSize(ImVec2(windowSize.x * 0.35f, windowSize.y - 40), ImGuiCond_Always);
    ImGui::SetWindowPos(ImVec2(0, 0));

    if (log_inf == false) {
        msgs.push_back("[INF] init");
        log_inf = true;
        shouldRender = true;
    }

    if (!voice_active) {
        if (c == 0) {
            if (ImGui::Button("Reconnect to server", ImVec2(x * 0.14, y * 0.05))) {
                server_connect();
                json j = send_json_request({ {"type", "try"} });
                if (j.value("message", 0) == 1) c = 1;
                shouldRender = true;
            }
        }
        ImGui::SetCursorPos(ImVec2((x * 0.35f) / 3, y / 2));
        if (c == 1) {
            if (ImGui::Button("Connect", ImVec2(x * 0.06, y * 0.05))) {
                voice_active = true;
                msgs.push_back("[INF] connecting to voice chat");
                json request = { {"type", "voice_connect"}, {"message", name} };
                std::string request_str = request.dump();
                send(client_socket, request_str.c_str(), request_str.size(), 0);
                json request1 = {{"type", "voice_cl_list"} };
                send(client_socket, request1.dump().c_str(), request1.dump().size(), 0);
                if (stream && Pa_IsStreamStopped(stream)) {
                    PaError err = Pa_StartStream(stream);
                    if (err != paNoError) {
                        std::cerr << "PortAudio error (StartStream): " << Pa_GetErrorText(err) << std::endl;
                        msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                    }
                    else {
                        msgs.push_back("[INF] Audio stream started");
                    }
                }
                shouldRender = true;
            }
        }
    }
    else {
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
        if (btn) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5961f, 0.9843f, 0.5961f, 1.0f));
            if (stream && Pa_IsStreamStopped(stream)) {
                PaError err = Pa_StartStream(stream);
                if (err != paNoError) {
                    std::cerr << "PortAudio error (StartStream): " << Pa_GetErrorText(err) << std::endl;
                    msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                }
                else {
                    msgs.push_back("[INF] Mic on");
                }
            }
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
            if (stream && Pa_IsStreamActive(stream)) {
                PaError err = Pa_StopStream(stream);
                if (err != paNoError) {
                    std::cerr << "PortAudio error (StopStream): " << Pa_GetErrorText(err) << std::endl;
                    msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                }
                else {
                    msgs.push_back("[INF] Mic off");
                }
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        if (ImGui::Button(ICON_FA_MICROPHONE, ImVec2(x * 0.03, y * 0.05))) {
            btn = !btn;
            shouldRender = true;
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0.0f, 30.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8784f, 0.2471f, 0.3098f, 1.0f));
        if (ImGui::Button(ICON_FA_PHONE, ImVec2(x * 0.03, y * 0.05))) {
            if (stream && Pa_IsStreamActive(stream)) {
                PaError err = Pa_StopStream(stream);
                if (err != paNoError) {
                    std::cerr << "PortAudio error (StopStream): " << Pa_GetErrorText(err) << std::endl;
                    msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
                }
            }
            voice_active = false;
            btn = false;
            users_in_voice.clear();
            json request = { {"type", "voice_control"}, {"command", "leave_voice"}, {"message", name} };
            std::string request_str = request.dump();
            send(client_socket, request_str.c_str(), request_str.size(), 0);
            msgs.push_back("[INF] voice ended");
            shouldRender = true;
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
        shouldRender = true;
    }
    ImGui::BeginChild("ChatScroll", ImVec2(0, windowSize.y - 80), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (!chat_msgs.empty()) {
        for (const auto& mSg : chat_msgs) {
            ImGui::Text("[%s] %s: %s", mSg.time.c_str(), mSg.sender.c_str(), mSg.text.c_str());
            if (mSg.isImage && mSg.media) {
                ImGui::Image((ImTextureID)mSg.media, ImVec2(100, 100));
            }
            else if (mSg.isVideo && mSg.media) {
                ImGui::Text("[Video Frame]");
            }
        }
        if (ImGui::GetScrollY() < ImGui::GetScrollMaxY()) autoScroll = false;
        if (autoScroll) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(windowSize.y - 70);
    ImGui::PushItemWidth(-70);
    if (ImGui::InputText(" ", buf, 1024, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (std::string(buf) != "" && std::string(buf) != " ") {
            SYSTEMTIME st;
            GetSystemTime(&st);
            char t[80] = "";
            sprintf_s(t, "%d:%d:%d", st.wHour + 3, st.wMinute, st.wSecond);
            json request = { {"type", "chat_msg"}, {"username", name}, {"message", std::string(buf)}, {"time", std::string(t)} };
            send(client_socket, request.dump().c_str(), request.dump().size(), 0);
            chat_msgs.push_back({ std::string(buf), name, std::string(t) });
            msgs.push_back("[INF] Message sent: " + std::string(buf));
            buf[0] = '\0';
            autoScroll = true;
            ImGui::SetKeyboardFocusHere(-1);
            shouldRender = true;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FILE, ImVec2(x * 0.02, y * 0.03))) {
        OPENFILENAMEW ofn = { 0 };
        WCHAR file[260] = { 0 };
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = file;
        ofn.nMaxFile = sizeof(file) / sizeof(WCHAR);
        ofn.lpstrFilter = L"Media Files\0*.png;*.jpg;*.mp4\0All Files\0*.*\0";
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            SYSTEMTIME st;
            GetSystemTime(&st);
            char t[80] = "";
            sprintf_s(t, "%d:%d:%d", st.wHour + 3, st.wMinute, st.wSecond);
            std::ifstream fileStream(file, std::ios::binary);
            std::vector<char> fileData((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
            fileStream.close();
            std::string base64_data = base64_encode((unsigned char*)fileData.data(), fileData.size());
            json request = { {"type", (wcsstr(file, L".mp4") ? "video" : "image")}, {"sender", name}, {"data", base64_data}, {"time", std::string(t)} };
            std::string request_str = request.dump();
            send(client_socket, request_str.c_str(), request_str.size(), 0);
            int width, height, channels;
            unsigned char* img_data = stbi_load_from_memory((unsigned char*)fileData.data(), fileData.size(), &width, &height, &channels, 4);
            ID3D10ShaderResourceView* media = nullptr;
            if (img_data) {
                D3D10_TEXTURE2D_DESC desc = { width, height, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D10_USAGE_DEFAULT, D3D10_BIND_SHADER_RESOURCE, 0, 0 };
                D3D10_SUBRESOURCE_DATA subData = { img_data, width * 4, 0 };
                ID3D10Texture2D* texture;
                g_pd3dDevice->CreateTexture2D(&desc, &subData, &texture);
                g_pd3dDevice->CreateShaderResourceView(texture, nullptr, &media);
                texture->Release();
                stbi_image_free(img_data);
            }
            chat_msgs.push_back({ wcsstr(file, L".mp4") ? "[Video]" : "[Image]", name, std::string(t), media, !wcsstr(file, L".mp4"), !!wcsstr(file, L".mp4") });
            shouldRender = true;
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
        shouldRender = true;
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
        shouldRender = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

char b_us[42] = { 0 };
char p[20] = { 0 };
bool login = false;

ID3D10ShaderResourceView* CreateTextureFromMemory(ID3D10Device* device, const unsigned char* image_data, int image_size) {
    int width, height, channels;
    unsigned char* data = stbi_load_from_memory(image_data, image_size, &width, &height, &channels, 4);
    if (!data) return nullptr;
    D3D10_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    ID3D10Texture2D* pTexture = nullptr;
    D3D10_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    device->CreateTexture2D(&desc, &subResource, &pTexture);
    ID3D10ShaderResourceView* textureView = nullptr;
    if (pTexture != nullptr) {
        D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc;
        ZeroMemory(&srvDesc, sizeof(srvDesc));
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = desc.MipLevels;
        srvDesc.Texture2D.MostDetailedMip = 0;
        device->CreateShaderResourceView(pTexture, &srvDesc, &textureView);
        pTexture->Release();
    }
    stbi_image_free(data);
    return textureView;
}

void login_false(HWND& hwnd) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.18f, 0.19f, 0.21f, 1.0f));
    ImGui::Begin("Login", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetWindowPos(ImVec2(x * 0.4f, y * 0.06f));
    ImGui::SetWindowSize(ImVec2(x * 0.2f, y * 0.5f));
    ImGui::Text("Registration");
    ImGui::SetCursorPosY(y * 0.2);
    ImGui::Text("Username");
    if (ImGui::InputText("##username", b_us, 42, ImGuiInputTextFlags_EnterReturnsTrue)) {
        shouldRender = true;
    }
    ImGui::Text("Password");
    if (ImGui::InputText("##password", p, 20, ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue)) {
        shouldRender = true;
    }
    ImGui::SetCursorPosY(y * 0.4);
    if (ImGui::Button("Done", ImVec2(x * 0.195, y * 0.05))) {
        std::ofstream f("data.txt");
        if (f.is_open()) {
            f << std::string(b_us) + "-=S=-" + std::string(p) + "-=S=-" + "pc_name" + "-=S=-base-=S=-no";
            name = std::string(b_us);
            f.close();
            login = true;
            shouldRender = true;
        }
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

int main(int, char**) {
    if (server_connect() == 0) c = 1;
    std::ifstream f("data.txt");
    if (f.is_open()) {
        std::string l;
        std::getline(f, l);
        std::cout << l << std::endl;
        std::vector v = split(l, "-=S=-");
        name = v[0];
        f.close();
        login = true;
    }

    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << std::endl;
        msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
        shouldRender = true;
    }
    else {
        err = Pa_OpenDefaultStream(&stream, NUM_CHANNELS, NUM_CHANNELS, paInt16, SAMPLE_RATE, FRAMES_PER_BUFFER, audio_callback, nullptr);
        if (err != paNoError) {
            std::cerr << "PortAudio open error: " << Pa_GetErrorText(err) << std::endl;
            msgs.push_back("[ERR] _ERROR_" + std::string(Pa_GetErrorText(err)) + "_");
            shouldRender = true;
        }
        else {
            std::cout << "Audio stream initialized\n";
        }
    }

    std::thread receive_thread(receive_messages);
    receive_thread.detach();

    setlocale(LC_ALL, "RU");
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"ImGui Example", nullptr };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"ZIPcord DX10", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    float baseFontSize = 20.0f;
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf, font_ttf_size, baseFontSize, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = baseFontSize;
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf1, font_ttf_size1, baseFontSize, &icons_config, icons_ranges);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Border] = ImVec4(0.137f, 0.153f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.173f, 0.184f, 0.2f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.137f, 0.153f, 0.165f, 1.0f);
    style.PopupRounding = 10;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.Fonts->Build();
    msgs.push_back("[INF] Styles loaded");
    shouldRender = true;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX10_Init(g_pd3dDevice);

    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
            shouldRender = true;
        }
        if (done) break;

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
            shouldRender = true;
        }

        if (shouldRender) {
            ImGui_ImplDX10_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (login == false) {
                login_false(hwnd);
            }
            else {
                v_chat(hwnd, stream);
                if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_F3)) {
                    showwindow = !showwindow;
                    shouldRender = true;
                }
                if (showwindow) {
                    console(io);
                }
            }

            ImGui::Render();
            const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
            g_pd3dDevice->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
            g_pd3dDevice->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
            ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());

            HRESULT hr = g_pSwapChain->Present(1, 0);
            g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
            shouldRender = false;
        }
        else {
            Sleep(16);
        }
    }

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

bool CreateDeviceD3D(HWND hWnd) {
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
    HRESULT res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res == DXGI_ERROR_UNSUPPORTED) res = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, D3D10_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice);
    if (res != S_OK) return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D10Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam);
        g_ResizeHeight = (UINT)HIWORD(lParam);
        shouldRender = true;
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
