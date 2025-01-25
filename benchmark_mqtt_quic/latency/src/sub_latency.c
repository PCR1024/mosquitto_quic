#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <cjson/cJSON.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h> 
#else
#include <sys/stat.h> 
#include <unistd.h>   
#endif
#include <errno.h>

#define HOST "localhost"
#define PORT  1883
#define KEEP_ALIVE 60
#define MSG_MAX_SIZE  512

static int running = 1;

/* 确认连接回调函数 */
void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    if(rc){
        printf("on_connect error!\n");
        exit(1);
    }
    else{
        if(mosquitto_subscribe(mosq, NULL, "test_signal", 2)){
            printf("Set the topic error!\n");
            exit(1);
        }
        printf("subscribe success");
    }
}

void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
    bool have_subscription = false;

    /* 在本例中，我们一次只订阅一个主题，但是订阅可以一次包含多个主题，因此这是检查所有主题的一种方法。*/
    for(int i = 0; i < qos_count; i++){
        printf("on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
        if(granted_qos[i] <= 2){
            have_subscription = true;
        }
    }
    if(have_subscription == false){
        /* 代理拒绝了我们所有的订阅，我们知道我们只发送了一个订阅，所以没有保持连接的点。*/
        fprintf(stderr, "Error: All subscriptions rejected.\n");
        mosquitto_disconnect(mosq);
    }
}

/*获取到订阅的内容*/
void on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    char *payload = (char *)msg->payload;
    payload[msg->payloadlen] = '\0'; // 确保消息是以空字符结尾的字符串

    // 接收时间戳
    double received_time = (double)time(NULL);

    // 解析 JSON 数据
    cJSON *json = cJSON_Parse(payload);
    if (!json){
        fprintf(stderr, "Error parsing JSON: %s\n", cJSON_GetErrorPtr());
        return;
    }

    // 获取 "timestamp" 字段
    cJSON *timestamp = cJSON_GetObjectItem(json, "timestamp");
    if (!cJSON_IsNumber(timestamp)){
        fprintf(stderr, "Invalid payload: 'timestamp' is missing or not a number.\n");
        cJSON_Delete(json);
        return;
    }

    // 添加 "received_timestamp" 字段
    cJSON *received_timestamp = cJSON_CreateNumber(received_time);
    cJSON_AddItemToObject(json, "received_timestamp", received_timestamp);

    // 将更新后的 JSON 数据写入日志文件
    FILE *log_file = fopen("mqtt_logs.json", "a");
    if (log_file){
        char *updated_json = cJSON_PrintUnformatted(json); // 生成紧凑的 JSON 字符串
        fprintf(log_file, "%s\n", updated_json);
        fclose(log_file);

        printf("Logged message: %s\n", updated_json);

        free(updated_json); // 释放生成的 JSON 字符串内存
    }
    else{
        fprintf(stderr, "Failed to open log file for writing.\n");
    }

    // 清理 JSON 对象
    cJSON_Delete(json);
}

void cleanUp(struct mosquitto* mosq)
{
    printf("clean up");
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    exit(1);
}

int main(int argc, char *argv[])
{
    int                 ret;
    struct mosquitto    *mosq;

        // 检查命令行参数：日志文件保存路径
    const char *log_dir = "logs"; // 默认路径
    if (argc > 1) {
        log_dir = argv[1];
    }

    // 检查或创建日志目录
#ifdef _WIN32
    if (_mkdir(log_dir) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create or access directory: %s\n", log_dir);
        return EXIT_FAILURE;
    }
#else
    if (mkdir(log_dir, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "Failed to create or access directory: %s\n", log_dir);
        return EXIT_FAILURE;
    }
#endif

    time_t now = time(NULL);
    struct tm *local_time = localtime(&now);
    char log_filename[256];
    strftime(log_filename, sizeof(log_filename), "logs/mqtt_logs_%Y%m%d_%H%M%S.json", local_time);

    FILE *log_file = fopen(log_filename, "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open log file: %s\n", log_filename);
        return EXIT_FAILURE;
    }

    /* MQTT 初始化 */
    ret = mosquitto_lib_init();
    if(ret){
        printf("Init lib error!\n");
        cleanUp(mosq);
    }

    /* 创建新的客户端 */
    mosq = mosquitto_new(NULL,true,log_file);
    if(mosq == NULL){
        printf("Create a new client failure\n");
        cleanUp(mosq);
    }

    /* 回调函数 */
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_subscribe_callback_set(mosq, on_subscribe);
    mosquitto_message_callback_set(mosq, on_message);

    /* 连接代理 */
    ret = mosquitto_connect(mosq, HOST, PORT, KEEP_ALIVE);
    if(ret){
        printf("Connect server error!\n");
        cleanUp(mosq);
    }
    printf("connection client is ok\n");

    mosquitto_loop_forever(mosq, -1, 1);

    cleanUp(mosq);

    return 0;
} 
