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
#include <openssl/ssl.h>
#include <openssl/err.h>

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

SSL* sslClient = 0;

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
                    exit(2);
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
	            exit(2);
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
	    
            if(SSL_write(sslClient, outputBuffer, strlen(outputBuffer)) < 0){
		fprintf(stderr, "Error with SSL_write()\n");
		exit(2);
		}
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
            exit(2);
        }
        if(fds[0].revents & POLLIN){
            char* buf;
            buf = (char*) malloc(1024);
	    int reading;
	    reading = SSL_read(sslClient, buf, 1024);
            if(reading < 0){
		fprintf(stderr, "Error with SSL_read()\n");
		exit(2);
		}
	    
            //fgets(buf, 1024, fdopen(sock, "r"));
	  char input[256];
	  memset(input, 0, 256);
	  int i = 0;
	  int j = 0;
	  for(; i < reading; i++){
	   	if(buf[i] == '\n'){
            		if(!strcmp(input, "SCALE=F")){
                		scale = 'F';
            		}
           	 	else if(!strcmp(input, "SCALE=C")){
                		scale = 'C';
            		}
            		else if(periodHelper("PERIOD=", input)){
                		period = atoi(buf + 7);
            		}
            		else if(!strcmp(input, "STOP")){
                		halt = 0;
            		}
            		else if(!strcmp(input, "START")){
                		halt = 1;
            		}
            		else if(!strcmp(input, "OFF")){
                		break_loop = 0;
            		}
	
            		if(logFile){
                		fputs(input, logFile);
                		fflush(logFile);
            			}
	   		j = 0;
			}	
		else{
			input[j] = buf[i];
			j++;
		}
	  	}
        }
        
    }
}

void connect_to_server(int argc, char* argv[]){

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if(sock < 0){
		fprintf(stderr, "Error opening socket\n");
		exit(2);
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
		exit(2);
	}

}

void start_ssl(){

	SSL_library_init();
	SSL_load_error_strings();
	OpenSSL_add_all_algorithms();
	SSL_CTX* newContext = SSL_CTX_new(TLSv1_client_method());
	sslClient = SSL_new(newContext);
	if(sslClient == NULL){
		fprintf(stderr, "Error setting new context\n");
		exit(2);
	}
	if(!SSL_set_fd(sslClient, sock)){
		fprintf(stderr, "Error setting fd to sock\n");
		exit(2);
	}
	if(SSL_connect(sslClient) != 1){
		fprintf(stderr, "Error connecting with SSL\n");
		exit(2);
	}

}

int main(int argc, char* argv[]){

    parse_args(argc, argv);

    connect_to_server(argc, argv);

    start_ssl();

    char sslBuf[256];
    sprintf(sslBuf, "ID=%s\n", myID);
    if(SSL_write(sslClient, sslBuf, strlen(sslBuf)) < 0){
	fprintf(stderr, "Error with SSL_write\n");
	exit(2);
	}
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
    
    if(SSL_write(sslClient, outputBuffer, strlen(outputBuffer)) < 0){
	fprintf(stderr, "Error with SSL_write()\n");
	exit(2);
	}

    if(logFile){
        fputs(outputBuffer, logFile);
        fflush(logFile);
    }
    
    mraa_aio_close(temp);
    mraa_gpio_close(button);
    SSL_shutdown(sslClient);
    SSL_free(sslClient);
    exit(0);
} 

