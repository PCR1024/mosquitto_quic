#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <mosquitto.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <ctype.h>
#include <cjson/cJSON.h>
#include <mosquitto.h>

#define HOST           "localhost"
#define PORT            1883
#define KEEP_ALIVE      60
#define MSG_MAX_SIZE    512

static int  g_stop = 0;

void on_connect(struct mosquitto *mosq, void *obj, int rc);
void sig_handle(int signum);

/* 获取时间 */
int get_time(char *datetime, int bytes)
{
    time_t              now;
    struct tm          *t;

    time(&now);
    t = localtime(&now);

    snprintf(datetime, bytes, "%04d-%02d-%02d %02d:%02d:%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, (t->tm_hour)+8, t->tm_min, t->tm_sec);

    return 0;
}

/* 安装信号 */
void sig_handle(int signum)
{
    if(SIGUSR1 == signum){
        g_stop = 1;
    }
}

// 发布函数
void publish_loop(struct mosquitto *mosq, const char *topic, int qos, int interval)
{
    while (1){
        time_t now = time(NULL);
        double timestamp = (double)now;

        // 构建 JSON 消息
        char payload[256];
        snprintf(payload, sizeof(payload), "{\"timestamp\": %.3f}", timestamp);

        // 发布消息
        int ret = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, qos, false);
        if (ret != MOSQ_ERR_SUCCESS){
            fprintf(stderr, "Failed to publish message: %s\n", mosquitto_strerror(ret));
            break;
        }

        printf("Published: %s\n", payload);

        // 按间隔等待
        sleep(interval);
        mosquitto_loop(mosq, -1, 1);
    }
}

void cleanUp(struct mosquitto* mosq)
{
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    exit(1);
}

/*确认连接回函数*/
void on_connect(struct mosquitto *mosq, void *obj, int rc)
{
    printf("Connection successful cJSON call packaging\n");
}

void on_publish(struct mosquitto *mosq, void *obj, int mid)
{
    printf("Message with id %d has been published.\n", mid);
}

int main (int argc, char **argv)
{
    int                 rv;
    struct mosquitto    *mosq = NULL;

    /*安装信号*/
    signal(SIGUSR1,sig_handle);
    
    /* MQTT 初始化 */
    rv = mosquitto_lib_init();
    if(rv != MOSQ_ERR_SUCCESS){
        printf("mosquitto lib int failure:%s\n", strerror(errno));
        cleanUp(mosq);
    }
    
    /* 创建新的客户端 */
    mosq = mosquitto_new(NULL,true,NULL);
    if(!mosq){
        printf("create client failure:%s\n",strerror(errno));
        cleanUp(mosq);
    }
    
    /* 回调函数 */
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_publish_callback_set(mosq, on_publish);
    
    /*  连接MQTT服务器,ip,端口,时间 */ 
    if(mosquitto_connect(mosq, HOST, PORT, KEEP_ALIVE) != MOSQ_ERR_SUCCESS){
        printf("mosquitto_connect() failed: %s\n",strerror(errno));
        cleanUp(mosq);
    }
    printf("connect successfully\n");

    // 启动事件循环（异步）
    int ret = mosquitto_loop_start(mosq);
    if (ret != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to start the loop: %s\n", mosquitto_strerror(ret));
        cleanUp(mosq);
    }

    publish_loop(mosq, "test_signal", 2, 1);

    // 停止事件循环并清理
    mosquitto_loop_stop(mosq, false);
    cleanUp(mosq);
    return 0;
} 