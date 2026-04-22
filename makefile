# 1. 编译器设置
CC = arm-buildroot-linux-gnueabihf-gcc

# 2. 目录定义
INC_DIR = inc
SRC_DIR = src
OBJ_DIR = build
BIN_DIR = bin

# 3. 编译选项
# -I$(INC_DIR): 包含头文件路径
CFLAGS = -O2 -Wall -I$(INC_DIR)

# 4. 链接选项
LDFLAGS = -lpthread

# 5. 目标程序名及路径
TARGET = $(BIN_DIR)/camera_udp_streamer

# 6. 源文件列表 (src 目录下的所有 .c)
SRCS = $(wildcard $(SRC_DIR)/*.c)

# 7. 目标文件列表 (将 src/*.c 映射为 build/*.o)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# --- 编译规则 ---

# 默认目标
all: $(TARGET)

# 链接阶段：将 build/*.o 链接成 bin/ 下的可执行文件
$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@echo "------------------------------------------------"
	@echo "Build Success!"
	@echo "Executable: $(TARGET)"
	@echo "------------------------------------------------"

# 编译阶段：将 src/*.c 编译成 build/*.o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# 创建目录（如果不存在）
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# 清理规则：删除 build 和 bin 文件夹
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Cleaned up build and bin directories."

.PHONY: all clean