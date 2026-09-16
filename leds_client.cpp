#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstdlib>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <netdb.h>

#define SERVER_PORT 5000

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Event event;
SDL_Texture* paletteTex;
SDL_Texture* brightnessBarTex;
SDL_Surface* palette;
SDL_Surface* brightnessBar;
bool running = 1;
int xCursor = 0;
int yCursor = 0;
int xColor = 250;
int yColor = 250;
int xLast, yLast;
float brightness = 1.0f;
uint64_t frameStart;
uint8_t frameTime = 1000/60;

using namespace std;

void update() {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);

    SDL_Rect src = {0, 0, 500, 500};
    SDL_Rect dst = {150, 150, 500, 500};
    SDL_RenderCopy(renderer, paletteTex, &src, &dst);

    src = {0, 0, 100, 500};
    dst = {700, 150, 100, 500};
    SDL_RenderCopy(renderer, brightnessBarTex, &src, &dst);

    SDL_Rect cursor = {xColor-5, yColor-5, 10, 10};
    SDL_RenderDrawRect(renderer, &cursor);
    
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    int y = 150+float(1.0-brightness)*500;
    SDL_RenderDrawLine(renderer, 700, y, 800, y);

    SDL_RenderPresent(renderer);
}

uint32_t hsv_to_rgb(float h, float s, float v) {
    h = h-floor(h);
    float r, g, b;
    float h6 = h*6.0f;
    int sector = static_cast<int>(h6);
    float f = h6-sector;
    float p = v*(1.0f-s);
    float q = v*(1.0f-s*f);
    float t = v*(1.0f-s*(1.0f-f));
    
    if (sector==0) { r = v; g = t; b = p; }
    else if (sector==1) { r = q; g = v; b = p; }
    else if (sector==2) { r = p; g = v; b = t; }
    else if (sector==3) { r = p; g = q; b = v; }
    else if (sector==4) { r = t; g = p; b = v; }
    else { r = v; g = p; b = q; }

    uint32_t R = static_cast<uint32_t>(r*255.0f);
    uint32_t G = static_cast<uint32_t>(g*255.0f);
    uint32_t B = static_cast<uint32_t>(b*255.0f);

    return (R<<24)|(G<<16)|(B<<8)|0xFF;
}

uint32_t calculate_color(uint16_t x, uint16_t y) {
    float dx = x-250; float dy = y-250;
    float distance = sqrt(dx*dx+dy*dy);
    if (distance<=250) {
        float angle = atan2(dy, dx);
        float hue = (angle+M_PI)/(2.0f*M_PI);
        float saturation = distance/250;
        float value = 1.0f;
        uint32_t color = hsv_to_rgb(hue, saturation, value);
        return color;
    } return 0x00000000;
}

void draw_palette() {
    SDL_LockSurface(palette);
    Uint32* pixelData = static_cast<Uint32*>(palette->pixels);
    int pitchPixels = palette->pitch/sizeof(Uint32);
    for (uint16_t y=0; y<500; y++) {
        for (uint16_t x=0; x<500; x++) { pixelData[y*500+x] = calculate_color(x, y); }
    } SDL_UnlockSurface(palette);
}

void draw_brightness_bar() {
    SDL_LockSurface(brightnessBar);
    Uint32* pixelData = static_cast<Uint32*>(brightnessBar->pixels);
    int pitchPixels = brightnessBar->pitch/sizeof(Uint32);

    for (uint16_t y=0; y<500; y++) {
        for (uint16_t x=0; x<100; x++) { 
            uint8_t value = uint8_t(((float)(500-y)/500.0f)*255.0);
            pixelData[y*100+x] = (value<<24)|(value<<16)|(value<<8)|0xFF;
        }
    } SDL_UnlockSurface(brightnessBar);
}

int main() {
    const char* raspberry_ip = "raspberrypi.local";
    system(string("ssh -i ~/.ssh/daeb_rsa_key viewport@"+string(raspberry_ip)+" \"leds > /dev/null 2>&1 &\"").c_str());
    this_thread::sleep_for(chrono::milliseconds(2000));

    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("LIGHTS", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 950, 800, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd<0) { perror("socket"); return 1; }
    
    struct addrinfo hints;
    struct addrinfo* result;

    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int status = getaddrinfo(raspberry_ip, nullptr, &hints, &result);

    if (status!=0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        close(socket_fd); return 1;
    }

    struct sockaddr_in* server = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
    server->sin_port = htons(SERVER_PORT);
    if (connect(socket_fd, result->ai_addr, result->ai_addrlen)<0) {perror("connect");
        freeaddrinfo(result);
        close(socket_fd); return 1;
    } freeaddrinfo(result);

    palette = SDL_CreateRGBSurfaceWithFormat(0, 500, 500, 32, SDL_PIXELFORMAT_RGBA32);
    if (!palette) { SDL_Log("ERROR: %s", SDL_GetError()); }
    draw_palette();
    
    brightnessBar = SDL_CreateRGBSurfaceWithFormat(0, 100, 500, 32, SDL_PIXELFORMAT_RGBA32);
    if (!brightnessBar) { SDL_Log("ERROR: %s", SDL_GetError()); }
    draw_brightness_bar();

    paletteTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 500, 500);
    if (!paletteTex) { SDL_Log("Error: %s", SDL_GetError()); }
    SDL_UpdateTexture(paletteTex, nullptr, palette->pixels, palette->pitch);

    brightnessBarTex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, 100, 500);
    if (!brightnessBarTex) { SDL_Log("Error: %s", SDL_GetError()); }
    SDL_UpdateTexture(brightnessBarTex, nullptr, brightnessBar->pixels, brightnessBar->pitch);

    while (running) {
        frameStart = SDL_GetTicks();

        update();
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) { running = 0; }
        } 
        
        uint32_t buttons = SDL_GetMouseState(&xCursor, &yCursor);
        if (xCursor==xColor && yCursor==yColor) { continue; } 
        int xFixed = xCursor-150; int yFixed = yCursor-150;
        if (buttons&SDL_BUTTON_LMASK && (xFixed>=0 && yFixed>=0) && (xFixed<=500 && yFixed<=500)) { 
            float dx = xFixed-250; float dy = yFixed-250;
            float distance = sqrt(dx*dx+dy*dy);
            if (distance<=250) { 
                xColor = xCursor; yColor = yCursor;
                uint32_t* pixelData = static_cast<Uint32*>(palette->pixels);
                int pitchPixels = palette->pitch/sizeof(Uint32);
                uint32_t color = pixelData[yFixed*pitchPixels+xFixed];
                uint8_t r = (color>>24)&0xFF; r = static_cast<uint8_t>((float)r*brightness);
                uint8_t g = (color>>16)&0xFF; g = static_cast<uint8_t>((float)g*brightness);
                uint8_t b = (color>>8)&0xFF; b = static_cast<uint8_t>((float)b*brightness);
                uint8_t packet[] { r, g, b };
                ssize_t sent = send(socket_fd, packet, sizeof(packet), 0);

                if (sent!=sizeof(packet)) { perror("send"); close(socket_fd); return 1; }
            }
        }

        if (xCursor==xLast && yCursor==yLast) { continue; } 
        xFixed = xCursor-700; yFixed = yCursor-150;
        if (buttons&SDL_BUTTON_LMASK && (xFixed>=0 && yFixed>=0) && (xFixed<=100 && yFixed<=500)) { 
            xLast = xCursor; yLast = yCursor;
            brightness = (float)(500-yFixed)/500.0f;
            uint32_t* pixelData = static_cast<Uint32*>(palette->pixels);
            int pitchPixels = palette->pitch/sizeof(Uint32);
            uint32_t color = pixelData[(yColor-150)*pitchPixels+(xColor-150)];
            uint8_t r = (color>>24)&0xFF; r = static_cast<uint8_t>((float)r*brightness);
            uint8_t g = (color>>16)&0xFF; g = static_cast<uint8_t>((float)g*brightness);
            uint8_t b = (color>>8)&0xFF; b = static_cast<uint8_t>((float)b*brightness);
            uint8_t packet[] { r, g, b };
            ssize_t sent = send(socket_fd, packet, sizeof(packet), 0);

            if (sent!=sizeof(packet)) { perror("send"); close(socket_fd); return 1; }
        }

        uint64_t timeElapsed = SDL_GetTicks()-frameStart;
        if (timeElapsed<frameTime) { SDL_Delay(frameTime-timeElapsed); }
    }

    close(socket_fd);
    SDL_DestroyWindow(window);
    SDL_DestroyRenderer(renderer);
    SDL_Quit();
    return 0;
}

