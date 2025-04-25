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
#include "libs/IconsFontAwesome5.h"
#include <portaudio.h>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include "libs/font1.h" // roboto
#include "libs/font2.h" // fa-solid-900
#include <fstream>
#include <sstream>
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <mutex>
#include <commdlg.h>
#include <map>
#define MAX_BUFFER_SIZE 1048576 // 1MB

using json = nlohmann::json;

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "portaudio.lib")
#pragma comment(lib, "comdlg32.lib")

#define SAMPLE_RATE 48000
#define FRAMES_PER_BUFFER 1024
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
char buf[2048] = "";
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
int id_message = 0;
bool login = false;
bool settings = false;
std::string theme_gl;
std::string passw;
bool custom_settings_theme = false;

struct Message {
    std::string text;
    std::string sender;
    std::string time;
    int id;
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
                        int id = received_json["id"];
                        chat_msgs.push_back({ message, sender, time, id });
                        msgs.push_back("[INF] Received chat message from " + sender + ": " + message);
                        /*std::ofstream file2f("chat.txt", std::ios::app);
                        if (file2f.is_open()) {
                            file2f << id_message - 1 << "(-=S=-)-=S=-(-=S=-)" << sender << "(-=S=-)-=S=-(-=S=-)" << time << "(-=S=-)-=S=-(-=S=-)doc(-=S=-)-=S=-(-=S=-)" << message << std::endl;
                            file2f.close();
                        }*/
                        shouldRender = true;
                    }
                    else if (type == "voice_connect") {
                        if (received_json["message"] == 1) {
                            msgs.push_back("[INF] Connected to voice chat");
                            shouldRender = true;
                        }
                    }
                    else if (type == "get_voice_clients") {
                        users_in_voice.clear();
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
                    else if (type == "id_msg_last") {
                        id_message = int(received_json["last_id"]);
                    }
                    else if (type == "add_user") {
                        if (received_json["message"] == "ok") {
                            login = true;
                        }
                    }
                    else if (type == "check_user") {
                        if (received_json["message"] == "ok") {
                            login = true;
                        }
                    }
                    else if (type == "old_msgs") {
                        for (json msg : received_json["messages"]) {
                            std::string sender = msg["username"];
                            std::string message = msg["message"];
                            std::string time = msg["time"];
                            int id = msg["id"];
                            chat_msgs.push_back({ message, sender, time, id });
                            msgs.push_back("[INF] Received chat message from " + sender + ": " + message);
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

bool server_connect() {
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    server_addr.sin_port = htons(412);

    msgs.push_back("[INF] init");
    shouldRender = true;
    if (connect(client_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Connect failed_");
        c = 0;
        shouldRender = true;
        return false;
    }
    else c = 1;
    shouldRender = true;
    return true;
}

void DrawMessageBox(const std::string& sender, const std::string& time, const std::string& text, HWND hwnd) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.137f, 0.153f, 0.165f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
    std::string header = sender + "  " + time;
    ImVec2 hSize = ImGui::CalcTextSize(header.c_str(), nullptr, false, x * 0.54f);
    ImVec2 textSize = ImGui::CalcTextSize(text.c_str(), nullptr, false, x * 0.54f);
    float minWidth = 100.0f;
    float maxWidth = x * 0.58f;
    float boxWidth;
    if (hSize.x >= textSize.x) {
        boxWidth = hSize.x + 20;
    }
    else {
        boxWidth = textSize.x + 20;
    }

    float boxHeight = textSize.y + 50.0f;
    ++id_message;
    std::string s = std::to_string(id_message);
    ImGui::BeginChild(s.c_str(), ImVec2(boxWidth, boxHeight), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%s", header.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}
/*bool LoadTextureFromFile(const char* filename, ID3D10Device* device, ID3D10ShaderResourceView** out_srv, int* out_width, int* out_height) {
    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(filename, &image_width, &image_height, NULL, 4);
    if (image_data == NULL)
        return false;
    D3D10_TEXTURE2D_DESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.Width = image_width;
    desc.Height = image_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D10_USAGE_DEFAULT;
    desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    ID3D10Texture2D* pTexture = NULL;
    D3D10_SUBRESOURCE_DATA subResource;
    subResource.pSysMem = image_data;
    subResource.SysMemPitch = desc.Width * 4;
    subResource.SysMemSlicePitch = 0;
    device->CreateTexture2D(&desc, &subResource, &pTexture);
    D3D10_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D10_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.MostDetailedMip = 0;
    device->CreateShaderResourceView(pTexture, &srvDesc, out_srv);
    pTexture->Release();
    stbi_image_free(image_data);
    *out_width = image_width;
    *out_height = image_height;

    return true;
}*/

char bb[42];
char bbb[20];
float* test;

std::map<std::string, ImVec4> th_da = { {"btn", ImVec4(0.322, 0.322, 0.322, 1.0f)}, {"window", ImVec4(0.173f, 0.184f, 0.2f, 1.0f)}, {"title", ImVec4(0.137f, 0.153f, 0.165f, 1.0f)}, {"border", ImVec4(0.137f, 0.153f, 0.165f, 1.0f)}, {"inputtext", ImVec4(0.322, 0.322, 0.322, 1.0)} }; // fix
std::map<std::string, ImVec4> th_de = { {"btn", ImVec4(0.322, 0.322, 0.322, 1.0f)}, {"window", ImVec4(0.173f, 0.184f, 0.2f, 1.0f)}, {"title", ImVec4(0.137f, 0.153f, 0.165f, 1.0f)}, {"border", ImVec4(0.137f, 0.153f, 0.165f, 1.0f)}, {"inputtext", ImVec4(0.322, 0.322, 0.322, 1.0)} };
std::map<std::string, ImVec4> th_li = { {"btn", ImVec4(0.941, 0.941, 0.941, 1.0f)}, {"window", ImVec4(1, 1, 1, 1.0f)}, {"title", ImVec4(1, 1, 1, 1.0f)}, {"border", ImVec4(0.984, 0.973, 1, 1.0f)}, {"inputtext", ImVec4(0.82, 0.82, 0.82, 1.0)}, {"text", ImVec4(0.0, 0.0, 0.0, 1.0f)} };
std::map<std::string, ImVec4> th_x_ = { {"btn", ImVec4(0.302, 0.302, 0.302, 1.0f)}, {"window", ImVec4(0.0, 0.0, 0.0, 1.0f)}, {"title", ImVec4(0.271, 0.271, 0.271, 1.0f)}, {"border", ImVec4(1.0, 1.0, 1.0, 1.0f)}, {"inputtext", ImVec4(0.137, 0.161, 0.188, 1.0)}, {"text", ImVec4(0.0, 0.0, 0.0, 1.0f)}, {"btn_li", ImVec4(0.878, 0.878, 0.878, 1.0)} };
std::map<std::string, ImVec4> th_rb = { {"btn", ImVec4(0.929, 0, 1, 1.0f)}, {"window", ImVec4(1, 0.325, 0.918, 1.0f)}, {"title", ImVec4(0.984, 0.757, 1, 1.0f)}, {"border", ImVec4(1, 0, 0.878, 1.0f)}, {"inputtext", ImVec4(0.788, 0.024, 0.718, 1.0)} };
std::map<std::string, ImVec4> th_pu = { {"btn", ImVec4(0.518, 0.271, 0.91, 1.0f)}, {"window", ImVec4(0.455, 0, 0.62, 1.0f)}, {"title", ImVec4(0.827, 0.518, 0.941, 1.0f)}, {"border", ImVec4(0.392, 0.18, 0.471, 1.0f)}, {"inputtext", ImVec4(0.482, 0.255, 0.678, 1.0)} };

void set_theme() {
    std::string tm;
    std::ifstream f("data.txt");
    if (f.is_open()) {
        std::string l;
        std::getline(f, l);
        std::cout << l << std::endl;
        std::vector v = split(l, delimiter);
        name = v[0];
        theme_gl = v[3];
        tm = theme_gl;
        passw = v[1];
        f.close();
    }

    ImGuiStyle& style = ImGui::GetStyle();
    if (tm == "default") {
        style.Colors[ImGuiCol_Border] = th_de["border"];
        style.Colors[ImGuiCol_WindowBg] = th_de["window"];
        style.Colors[ImGuiCol_Button] = th_de["btn"];
        style.Colors[ImGuiCol_FrameBg] = th_de["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_de["title"];
        style.Colors[ImGuiCol_ButtonHovered] = th_x_["btn_li"];
        style.Colors[ImGuiCol_ButtonActive] = th_x_["btn_li"];
        std::cout << "Theme set to default" << std::endl;
    }
    else if (tm == "x") {
        style.Colors[ImGuiCol_Border] = th_x_["border"];
        style.Colors[ImGuiCol_WindowBg] = th_x_["window"];
        style.Colors[ImGuiCol_FrameBg] = th_x_["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_x_["title"];
        style.Colors[ImGuiCol_Button] = th_x_["btn"];
        style.Colors[ImGuiCol_ButtonHovered] = th_x_["btn_li"];
        style.Colors[ImGuiCol_ButtonActive] = th_x_["btn_li"];
        std::cout << "Theme set to x" << std::endl;
    }
    else if (tm == "rb") {
        style.Colors[ImGuiCol_Border] = th_rb["border"];
        style.Colors[ImGuiCol_WindowBg] = th_rb["window"];
        style.Colors[ImGuiCol_FrameBg] = th_rb["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_rb["title"];
        style.Colors[ImGuiCol_Button] = th_rb["btn"];

        std::cout << "Theme set to rb" << std::endl;
    }
    else if (tm == "light") {
        style.Colors[ImGuiCol_Border] = th_li["border"];
        style.Colors[ImGuiCol_WindowBg] = th_li["window"];
        style.Colors[ImGuiCol_FrameBg] = th_li["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_li["title"];
        style.Colors[ImGuiCol_Button] = th_li["btn"];
        style.Colors[ImGuiCol_Text] = th_li["text"];
        std::cout << "Theme set to light" << std::endl;
    }
    else if (tm == "dark") {
        style.Colors[ImGuiCol_Border] = th_da["border"];
        style.Colors[ImGuiCol_WindowBg] = th_da["window"];
        style.Colors[ImGuiCol_FrameBg] = th_da["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_da["title"];
        style.Colors[ImGuiCol_Button] = th_da["btn"];
        std::cout << "Theme set to dark" << std::endl;
    }
    else if (tm == "purple") {
        style.Colors[ImGuiCol_Border] = th_pu["border"];
        style.Colors[ImGuiCol_WindowBg] = th_pu["window"];
        style.Colors[ImGuiCol_FrameBg] = th_pu["inputtext"];
        style.Colors[ImGuiCol_TitleBgActive] = th_pu["title"];
        style.Colors[ImGuiCol_Button] = th_pu["btn"];
        std::cout << "Theme set to purple" << std::endl;
    }
    shouldRender = true;
}

void settings_(HWND& hwnd) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
    ImGui::SetWindowSize(ImVec2(x * 0.32, y * 0.7));
    if (name == "" || name == " ") {
        ImGui::Text("Uzbechische");
    }
    else {
        ImGui::Text(name.c_str());
    }
    ImGui::Text("Username");
    ImGui::InputText("##username", bb, 42, 0);
    ImGui::Text("Password");
    ImGui::InputText("##password", bbb, 20, 0);
    ImGui::Separator();
    ImGui::Text("Sellect app style");
    ImGui::Text("Now: %s", theme_gl.c_str());

    if (ImGui::Button("Default", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "default";
    }
    ImGui::SameLine();
    if (ImGui::Button("Dark", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "dark";
    }
    if (ImGui::Button("Light", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "light";
    }
    ImGui::SameLine();
    if (ImGui::Button("Twitter style", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "x";
    }
    if (ImGui::Button("Roblox", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "rb";
    }
    ImGui::SameLine();
    if (ImGui::Button("Purple", ImVec2(x * 0.15, y * 0.05))) {
        theme_gl = "purple";
    }
    if (ImGui::Button("Custom", ImVec2(x * 0.306, y * 0.05))) {
        shouldRender = true;
    }

    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - y * 0.06);
    if (ImGui::Button("Save", ImVec2(x * 0.30, y * 0.05))) {
        std::ofstream f("data.txt");
        if (f.is_open()) {
            if (std::string(bbb) != "" && std::string(bbb) != " ") {
                passw = std::string(bbb);
            }
            if (std::string(bb) != "" && std::string(bb) != " ") {
                name = std::string(bb);
            }
            f << name + delimiter + passw + delimiter + "pc_name" + delimiter + theme_gl + delimiter + "no";
            shouldRender = true;
            f.close();
            set_theme();
        }
    }
    ImGui::End();
}

void v_chat(HWND& hwnd, PaStream* stream) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImVec2 windowSize = ImVec2(x, y);
    ImGui::Begin("Voice chat", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
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
                if (server_connect()) {
                    c = 1;
                    shouldRender = true;
                }
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
                json request1 = { {"type", "get_voice_clients"} };
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
            ImGui::SetCursorPos(ImVec2((x * 0.35f) / 3, (y / 2) + y * 0.05 + 10));
            if (ImGui::Button("Settings", ImVec2(x * 0.06, y * 0.05))) {
                settings = !settings;
            }
            if (settings == true) {
                settings_(hwnd);
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
            json request = { {"type", "disconnect"}, {"message", name} };
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

    ImGui::End();
    ImGui::Begin("Chat", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetWindowSize(ImVec2(windowSize.x * 0.65f, windowSize.y - 40), ImGuiCond_Always);
    ImGui::SetWindowPos(ImVec2(windowSize.x * 0.35f, 0));
    if (log_inf2 == false) {
        msgs.push_back("[INF] Init 2");
        log_inf2 = true;
        shouldRender = true;
    }
    ImGui::BeginChild("ChatScroll", ImVec2(0, windowSize.y - 100), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (!chat_msgs.empty()) {
        for (const auto& msg : chat_msgs) {
            DrawMessageBox(msg.sender, msg.time, msg.text, hwnd);
            //if (msg.isImage && msg.media) {
            //    ImGui::Image((ImTextureID)msg.media, ImVec2(100, 100));
            //}
            //else if (msg.isVideo && msg.media) {
            //    ImGui::Text("[Video Frame]");
            //}
        }
        if (ImGui::GetScrollY() < ImGui::GetScrollMaxY()) autoScroll = false;
        if (autoScroll) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::SetCursorPosY(windowSize.y - 80);
    ImGui::PushItemWidth(-70);
    if (ImGui::InputText("##Input", buf, 2048, ImGuiInputTextFlags_EnterReturnsTrue)) {
        if (std::string(buf) != "" && std::string(buf) != " ") {
            SYSTEMTIME st;
            GetSystemTime(&st);
            char t[80] = "";
            sprintf_s(t, "%d:%d", st.wHour + 3, st.wMinute);
            json request = { {"type", "chat_msg"}, {"username", name}, {"message", std::string(buf)}, {"time", std::string(t)} };
            send(client_socket, request.dump().c_str(), request.dump().size(), 0);
            ++id_message;
            chat_msgs.push_back({ std::string(buf), name, std::string(t), id_message });
            msgs.push_back("[INF] Message sent: " + std::string(buf));
            /*std::ofstream file2f("chat.txt", std::ios::app);
            if (file2f.is_open()) {
                file2f << id_message - 1 << "(-=S=-)-=S=-(-=S=-)" << name << "(-=S=-)-=S=-(-=S=-)" << std::string(t) << "(-=S=-)-=S=-(-=S=-)doc(-=S=-)-=S=-(-=S=-)" << std::string(buf) << std::endl;
                file2f.close();
            }*/
            buf[0] = '\0';
            autoScroll = true;
            ImGui::SetKeyboardFocusHere(-1);
            shouldRender = true;
        }
    }
    ImGui::SetItemDefaultFocus();
    ImGui::PopItemWidth();
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

/*ID3D10ShaderResourceView* CreateTextureFromMemory(ID3D10Device* device, const unsigned char* image_data, int image_size) {
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
}*/

void login_false(HWND& hwnd) {
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.137f, 0.153f, 0.165f, 1.0f));
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
            f << std::string(b_us) + "-=S=-" + std::string(p) + "-=S=-pc_name-=S=-base-=S=-no";
            name = std::string(b_us);
            f.close();
            login = true;
            shouldRender = true;
        }
        json r = { {"type", "add_user"}, {"username", std::string(b_us)}, {"password", std::string(p)} };
        send(client_socket, r.dump().c_str(), r.dump().size(), 0);
    }
    ImGui::PopStyleColor();
    ImGui::End();
}

int main(int, char**) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_WSAStartup failed_");
        c = 0;
        shouldRender = true;
    }

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        msgs.push_back("[ERR] _ERROR_Socket creation failed_");
        WSACleanup();
        c = 0;
        shouldRender = true;
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
        c = 0;
        shouldRender = true;
    }
    c = 1;
    shouldRender = true;

    std::ifstream f("data.txt");
    if (f.is_open()) {
        std::string l;
        std::getline(f, l);
        std::cout << l << std::endl;
        std::vector v = split(l, delimiter);
        name = v[0];
        theme_gl = v[3];
        passw = v[1];
        //r = { {"type", "check_user"}, {"username", name}, {"password", v[1]}};
        //send(client_socket, r.dump().c_str(), r.dump().size(), 0);
        login = true;
        f.close();
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

    //::ShowWindow(GetConsoleWindow(), SW_HIDE); отключает консоль
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    json r = { {"type", "id_msg_last"} };
    send(client_socket, r.dump().c_str(), r.dump().size(), 0);
    r = { {"type", "get_msgs"}, {"message", "0-=S=-1000"} };

    std::ifstream filef("chat.txt");
    if (filef.is_open()) {
        std::string line;
        while (std::getline(filef, line)) {
            size_t pos = 0;
            std::vector<std::string> parts;
            while ((pos = line.find("(-=S=-)-=S=-(-=S=-)")) != std::string::npos) {
                parts.push_back(line.substr(0, pos));
                line.erase(0, pos + 19);
            }
            parts.push_back(line);
            if (parts.size() == 4) {
                int id = std::stoi(parts[0]);
                std::string sender = parts[1];
                std::string time = parts[2];
                std::string text = parts[3];
                id_message = id;
                Message m;
                m.sender = sender;
                m.time = time;
                m.text = text;
                m.id = id;
                chat_msgs.push_back(m);
            }
            else {
                printf("error");
            }
            /*else if (parts.size() >= 5) {
                int id = std::stoi(parts[0]);
                Message m;
                m.sender = parts[1];
                m.time = parts[2];
                m.text = parts[4];
                m.id = id;
                if (parts[3] == "doc") {
                    m.doc = (const unsigned char*)parts[5].c_str();
                    m.is_doc = true;
                }
                else if (parts[3] == "img") {
                    ID3D10ShaderResourceView* t1 = CreateTextureFromMemory(g_pd3dDevice, base64_decode(parts[5]), std::stoull(parts[7]));
                    m.texture = t1;
                    m.is_image = true;
                    m.width = 350;
                    m.height = 200;
                }*/
        }
        filef.close();
    }
    else {
        std::cerr << "Failed to open chat_history.txt for reading!" << std::endl;
    }


    //send(client_socket, r.dump().c_str(), r.dump().size(), 0); // uncomment that if u want to get all messages from 0 to 1000 u can edit it in line 906 receive_messages doesnt realy process messages, so, its unhelpful and no i dont need it, but want to fix it

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    Dimensions d = getWS(hwnd);
    float x = d.width;
    float y = d.height;
    float baseFontSize = x * 0.015;
    io.Fonts->Clear();
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf, font_ttf_size, baseFontSize, nullptr, io.Fonts->GetGlyphRangesCyrillic());
    static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    icons_config.GlyphMinAdvanceX = baseFontSize;
    io.Fonts->AddFontFromMemoryTTF((void*)font_ttf1, font_ttf_size1, baseFontSize, &icons_config, icons_ranges);

    ImGuiStyle& style = ImGui::GetStyle();
    set_theme();
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

    std::ofstream fff("data.txt");
    if (fff.is_open()) {
        fff << name << "-=S=-" << passw << "-=S=-pc_name-=S=-" << theme_gl << "-=S=-no-=S=-" << std::to_string(id_message);
        fff.close();
    }

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
