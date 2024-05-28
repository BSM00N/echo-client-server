#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#endif // __linux
#ifdef WIN32
#include <winsock2.h>
#include "../mingw_net.h"
#endif // WIN32
#include <thread>
#include <vector> // 클라이언트 소켓을 저장하기 위해 추가 (벡터)
#include <mutex> // 멀티스레드 환경에서 클라이언트 소켓 벡터 보호를 위해 추가
#include <algorithm> // std::remove를 사용하기 위해 추가

#ifdef WIN32
void myerror(const char* msg) { fprintf(stderr, "%s %lu\n", msg, GetLastError()); }
#else
void myerror(const char* msg) { fprintf(stderr, "%s %s %d\n", msg, strerror(errno), errno); }
#endif

void usage() {
	printf("tcp server %s\n",
#include "../version.txt"
   );
	printf("\n");
	printf("syntax: ts <port> [-e] [-b] [-si <src ip>]\n");
	printf("  -e : echo\n");
	printf("  -b : broadcast\n");
	printf("sample: ts 1234\n");
}

struct Param {
	bool echo{false};
	bool broadcast{false}; //-b 옵션 처리를 위해 사용
	uint16_t port{0};
	uint32_t srcIp{0};

	bool parse(int argc, char* argv[]) {
		for (int i = 1; i < argc;) {
			if (strcmp(argv[i], "-e") == 0) {
				echo = true;
				i++;
				continue;
			}

			// 위의 문법 및 구조를 참조하여 -b 옵션 처리 위해 사
			if (strcmp(argv[i], "-b") == 0) {
            broadcast = true; 
            i++;
            continue;
         }

			if (strcmp(argv[i], "-si") == 0) {
				int res = inet_pton(AF_INET, argv[i + 1], &srcIp);
				switch (res) {
					case 1: break;
					case 0: fprintf(stderr, "not a valid network address\n"); return false;
					case -1: myerror("inet_pton"); return false;
				}
				i += 2;
				continue;
			}

			if (i < argc) port = atoi(argv[i++]);
		}
		return port != 0;
	}
} param;

std::vector<int> clients;
// 이때의 벡터의 경우에는 동적할당을 통해 데이터 저장
std::mutex clients_mutex;
//뮤텍스를 사용한 벡터 동시 접근을 막음

void recvThread(int sd) {
	printf("connected\n");
	fflush(stdout);
	static const int BUFSIZE = 65536;
	char buf[BUFSIZE];
	while (true) {
		ssize_t res = ::recv(sd, buf, BUFSIZE - 1, 0);
		if (res == 0 || res == -1) {
			fprintf(stderr, "recv return %zd", res);
			myerror(" ");
			break;
		}
		buf[res] = '\0';
		printf("%s", buf);
		fflush(stdout);
		if (param.echo) {
			res = ::send(sd, buf, res, 0);
			if (res == 0 || res == -1) {
				fprintf(stderr, "send return %zd", res);
				myerror(" ");
				break;
			}
		}

		// -b 옵션이 있을 경우 모든 클라이언트에게 브로드캐스트
		if (param.broadcast) { 
         std::lock_guard<std::mutex> guard(clients_mutex); // 벡터 보호를 위해 뮤텍스 사용
         for (int client_sd : clients) { //clients의 데이터를 client_sd로 가져옴.
            if (client_sd != sd) { //각 클라이언트의 디스크립터가 현재의 소켓디스크립터 sd와 다른지 확인 -> 자기 자신에게 보내지 않음
                 ::send(client_sd, buf, res, 0);
            }
         }
      }
	}
	printf("disconnected\n");
	fflush(stdout);
	::close(sd);
	std::lock_guard<std::mutex> guard(clients_mutex); //벡터 보호를 위한 뮤텍스 사용 
	clients.erase(std::remove(clients.begin(), clients.end(), sd), clients.end()); //사용한 클라이언트 데이터 제
}

int main(int argc, char* argv[]) {
	if (!param.parse(argc, argv)) {
		usage();
		return -1;
	}

#ifdef WIN32
	WSAData wsaData;
	WSAStartup(0x0202, &wsaData);
#endif // WIN32

	// socket
	int sd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (sd == -1) {
		myerror("socket");
		return -1;
	}

#ifdef __linux__
	// setsockopt
	{
		int optval = 1;
		int res = ::setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
		if (res == -1) {
			myerror("setsockopt");
			return -1;
		}
	}
#endif // __linux

	// bind
	{
		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = param.srcIp;
		addr.sin_port = htons(param.port);

		ssize_t res = ::bind(sd, (struct sockaddr *)&addr, sizeof(addr));
		if (res == -1) {
			myerror("bind");
			return -1;
		}
	}

	// listen
	{
		int res = listen(sd, 5);
		if (res == -1) {
			myerror("listen");
			return -1;
		}
	}

	while (true) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int newsd = ::accept(sd, (struct sockaddr *)&addr, &len);
		if (newsd == -1) {
			myerror("accept");
			break;
		}
		std::lock_guard<std::mutex> guard(clients_mutex); // 벡터 보호를 위해 뮤텍스 사용
      clients.push_back(newsd); // 새 클라이언트 소켓을 벡터에 추가
		std::thread* t = new std::thread(recvThread, newsd);
		t->detach();
	}
	::close(sd);
}
