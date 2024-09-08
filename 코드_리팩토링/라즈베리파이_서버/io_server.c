#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#define BUF_SIZE 100
#define MAX_CLNT 32
#define ID_SIZE 10
#define ARR_CNT 5
#define DEBUG

// 메시지 정보 구조체
typedef struct {
		char fd;          // 소켓 파일 디스크립터
		char *from;       // 보낸 사람 ID
		char *to;         // 받을 사람 ID
		char *msg;        // 메시지 내용
		int len;          // 메시지 길이
} MSG_INFO;

// 클라이언트 정보 구조체
typedef struct {
		int index;        // 클라이언트의 배열 인덱스
		int fd;           // 클라이언트 소켓 파일 디스크립터
		char ip[20];      // 클라이언트 IP 주소
		char id[ID_SIZE]; // 클라이언트 ID
		char pw[ID_SIZE]; // 클라이언트 비밀번호
} CLIENT_INFO;

void * clnt_connection(void * arg); 					// 클라이언트 연결 처리 함수
void send_msg(MSG_INFO * msg_info, CLIENT_INFO * first_client_info); 	// 메시지 전송 함수
void error_handling(char * msg); 					// 에러 처리 함수
void log_file(char * msgstr); 						// 로그 출력 함수

int clnt_cnt=0; 							// 현재 접속한 클라이언트 수
pthread_mutex_t mutx; 							// 클라이언트 동기화 위한 뮤텍스

int main(int argc, char *argv[])
{
		int serv_sock, clnt_sock; 				// 서버 소켓과 클라이언트 소켓
		struct sockaddr_in serv_adr, clnt_adr; 			// 서버와 클라이언트의 주소 정보
		int clnt_adr_sz; 					// 클라이언트 주소 크기
		int sock_option  = 1; 					// 소켓 옵션 (재사용 설정)
		pthread_t t_id[MAX_CLNT] = {0}; 			// 클라이언트별 스레드 ID
		int str_len = 0;
		int i;
		char idpasswd[(ID_SIZE*2)+3]; 				// ID와 비밀번호 읽기 위한 버퍼
		char *pToken;
		char *pArray[ARR_CNT]={0};				// 토큰 분리 저장 배열
		char msg[BUF_SIZE]; 					// 메시지 버퍼

		CLIENT_INFO client_info[MAX_CLNT] = {{0,-1,"","JYJ_STM","PASSWD"}, \
				{0,-1,"","JYJ_SQL","PASSWD"},  {0,-1,"JYJ_ARD","3","PASSWD"}, \
				{0,-1,"JYJ_RPI","4","PASSWD"},  {0,-1,"JYJ_UBT","5","PASSWD"}, \
				{0,-1,"","6","PASSWD"},  {0,-1,"","7","PASSWD"}, \
				{0,-1,"","8","PASSWD"},  {0,-1,"","9","PASSWD"}, \
				{0,-1,"","10","PASSWD"},  {0,-1,"","11","PASSWD"}, \
				{0,-1,"","12","PASSWD"},  {0,-1,"","13","PASSWD"}, \
				{0,-1,"","14","PASSWD"},  {0,-1,"","15","PASSWD"}, \
				{0,-1,"","16","PASSWD"},  {0,-1,"","17","PASSWD"}, \
				{0,-1,"","18","PASSWD"},  {0,-1,"","19","PASSWD"}, \
				{0,-1,"","20","PASSWD"},  {0,-1,"","21","PASSWD"}, \
				{0,-1,"","22","PASSWD"},  {0,-1,"","23","PASSWD"}, \
				{0,-1,"","24","PASSWD"},  {0,-1,"","25","PASSWD"}, \
				{0,-1,"","26","PASSWD"},  {0,-1,"","27","PASSWD"}, \
				{0,-1,"","28","PASSWD"},  {0,-1,"","29","PASSWD"}, \
				{0,-1,"","30","PASSWD"},  {0,-1,"","31","PASSWD"}, \
				{0,-1,"","HM_CON","PASSWD"}
		};

		// 포트 번호가 제공되지 않았을 때
		if(argc != 2) { 					
				printf("Usage : %s <port>\n",argv[0]);
				exit(1);
		}
		fputs("IoT Server Start!!\n",stdout);

		// 뮤텍스 초기화
		if(pthread_mutex_init(&mutx, NULL))
				error_handling("mutex init error");

		// 서버 소켓 생성
		serv_sock = socket(PF_INET, SOCK_STREAM, 0);

		// 서버 주소 정보 초기화
		memset(&serv_adr, 0, sizeof(serv_adr));
		serv_adr.sin_family=AF_INET;
		serv_adr.sin_addr.s_addr=htonl(INADDR_ANY);
		serv_adr.sin_port=htons(atoi(argv[1]));

		// 소켓 옵션 설정
		// TIME_WAIT 시간이 지나기 전에 다시 포트 바인딩 가능
		// 보안 문제 발생가능?
		#ifdef DEBUG
    			setsockopt(serv_sock, SOL_SOCKET, SO_REUSEADDR, (void*)&sock_option, sizeof(sock_option));
		#endif
	
		// 소켓과 서버 주소 바인딩
		if(bind(serv_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr))==-1)
				error_handling("bind() error");

		// 클라이언트 연결 대기
		if(listen(serv_sock, 5) == -1)
				error_handling("listen() error");

		while(1) {	
				
				clnt_adr_sz = sizeof(clnt_adr); // accept 함수에 넣기 위해
				clnt_sock = accept(serv_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz); // 클라이언트 연결 요청 수락

				if(clnt_cnt >= MAX_CLNT) // 클라이언트 수가 한도 초과일 때
				{
						printf("socket full\n");
						shutdown(clnt_sock, SHUT_WR); // 소켓 닫기
						continue;
				}
				else if(clnt_sock < 0)
				{
						perror("accept()");
						continue;
				}

				// 클라이언트의 ID와 비밀번호 읽기
				str_len = read(clnt_sock, idpasswd, sizeof(idpasswd));
				idpasswd[str_len] = '\0';

				if(str_len > 0)
				{
						i=0;
						// ID와 비밀번호 분리
						pToken = strtok(idpasswd, "[:]");

						while(pToken != NULL)
						{
								pArray[i] =  pToken;
								if(i++ >= ARR_CNT)
										break;	
								pToken = strtok(NULL,"[:]");
						}

						// 클라이언트 정보와 일치하는지 확인
						for(i=0; i < MAX_C시
								if(!strcmp(client_info[i].id, pArray[0]))
								{
										if(client_info[i].fd != -1) // 이미 로그인된 경우
										{
												sprintf(msg,"[%s] Already logged!\n", pArray[0]);
												write(clnt_sock, msg, strlen(msg));
												log_file(msg);
												shutdown(clnt_sock, SHUT_WR);
												break;
										}
										if(!strcmp(client_info[i].pw, pArray[1])) // 비밀번호 일치 시
										{
												// 클라이언트 정보 업데이트 및 스레드 생성
												strcpy(client_info[i].ip, inet_ntoa(clnt_adr.sin_addr));

												// 공유 정보인 clnt_cnt의 값을 변환하기 위해 뮤텍스 잠금
												pthread_mutex_lock(&mutx);
												client_info[i].index = i; 
												client_info[i].fd = clnt_sock; 
												clnt_cnt++;
												pthread_mutex_unlock(&mutx);
												// 뮤텍스 해제
												sprintf(msg, "[%s] New connected! (ip:%s,fd:%d,sockcnt:%d)\n", pArray[0], inet_ntoa(clnt_adr.sin_addr), clnt_sock, clnt_cnt);
												log_file(msg);

												// 클라이언트에서 연결 성공 메시지 출력
												write(clnt_sock, msg, strlen(msg));

												// 스레드를 기본 형태로 만들고 clnt_connection을 실행, client의 정보 구조체도 넘김
												pthread_create(t_id+i, NULL, clnt_connection, (void *)(client_info + i)); // 클라이언트별 스레드 생성
												pthread_detach(t_id[i]); // 스레드 분리, 자동으로 자원 해제
												break;
										}
								}
						}

						if(i == MAX_CLNT) // 인증 실패 시
						{
								sprintf(msg, "[%s] Authentication Error!\n", pArray[0]);
								write(clnt_sock, msg, strlen(msg));
								log_file(msg);
								shutdown(clnt_sock, SHUT_WR);
						}
				}
				else 
						shutdown(clnt_sock, SHUT_WR); // 송수신 완전 종료료

		}
		return 0;
}

// 클라이언트 연결 처리 함수, 서브 스레드
void * clnt_connection(void *arg)
{
		CLIENT_INFO * client_info = (CLIENT_INFO *)arg;		// argument로 온 Client info를 CLIENT_INFO 형 주소로 형변환
		int index = client_info->index;						// 클라이언트 index 저장
		int str_len = 0;									// 메시지 길이 저장용 변수
		char msg[BUF_SIZE];									// 수신 메시지 저장 버퍼 선언
		char to_msg[MAX_CLNT*ID_SIZE+1];					// 송신 메시지 버퍼 선언
		int i = 0;											// 문자열 토큰화 반복문에 쓰이는 변수
		char *pToken;										// 메시지 토큰화 후 시작 주소를 저장할 포인터
		char *pArray[ARR_CNT] = {0};						// 토큰화 된 메시지를 저장할 배열
		char strBuff[130] = {0};							// 서버의 로그 메시지 처리용 버퍼퍼 

		MSG_INFO msg_info;
		CLIENT_INFO  * first_client_info;

		// 클라이언트 목록 참조를 위해 포인터 설정
		first_client_info = (CLIENT_INFO *)((void *)client_info - (void *)( sizeof(CLIENT_INFO) * index ));

		while(1)
		{
				memset(msg, 0x0, sizeof(msg));
				str_len = read(client_info->fd, msg, sizeof(msg)-1); // 클라이언트로부터 메시지 읽기
				if(str_len <= 0)
						break;

				msg[str_len] = '\0';
				pToken = strtok(msg, "[:]"); // 메시지 분리
				i = 0; 
				while(pToken != NULL)
				{		
						// 토큰을 배열에 저장
						pArray[i] =  pToken;
						if(i++ >= ARR_CNT)
								break;	
						// 다음 토큰 추출
						// strtok()는 내부적으로 문자열의 어느 위치까지 읽었는지 기억함.
						pToken = strtok(NULL, "[:]");
				}

				// 메시지 정보 설정
				msg_info.fd = client_info->fd;		// 나의 파일 디스크립터 aka 소켓
				msg_info.from = client_info->id;	// 내 아이디, 송신자
				msg_info.to = pArray[0];			// 상대방 아이디, 수신자
				sprintf(to_msg,"[%s]%s", msg_info.from, pArray[1]);	// 송신 아이디 문자열 생성
				msg_info.msg = to_msg;	// 구조체에 저장
				msg_info.len = strlen(to_msg);	// 문자열 길이도 저장

				// 만들어진 메시지를 버퍼에 저장하고
				sprintf(strBuff,"msg : [%s->%s] %s", msg_info.from, msg_info.to, pArray[1]);
				
				log_file(strBuff);

				// 메시지 전송
				// 메시지 정보에 따라 첫번째 클라이언트 부터 순차적으로 ID 비교해서 주인 찾아감
				// 메인 스레드의 함수 -> 서버가 만들어진 메시지 적절한 클라이언트에게 보내줌줌
				send_msg(&msg_info, first_client_info);
		}

		close(client_info->fd); // 클라이언트 연결 종료

		// 서버의 로그 메시지 처리 
		sprintf(strBuff, "Disconnect ID:%s (ip:%s,fd:%d,sockcnt:%d)\n", client_info->id, client_info->ip, client_info->fd, clnt_cnt-1);
		log_file(strBuff);

		// 클라이언트 수 감소 및 정보 초기화
		// 공유 변수 clnt_cnt 처리
		pthread_mutex_lock(&mutx);
		clnt_cnt--;
		client_info->fd = -1;
		pthread_mutex_unlock(&mutx);

		return 0;
}

// 메시지 전송 함수, 메인 스레드
void send_msg(MSG_INFO * msg_info, CLIENT_INFO * first_client_info)
{
		
		int i=0;

		// 모든 클라이언트에게 메시지 전송
		if(!strcmp(msg_info->to, "ALLMSG"))
		{		
				// 
				for(i=0; i<MAX_CLNT; i++)
						if((first_client_info+i)->fd != -1)	
								write((first_client_info+i)->fd, msg_info->msg, msg_info->len);
		}
		// 접속한 클라이언트 목록 전송
		else if(!strcmp(msg_info->to, "IDLIST"))
		{
				char* idlist = (char *)malloc(ID_SIZE * MAX_CLNT);
				msg_info->msg[strlen(msg_info->msg) - 1] = '\0';
				strcpy(idlist, msg_info->msg);

				for(i=0; i<MAX_CLNT; i++)
				{
						if((first_client_info+i)->fd != -1)	
						{
								strcat(idlist, (first_client_info+i)->id);
								strcat(idlist, " ");
						}
				}
				strcat(idlist, "\n");
				write(msg_info->fd, idlist, strlen(idlist));
				free(idlist);
		}
		// 특정 클라이언트에게 메시지 전송
		else
				for(i=0; i<MAX_CLNT; i++)
						if((first_client_info+i)->fd != -1)	
								if(!strcmp(msg_info->to, (first_client_info+i)->id))
										write((first_client_info+i)->fd, msg_info->msg, msg_info->len);
}

// 에러 처리 함수
void error_handling(char *msg)
{
		fputs(msg, stderr);
		fputc('\n', stderr);
		exit(1);
}

// 로그 출력 함수
void log_file(char * msgstr)
{
		fputs(msgstr, stdout);
}
