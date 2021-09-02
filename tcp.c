#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <poll.h>
#include <errno.h>
#include <mraa.h>
#include <time.h>
#include <mraa/aio.h>
#include <sys/time.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>

int period = 1;
char scale = 'F';
FILE* logFile = NULL;
mraa_aio_context temp;
mraa_gpio_context button;

struct timeval myTime;
int prevSec = -1;
char timeBuffer[256];
char outputBuffer[256];
int break_loop = 1;
int halt = 1;

char* myID;
char* host;
int sock;
struct sockaddr_in server_address;
struct hostent* server;

void parse_args(int argc, char* argv[]){

    static struct option long_options[] = {
        {"scale", required_argument, 0, 's'},
        {"period", required_argument, 0, 'p'},
        {"log", required_argument, 0, 'l'},
	{"host", required_argument, 0, 'h'},
	{"id", required_argument, 0, 'i'},
        {0, 0, 0, 0}
    };
    int c;
    int option_index = 0;
    while(1){
        c = getopt_long(argc, argv, "", long_options, &option_index);
        if(c < 0)
            break;
        switch(c){
            case 's':
            ;
	            char scaleChar;
                scaleChar = optarg[0];
                if(scaleChar == 'F' || scaleChar == 'C'){
                    scale = scaleChar;
                }
	            break;
            case 'p':
	            period = atoi(optarg);
	            break;
            case 'l':
                logFile = fopen(optarg, "w+");
                if(logFile == NULL){
                    fprintf(stderr, "Error opening log file");
                    exit(1);
                }
                break;
	    case 'h':
		host = optarg;
		break;
	    case 'i':
		myID = optarg;
		break;		
            default:
	            fprintf(stderr, "Error, invalid arguments \n");
	            exit(1);
        }
    } 
}


int periodHelper(char* first, char* second){
    int firstLen, secondLen;
    firstLen = strlen(first);
    secondLen = strlen(second);
    if(firstLen > secondLen){
        return 0;
    }
    else{
        return !memcmp(first, second, firstLen);
    }
}

void shut_down(){
    break_loop = 0;
}

void to_the_polls(){

    struct pollfd fds[1];
    fds[0].fd = sock;
    fds[0].events = POLLIN | POLLHUP | POLLERR;

    double myTemp, tempVar1, tempVar2, val;

    while(break_loop == 1){
        gettimeofday(&myTime, 0);
        int timeSec = myTime.tv_sec;
        int diff = timeSec - prevSec;
        if(halt == 1 && diff >= period){
            val = mraa_aio_read(temp);
            tempVar1 = (((1023.0) / (float) val) - 1.0) * 100000.0;
            tempVar2 = 1.0 / (log(tempVar1 / 100000.0) / 4275 + 1 / 298.15) - 273.15;
            if(scale == 'F'){
                myTemp = (tempVar2 * 9) / 5 + 32;
            }
            else myTemp = tempVar2;
            strftime(timeBuffer, 256, "%H:%M:%S", localtime(&myTime.tv_sec));
            sprintf(outputBuffer, "%s %.1f\n", timeBuffer, myTemp);
            dprintf(sock, outputBuffer);
            if(logFile){
                fputs(outputBuffer, logFile);
                fflush(logFile);
            }
            prevSec = myTime.tv_sec;
        }
        int pret, timeout;
        timeout = 0;
        pret = poll(fds, 1, timeout);
        if(pret < 0){
            fprintf(stderr, "Error polling");
            exit(1);
        }
        if(fds[0].revents & POLLIN){
            char* buf;
            buf = (char*) malloc(1024);
            fgets(buf, 1024, fdopen(sock, "r"));
            if(!strcmp(buf, "SCALE=F\n")){
                scale = 'F';
            }
            else if(!strcmp(buf, "SCALE=C\n")){
                scale = 'C';
            }
            else if(periodHelper("PERIOD=\n", buf)){
                period = atoi(buf + 7);
            }
            else if(!strcmp(buf, "STOP\n")){
                halt = 0;
            }
            else if(!strcmp(buf, "START\n")){
                halt = 1;
            }
            else if(periodHelper("LOG\n", buf)){
                ;
            }
            else if(!strcmp(buf, "OFF\n")){
                break_loop = 0;
            }
            if(logFile){
                fputs(buf, logFile);
                fflush(logFile);
            }
        }
        
    }
}

void connect_to_server(int argc, char* argv[]){

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0){
		fprintf(stderr, "Error opening socket\n");
		exit(1);
	}
	server = gethostbyname(host);
	
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	memcpy((char*)&server_address.sin_addr.s_addr, (char*)server->h_addr, server->h_length);
	int port = atoi(argv[argc - 1]);
	server_address.sin_port = htons(port);
	int connecting = 0;
	connecting = connect(sock, (struct sockaddr*)&server_address, sizeof(server_address));
	if(connecting < 0){
		fprintf(stderr, "Error connecting to server\n");
		exit(1);
	}

}

int main(int argc, char* argv[]){

    parse_args(argc, argv);

    connect_to_server(argc, argv);

    dprintf(sock, "ID=%s\n", myID);
    fputs("ID=", logFile);
    fputs(myID, logFile);
    fputs("\n", logFile);
    fflush(logFile);

    temp = mraa_aio_init(1);
    button = mraa_gpio_init(60);
    if(button == NULL){
        fprintf(stderr, "Error initializing button\n");
        mraa_deinit();
        exit(1);
    }
    mraa_gpio_dir(button, MRAA_GPIO_IN);
    mraa_gpio_isr(button, MRAA_GPIO_EDGE_RISING, &shut_down, NULL);
    to_the_polls();

    gettimeofday(&myTime, 0);
    strftime(timeBuffer, 256, "%H:%M:%S", localtime(&myTime.tv_sec));
    sprintf(outputBuffer, "%s SHUTDOWN\n", timeBuffer);
    
    dprintf(sock, outputBuffer);

    if(logFile){
        fputs(outputBuffer, logFile);
        fflush(logFile);
    }
    
    mraa_aio_close(temp);
    mraa_gpio_close(button);
    exit(0);
} 
