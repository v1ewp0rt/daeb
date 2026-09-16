#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#define PORT 5000
#define SERIAL_DEVICE "/dev/ttyACM0" 
#define SERIAL_BAUD B9600

bool recv_all(int socket, void* data, size_t size) { 
    uint8_t* buffer = static_cast<uint8_t*>(data); 
    size_t received = 0; 
    while (received<size) { 
        ssize_t result = recv(socket, buffer+received, size-received, 0); 
        if (result<=0) { return 0; } 
        received += static_cast<size_t>(result); 
    } return 1; 
}

int main() {
    bool running = 1;
    int serial = open(SERIAL_DEVICE, O_RDWR|O_NOCTTY);
    if (serial<0) { printf("FAILED OPENING /dev/ttyACM0\n"); return 1; }
    termios tty{};

    if (tcgetattr(serial, &tty)!=0) {
        printf("FAILED GETTING CONFIGURATION\n");
        close(serial); return 1;
    }

    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD|CLOCAL;
    tty.c_lflag &= ~(ICANON|ECHO|ECHOE|ISIG);
    tty.c_iflag &= ~(IXON|IXOFF|IXANY);
    tty.c_oflag &= ~OPOST;
    tcsetattr(serial, TCSANOW, &tty);

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket<0) { perror("socket"); return 1; }
    int option = 1;

    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    sockaddr_in address{};
    std::memset(&address, 0, sizeof(address));
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address))<0) { perror("bind"); close(server_socket); return 1; }
    if (listen(server_socket, 1)<0) { perror("listen"); close(server_socket); return 1; }

    printf("SERVER LISTENING IN PORT %d...\n", PORT);
    sockaddr_in client_address{};
    socklen_t client_length = sizeof(client_address);

    int client_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_address), &client_length);
    if (client_socket<0) { perror("accept"); close(server_socket); return 1; } 

    while (running) { 
        struct sockaddr_in client_address; 
        socklen_t client_length = sizeof(client_address); 
        int client = accept(server_socket, reinterpret_cast<struct sockaddr*>(&client_address), &client_length); 
        if (client<0) { perror("accept"); continue; } 
        printf("CLIENT CONNECTED\n"); 
        while (1) { 
            uint8_t packet[3]; 
            if (!recv_all(client, packet, sizeof(packet))) { printf("Client disconnected\n"); running = 0; break; }
            ssize_t written = write(serial, packet, sizeof(packet)); 
            if (written!=sizeof(packet)) { perror("write serial"); running = 0; break; } 
        } close(client); 
    } 
    
    close(server_socket); 
    close(serial); 
    return 0;
}