CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE -g
LDFLAGS = -lpthread

SRCS = http_server.c config.c classify.c route.c http_client.c jsmn.c debug_log.c
OBJS = $(SRCS:.c=.o)
TARGET = model_router

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "✅ 编译完成: ./$(TARGET)"
	@echo "   运行: ./$(TARGET) [port]"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

# 调试构建
debug: CFLAGS += -DDEBUG -O0 -g3
debug: clean all

# 内存检测
valgrind: $(TARGET)
	valgrind --leak-check=full ./$(TARGET)
