#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

// 对应 GPIO4_19, 20, 21, 22
int pins[] = {115, 116, 117, 118};

void write_gpio(int pin, int val) {
    char path[64];
    sprintf(path, "/sys/class/gpio/gpio%d/value", pin);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, val ? "1" : "0", 1);
        close(fd);
    }
}

int main() {
    // 假设你已经手动执行了 export 和 direction out
    printf("Starting Motor Test (Press Ctrl+C to stop)...\n");

    // 四相四拍时序：A -> B -> C -> D
    int seq[4][4] = {
        {1, 0, 0, 0},
        {0, 1, 0, 0},
        {0, 0, 1, 0},
        {0, 0, 0, 1}
    };

    while(1) {
        for (int step = 0; step < 4; step++) {
            for (int i = 0; i < 4; i++) {
                write_gpio(pins[i], seq[step][i]);
            }
            usleep(10000); // 增加延时到 10ms (10000us) 观察是否有震动
        }
    }
    return 0;
}
