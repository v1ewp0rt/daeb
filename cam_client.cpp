#include <SDL2/SDL.h>
#include <opencv2/opencv.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#define PORT 5000

bool recv_all(int socket, void* data, size_t size) {
    char* buffer = static_cast<char*>(data);
    while (size>0) {
        ssize_t received = recv(socket, buffer, size, 0);
        if (received<=0) { return 0; }
        buffer += received;
        size -= received;
    } return 1;
}

bool send_command(int socket, char command) { 
    ssize_t sent = send(socket, &command, 1, 0); 
    return sent==1; 
}

void receive_frames(int socket, cv::Mat& frame, std::mutex& frame_mutex, bool& connected) {
    while (connected) {
        uint32_t network_size = 0;
        if (!recv_all(socket, &network_size, sizeof(network_size))) { connected = 0; break; }
        uint32_t jpeg_size = ntohl(network_size);
        if (jpeg_size==0 || jpeg_size>10*1024*1024) {
            printf("INVALID FRAME SIZE: %u\n", jpeg_size);
            connected = 0; break;
        }

        std::vector<uchar> jpeg(jpeg_size);
        if (!recv_all(socket, jpeg.data(), jpeg.size())) { connected = 0; break; }
        cv::Mat decoded = cv::imdecode(jpeg, cv::IMREAD_COLOR);
        if (decoded.empty()) { continue; }
        { std::lock_guard<std::mutex> lock(frame_mutex); frame = decoded; }
    }
}

int main(int argc, char** argv) {
    if (argc!=2) {
        printf("USAGE: %s <IP>\n", argv[0]);
        return 1;
    }

    const char* raspberry_ip = argv[1];

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd<0) { perror("socket"); return 1; }
    
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);

    if (inet_pton(AF_INET, raspberry_ip, &server_address.sin_addr)<=0) {
        printf("INVALID IP ADDRESS: %s\n", raspberry_ip);
        close(socket_fd); return 1;
    } printf("CONNECTING TO %s:%d...\n", raspberry_ip, PORT);

    if (connect(socket_fd, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address))<0) {
        perror("connect"); close(socket_fd); return 1;
    } printf("CONNECTED\n");

    if (SDL_Init(SDL_INIT_VIDEO)!=0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        close(socket_fd); return 1;
    }

    SDL_Window* window = SDL_CreateWindow("DAEB VISION", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600, SDL_WINDOW_SHOWN);

    if (!window) {
        printf("SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit(); close(socket_fd); return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        printf("SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit();
        close(socket_fd); return 1;
    }

    cv::Mat frame;
    std::mutex frame_mutex;
    bool connected = true;
    std::thread network_thread(receive_frames, socket_fd, std::ref(frame), std::ref(frame_mutex), std::ref(connected));
    SDL_Texture* texture = nullptr;
    bool running = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) { running = 0; }
            if (event.type==SDL_KEYDOWN) {
                if (event.key.keysym.sym==SDLK_ESCAPE) { running = 0; }
                char command = 0;
                if (event.key.keysym.sym==SDLK_w) { command = 'W'; }
                if (event.key.keysym.sym==SDLK_s) { command = 'S'; }
                if (event.key.keysym.sym==SDLK_a) { command = 'A'; }
                if (event.key.keysym.sym==SDLK_d) { command = 'D'; }

                if (command!=0) { 
                    if (!send_command( socket_fd, command)) { 
                        printf( "FAILED TO SEND COMMAND\n" ); connected = 0; running = 0; 
                    } 
                }
            }
        }

        cv::Mat current_frame;
        { std::lock_guard<std::mutex> lock(frame_mutex);
            if (!frame.empty()) { current_frame = frame.clone(); }
        }

        if (!current_frame.empty()) {
            if (!texture || SDL_QueryTexture(texture, nullptr, nullptr, nullptr, nullptr)!=0) {
                if (texture) { SDL_DestroyTexture(texture); }
                texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, current_frame.cols, current_frame.rows);
            }

            SDL_UpdateTexture(texture, nullptr, current_frame.data, static_cast<int>(current_frame.step));
            SDL_RenderClear(renderer);

            SDL_Rect srcRect = {0, 0, 640, 480};
            SDL_Rect dstRect = {0, 0, 800, 600};

            SDL_RenderCopy(renderer, texture, &srcRect, &dstRect);
            SDL_RenderPresent(renderer);
        } SDL_Delay(1);
    }

    connected = 0;
    shutdown(socket_fd, SHUT_RDWR);
    if (network_thread.joinable()) { network_thread.join(); }
    if (texture) { SDL_DestroyTexture(texture); }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    close(socket_fd);
    return 0;
}