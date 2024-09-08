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

// 상수 정의
#define BUF_SIZE 100              // 버퍼 크기 정의
#define NAME_SIZE 20              // 사용자 이름 크기 정의
#define ARR_CNT 10                // 배열 크기 정의
#define HBEAT_SIZE 120            // 심박수 데이터 크기 정의
#define HRV_HOUR 1                // HRV 측정 시간 단위
#define HOST "localhost"          // MySQL 호스트
#define USER "iot"                // MySQL 사용자
#define PASS "pwiot"              // MySQL 패스워드
#define DB "lulldb"               // MySQL 데이터베이스 이름
#define SERVER_IP "127.0.0.1"     // 서버 IP
#define SERVER_PORT 5000          // 서버 포트
#define PASSWD "PASSWD"           // 패스워드 상수

// 함수 선언
void send_stress_message(const char* status);
void* send_msg(void* arg);
void* recv_msg(void* arg);
void* hrv_msg(void* arg);
void error_handling(char* msg);
void play_music(const char* music_file);

// HRV 데이터를 저장하는 구조체
typedef struct {
    float sdnn;
    float rmssd;
    float pnn50;
} HRVData;

// 글로벌 변수
char name[NAME_SIZE] = "[Default]";  // 사용자 이름을 저장할 변수
char msg[BUF_SIZE];                  // 메시지 저장 버퍼
int hbeat[HBEAT_SIZE * HRV_HOUR];     // 심박수 데이터를 저장하는 배열
int temp, humi;                      // 온도 및 습도 데이터
volatile sig_atomic_t timer_expired = 0; // 타이머 만료 상태를 나타내는 변수
volatile int stress_level = -1;       // 스트레스 레벨
HRVData hrv;                          // HRV 데이터를 저장할 구조체

// MySQL 에러 처리 함수
void finish_with_error(MYSQL* con) {
    fprintf(stderr, "%s\n", mysql_error(con));  // 에러 메시지를 출력
    mysql_close(con);                           // MySQL 연결 종료
    exit(1);                                    // 프로그램 종료
}

// HRV 데이터를 계산하고 스트레스 메시지를 전송하는 함수
void calculateHRVFromHeartRateNSendData(int heart_rates[], int count, int temp, int humi, HRVData* hrv) {
    double sum = 0.0, sum_sq_diff = 0.0;  // 평균과 차이의 제곱합을 계산할 변수
    int nn50_count = 0;                   // NN50을 계산할 때 사용

    // 심박수 평균 계산
    for (int i = 0; i < count; i++) {
        sum += heart_rates[i];
    }
    double mean = sum / count;  // 평균 심박수 계산

    // 심박수의 분산 계산
    for (int i = 0; i < count; i++) {
        sum_sq_diff += pow(heart_rates[i] - mean, 2);
    }

    // HRV 데이터 계산
    hrv->sdnn = sqrt(sum_sq_diff / (count - 1));  // SDNN 계산
    hrv->rmssd = sqrt(sum_sq_diff / (count - 1)); // RMSSD 계산
    hrv->pnn50 = (double)nn50_count / (count - 1) * 100.0; // pNN50 계산

    // 스트레스 레벨 계산 (기준값에 따른 스트레스 레벨 결정)
    stress_level = (hrv->sdnn < 50) + (hrv->rmssd < 42) + (hrv->pnn50 < 3);
    printf("Stress level: %d, Temp: %d, Humidity: %d\n", stress_level, temp, humi);

    // 스트레스 상태에 따라 상태 결정 (GREEN, YELLOW, RED)
    const char* status = stress_level == 0 ? "GREEN@ON" :
                         stress_level == 1 ? "YELLOW@ON" : 
                         stress_level >= 2 ? "RED@ON" : "UNKNOWN";

    // 상태에 따라 음악 파일 설정 및 실행
    const char* music_file = stress_level == 0 ? "green.mp3" :
                             stress_level == 1 ? "yellow.mp3" : "red.mp3";
    play_music(music_file);  // 음악 재생
    send_stress_message(status);  // 스트레스 메시지 전송
}

// 음악을 재생하는 함수
void play_music(const char* music_file) {
    pid_t pid = fork();  // 자식 프로세스를 생성

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
        // 음악재생 프로세스가 끝날 때 까지 기다리지 않음, 메시지 출력
        printf("Playing music: %s\n", music_file);
    }
}

// 스트레스 메시지를 서버로 전송하는 함수
void send_stress_message(const char* status) {
    int sock;
    struct sockaddr_in serv_addr;
    char name_msg[BUF_SIZE];
    char cmd_msg[BUF_SIZE];

    signal(SIGPIPE, SIG_IGN);  // SIGPIPE 무시 (서버 연결이 끊어졌을 때 무시)

    sock = socket(PF_INET, SOCK_STREAM, 0);  // 소켓 생성
    if (sock == -1) {
        perror("socket() error");
        return;
    }

    memset(&serv_addr, 0, sizeof(serv_addr));  // 서버 주소 초기화
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(SERVER_IP);  // 서버 IP 설정
    serv_addr.sin_port = htons(SERVER_PORT);  // 서버 포트 설정

    // 서버에 연결
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("connect() error");
        close(sock);
        return;
    }

    // 이름과 패스워드를 포함한 메시지 전송
    snprintf(name_msg, sizeof(name_msg), "[JYJ_RPI:PASSWD]\n");
    write(sock, name_msg, strlen(name_msg));

    // 스트레스 상태 메시지 전송
    snprintf(cmd_msg, sizeof(cmd_msg), "[JYJ_STM]%s\n", status);
    write(sock, cmd_msg, strlen(cmd_msg));

    close(sock);  // 소켓 종료
}

// 타이머가 만료되었을 때 호출되는 핸들러 함수
void timer_handler(int signum) {
    timer_expired = 1;  // 타이머 만료 상태로 설정
}

// 메시지 수신을 처리하는 스레드 함수
void* recv_msg(void* arg) {
    int* sock = (int*)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 1];
    int str_len;

    // 서버로부터 메시지를 읽어 콘솔에 출력
    while (1) {
        memset(name_msg, 0, sizeof(name_msg));
        str_len = read(*sock, name_msg, sizeof(name_msg) - 1);
        if (str_len <= 0) {
            *sock = -1;  // 연결 종료 시 소켓을 -1로 설정
            return NULL;
        }

        name_msg[str_len] = '\0';  // 문자열 종료
        fputs(name_msg, stdout);  // 메시지 출력
    }
}

// 메시지 송신을 처리하는 스레드 함수
void* send_msg(void* arg) {
    int* sock = (int*)arg;
    char name_msg[NAME_SIZE + BUF_SIZE + 2];

    // 사용자 입력을 받아 서버로 전송
    while (1) {
        memset(msg, 0, sizeof(msg));
        fgets(msg, BUF_SIZE, stdin);  // 사용자 입력 받기

        if (!strncmp(msg, "quit", 4)) {  // "quit" 입력 시 종료
            *sock = -1;
            return NULL;
        }

        snprintf(name_msg, sizeof(name_msg), "[ALLMSG]%s", msg);  // 입력 메시지에 포맷팅 추가
        if (write(*sock, name_msg, strlen(name_msg)) <= 0) {  // 메시지 전송
            *sock = -1;
            return NULL;
        }
    }
}

// HRV 계산 및 데이터를 처리하는 스레드 함수
void* hrv_msg(void* arg) {
    while (1) {
        if (timer_expired) {  // 타이머가 만료되었는지 확인
            MYSQL* conn = mysql_init(NULL);  // MySQL 초기화
            if (conn == NULL || !mysql_real_connect(conn, HOST, USER, PASS, DB, 0, NULL, 0)) {
                finish_with_error(conn);  // MySQL 연결 실패 시 에러 처리
            }

            // 심박수 데이터 가져오기 
            // 최근 6시간 동안의 데이터 수집
            if (mysql_query(conn, "SELECT hbeat FROM lull_sensor WHERE time >= NOW() - INTERVAL 6 HOUR;")) {
                finish_with_error(conn);
            }

            MYSQL_RES* result = mysql_store_result(conn);
            if (result == NULL) {
                finish_with_error(conn);
            }

            // 결과에서 심박수 데이터를 배열에 저장
            MYSQL_ROW sqlrow;
            int i = 0, count = mysql_num_rows(result);
            while ((sqlrow = mysql_fetch_row(result))) {
                hbeat[i++] = atoi(sqlrow[0]);
            }
            mysql_free_result(result);

            // 최근 1개의 온도 및 습도 데이터 가져오기
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

            // HRV 계산 및 스트레스 메시지 전송
            calculateHRVFromHeartRateNSendData(hbeat, count, temp, humi, &hrv);
            timer_expired = 0;  // 타이머 만료 상태 초기화
        }
    }
}

// 메인 함수
int main(int argc, char* argv[]) {
    int sock;
    struct sockaddr_in serv_addr;
    pthread_t snd_thread, rcv_thread, hrv_thread;
    void* thread_return;

    // 명령어 인자 체크 (IP, 포트, 이름 필요)
    if (argc != 4) {
        printf("Usage: %s <IP> <port> <name>\n", argv[0]);
        exit(1);
    }

    snprintf(name, sizeof(name), "%s", argv[3]);  // 이름 설정

    sock = socket(PF_INET, SOCK_STREAM, 0);  // 소켓 생성
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));  // 서버 주소 설정
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);  // IP 설정
    serv_addr.sin_port = htons(atoi(argv[2]));  // 포트 설정

    // 서버에 연결 시도
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    snprintf(msg, sizeof(msg), "[%s:PASSWD]", name);  // 로그인 메시지 포맷
    write(sock, msg, strlen(msg));  // 서버에 전송

    // 송신, 수신, HRV 스레드 생성
    pthread_create(&snd_thread, NULL, send_msg, (void*)&sock);
    pthread_create(&rcv_thread, NULL, recv_msg, (void*)&sock);
    pthread_create(&hrv_thread, NULL, hrv_msg, (void*)&sock);

    // 스레드 종료 대기
    pthread_join(snd_thread, &thread_return);
    pthread_join(rcv_thread, &thread_return);
    pthread_join(hrv_thread, &thread_return);

    close(sock);  // 소켓 닫기
    return 0;
}

// 에러 처리 함수
void error_handling(char* msg) {
    fputs(msg, stderr);  // 에러 메시지 출력
    fputc('\n', stderr);
    exit(1);  // 프로그램 종료
}
