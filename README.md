```
g++ leds_client.cpp -lSDL2 -o leds
g++ cam_client.cpp -o vision -I/usr/include/opencv4 -L/usr/lib64 -lSDL2 -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -pthread
g++ vault.cpp -o vault -lssh2 -lncurses
```

```
g++ leds_server.cpp -o leds
g++ cam_server.cpp -o vision -I/usr/include/opencv4 -llgpio -lopencv_videoio -lopencv_imgproc -lopencv_imgcodecs -lopencv_core -pthread
```
