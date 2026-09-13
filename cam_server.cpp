#include <opencv2/opencv.hpp>

#include <lgpio.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#define PORT 5000
#define HORIZONTAL 18
#define VERTICAL 19
#define STEP 2
#define TIMEOUT_MS 100


bool send_all(int socket, const void* data, size_t size) {
    const char* buffer = static_cast<const char*>(data);
    while (size>0) {
        ssize_t sent = send(socket, buffer, size, 0);
        if (sent<=0) { return 0; }
        buffer += sent;
        size -= sent;
    } return 1;
}

int angle_to_pulse(int angle) { return 500+(angle*2000)/180; }

void start_servo(int handle, int gpio, int angle) {
    int pulse = angle_to_pulse(angle);
    lgTxPulse(handle, gpio, pulse, 20000-pulse, 0, 0);
}

void stop_servo(int handle, int gpio) { lgTxPulse(handle, gpio, 0, 0, 0, 0); }

void servo_control(int socket, int gpio_handle, std::atomic<bool>& running) {
    int horizontal = 90;
    int vertical = 180;
    bool horizontal_active = false;
    bool vertical_active = false;
    auto last_command = std::chrono::steady_clock::now();
    while (running) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT_MS*1000;

        int result = select(socket+1, &read_set, nullptr, nullptr, &timeout);
        if (result>0 && FD_ISSET(socket, &read_set)) {
            char command;

            ssize_t received = recv(socket, &command, 1, 0);
            if (received<=0) {
                printf("CLIENT DISCONNECTED\n");
                running = false; break;
            }

            if (command=='w' || command=='W') {
                vertical += STEP;
                vertical = vertical>180?180:vertical;    
                start_servo(gpio_handle, VERTICAL, vertical);
                vertical_active = 1;
            } else if (command=='s' || command=='S') {
                vertical -= STEP;
                vertical = vertical<0?0:vertical;
                start_servo(gpio_handle, VERTICAL, vertical);
                vertical_active = 1;
            } else if (command=='a' || command=='A') {
                horizontal += STEP;
                horizontal = horizontal>180:180:horizontal;
                start_servo(gpio_handle, HORIZONTAL, horizontal);
                horizontal_active = true;
            } else if (command=='d' || command=='D') {
                horizontal -= STEP;
                horizontal = horizontal<0?0:horizontal;
                start_servo(gpio_handle, HORIZONTAL, horizontal);
                horizontal_active = 1;
            } last_command = std::chrono::steady_clock::now();
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now-last_command).count();
        if (elapsed>=TIMEOUT_MS) {
            if (horizontal_active) {
                stop_servo(gpio_handle, HORIZONTAL);
                horizontal_active = 0;
            } if (vertical_active) {
                stop_servo(gpio_handle, VERTICAL);
                vertical_active = 0;
            }
        }
    }

    stop_servo(gpio_handle, HORIZONTAL);
    stop_servo(gpio_handle, VERTICAL);
}

int main() {
    int gpio_handle = lgGpiochipOpen(0);
    if (gpio_handle<0) { printf("FAILED TO OPEN GPIO\n"); return 1; }

    if (lgGpioClaimOutput(gpio_handle, 0, HORIZONTAL, 0)<0) {
        printf("FAILED TO CLAIM GPIO %d\n", HORIZONTAL);
        lgGpiochipClose(gpio_handle); return 1;
    }

    if (lgGpioClaimOutput(gpio_handle, 0, VERTICAL, 0)<0) {
        printf("FAILED TO CLAIM GPIO %d\n", VERTICAL);
        lgGpiochipClose(gpio_handle); return 1;
    }
    
    cv::VideoCapture camera(0);
    if (!camera.isOpened()) {
        printf("FAILED OPENING WEBCAM\n");
        lgGpiochipClose(gpio_handle); return 1;
    }

    camera.set(cv::CAP_PROP_FRAME_WIDTH, 640);
    camera.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
    camera.set(cv::CAP_PROP_FPS, 30);


    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket<0) { perror("socket"); lgGpiochipClose(gpio_handle); return 1; }
    int option = 1;

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))<0) {
        perror("bind"); close(server_socket);
        lgGpiochipClose(gpio_handle); return 1;
    }

    if (listen(server_socket, 1)<0) {
        perror("listen"); close(server_socket);
        lgGpiochipClose(gpio_handle); return 1;
    }


    printf("SERVER LISTENING IN PORT %d...\n", PORT);
    sockaddr_in client_address{};
    socklen_t client_length = sizeof(client_address);

    int client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_address), &client_length);
    if (client_socket<0) {
        perror("accept"); close(server_socket);
        lgGpiochipClose(gpio_handle); return 1;
    } printf("CLIENT CONNECTED\n");


    std::atomic<bool> running(1);
    std::thread control_thread(servo_control, client_socket, gpio_handle, std::ref(running));
    cv::Mat frame;
    std::vector<uchar> jpeg;
    std::vector<int> parameters = {cv::IMWRITE_JPEG_QUALITY, 80};

    while (running) {
        camera >> frame;
        if (frame.empty()) { continue; }
        jpeg.clear();
        if (!cv::imencode(".jpg", frame, jpeg, parameters)) { continue; }

        uint32_t size = static_cast<uint32_t>(jpeg.size());
        uint32_t network_size = htonl(size);

        if (!send_all(client_socket, &network_size, sizeof(network_size))) {
            printf("CLIENT DISCONNECTED\n");
            running = 0; break;
        }

        if (!send_all(client_socket, jpeg.data(), jpeg.size())) {
            printf("CLIENT DISCONNECTED\n");
            running = 0; break;
        }
    }

    shutdown(client_socket, SHUT_RDWR);
    close(client_socket);

    if (control_thread.joinable()) { control_thread.join(); }
    close(server_socket);
    lgGpiochipClose(gpio_handle);
    return 0;
}