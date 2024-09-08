#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <mysql/mysql.h>
#include <math.h>
#include <sys/wait.h>

#define BUF_SIZE 100
#define NAME_SIZE 20
#define ARR_CNT 10
#define HBEAT_SIZE 120
#define HRV_HOUR 1
#define HOST "localhost"
#define USER "iot"
#define PASS "pwiot"
#define DB "lulldb"
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 5000
#define PASSWD "PASSWD"

void send_stress_message(const char* status);
void* send_msg(void* arg);
void* recv_msg(void* arg);
void* hrv_msg(void* arg);
void error_handling(char* msg);
void play_music(const char* music_file);

typedef struct {
    float sdnn;
    float rmssd;
    float pnn50;
} HRVData;

// 글로벌 변수들
char name[NAME_SIZE] = "[Default]";
char msg[BUF_SIZE];
int hbeat[HBEAT_SIZE * HRV_HOUR];
int temp, humi;
volatile sig_atomic_t timer_expired = 0;
volatile int stress_level = -1;
HRVData hrv;

// 에러 처리 함수
void finish_with_error(MYSQL* con) {
    fprintf(stderr, "%s\n", mysql_error(con));
    mysql_close(con);
    exit(1);
}

// HRV 계산 함수
void calculateHRVFromHeartRateNSendData(int heart_rates[], int count, int temp, int humi, HRVData* hrv) {
    double sum = 0.0, sum_sq_diff = 0.0;
    int nn50_count = 0;

    for (int i = 0; i < count; i++) {
        sum += heart_rates[i];
    }
    double mean = sum / count;
    for (int i = 0; i < count; i++) {
        sum_sq_diff += pow(heart_rates[i] - mean, 2);
    }

    hrv->sdnn = sqrt(sum_sq_diff / (count - 1));
    hrv->rmssd = sqrt(sum_sq_diff / (count - 1));
    hrv->pnn50 = (double)nn50_count / (count - 1) * 100.0;

    // 스트레스 레벨 계산
    stress_level = (hrv->sdnn < 50) + (hrv->rmssd < 42) + (hrv->pnn50 < 3);
    printf("Stress level: %d, Temp: %d, Humidity: %d\n", stress_level, temp, humi);

    const char* status = stress_level == 0 ? "GREEN@ON" :
                         stress_level == 1 ? "YELLOW@ON" : 
                         stress_level >= 2 ? "RED@ON" : "UNKNOWN";

    // 음악 재생 및 메시지 전송
    const char* music_file = stress_level == 0 ? "green.mp3" :
                             stress_level == 1 ? "yellow.mp3" : "red.mp3";
    play_music(music_file);
    send_stress_message(status);
}

void play_music(const char* music_file) {
    pid_t pid = fork();  // 새로운 프로세스 생성

    if (pid == -1) {
        // fork 실패 시 에러 처리
        perror("fork() failed");
        return;
    }

    if (pid == 0) {
        // 자식 프로세스에서 음악 재생
        execlp("mpg123", "mpg123", music_file, (char*)NULL);
        // execlp 실패 시 에러 출력
        perror("execlp() failed");
        exit(EXIT_FAILURE);
    } else {
        waitpid(pid, NULL, 0);
        printf("Playing music: %s\n", music_file);
    }
}

// 스트레스 메시지 전송 함수
void send_stress_message(const char* status) {
    int sock;
    struct sockaddr_in serv_addr;
    char name_msg[BUF_SIZE];
    char cmd_msg[BUF_SIZE];

    signal(SIGPIPE, SIG_IGN);

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket() error");
        return;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serv_addr.sin_port = htons(SERVER_PORT);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect() error");
        close(sock);
        return;
    }

    snprintf(name_msg, sizeof(name_msg), "[JYJ_LIN:PASSWD]\n");
    write(sock, name_msg, strlen(name_msg));

    snprintf(cmd_msg, sizeof(cmd_msg), "[JYJ_BLT]%s\n", status);
    write(sock, cmd_msg, strlen(cmd_msg));

    close(sock);
}

// 타이머 핸들러
void timer_handler(int signum) {
    timer_expired = 1;
}

// 소켓 메시지 수신 스레드
void* recv_msg(void* arg) {
    int* sock = (int*)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 1];
    int str_len;

    while (1) {
        memset(name_msg, 0, sizeof(name_msg));
        str_len = read(*sock, name_msg, sizeof(name_msg) - 1);
        if (str_len <= 0) {
            *sock = -1;
            return NULL;
        }

        name_msg[str_len] = '\0';
        fputs(name_msg, stdout);
    }
}

// 소켓 메시지 송신 스레드
void* send_msg(void* arg) {
    int* sock = (int*)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 2];

    while (1) {
        memset(msg, 0, sizeof(msg));
        fgets(msg, BUF_SIZE, stdin);

        if (!strncmp(msg, "quit", 4)) {
            *sock = -1;
            return NULL;
        }

        snprintf(name_msg, sizeof(name_msg), "[ALLMSG]%s", msg);
        if (write(*sock, name_msg, strlen(name_msg)) <= 0) {
            *sock = -1;
            return NULL;
        }
    }
}

// HRV 메시지 스레드
void* hrv_msg(void* arg) {
    while (1) {
        if (timer_expired) {
            MYSQL* conn = mysql_init(NULL);
            if (conn == NULL || !mysql_real_connect(conn, HOST, USER, PASS, DB, 0, NULL, 0)) {
                finish_with_error(conn);
            }

            // 심박수 및 온도, 습도 데이터 가져오기
            if (mysql_query(conn, "SELECT hbeat FROM lull_sensor WHERE time >= NOW() - INTERVAL 6 HOUR;")) {
                finish_with_error(conn);
            }

            MYSQL_RES* result = mysql_store_result(conn);
            if (result == NULL) {
                finish_with_error(conn);
            }

            MYSQL_ROW sqlrow;
            int i = 0, count = mysql_num_rows(result);
            while ((sqlrow = mysql_fetch_row(result))) {
                hbeat[i++] = atoi(sqlrow[0]);
            }
            mysql_free_result(result);

            if (mysql_query(conn, "SELECT temp, humi FROM lull_sensor ORDER BY date DESC, time DESC LIMIT 1;")) {
                finish_with_error(conn);
            }

            result = mysql_store_result(conn);
            if (result == NULL) {
                finish_with_error(conn);
            }

            sqlrow = mysql_fetch_row(result);
            temp = atoi(sqlrow[0]);
            humi = atoi(sqlrow[1]);
            mysql_free_result(result);
            mysql_close(conn);

            // HRV 계산 및 메시지 전송
            calculateHRVFromHeartRateNSendData(hbeat, count, temp, humi, &hrv);
            timer_expired = 0;
        }

        usleep(100000);
    }
}

int main(int argc, char* argv[]) {
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread, rcv_thread, hrv_thread;
    void* thread_return;

    if (argc != 4) {
        printf("Usage: %s <IP> <port> <name>\n", argv[0]);
        exit(1);
    }

    snprintf(name, sizeof(name), "%s", argv[3]);

    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    snprintf(msg, sizeof(msg), "[%s:PASSWD]", name);
    write(sock, msg, strlen(msg));

    pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
    pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
    pthread_create(&hrv_thread, NULL, hrv_msg, (void*)&sock);

    pthread_join(snd_thread, &thread_return);
    pthread_join(rcv_thread, &thread_return);
    pthread_join(hrv_thread, &thread_return);

    close(sock);
    return 0;
}

void error_handling(char* msg) {
    fputs(msg, stderr);
    fputc('\n', stderr);
    exit(1);
}
