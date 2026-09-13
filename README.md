g++ leds_client.cpp -lSDL2 -o leds_client
g++ cam_client.cpp -o cam_client -I/usr/include/opencv4 -L/usr/lib64 -lSDL2 -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread

g++ leds_server.cpp -o leds_server
g++ cam_server.cpp -o cam_server -I/usr/include/opencv4 -llgpio -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -lopencv_core -pthread
