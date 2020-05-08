#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <ncurses.h>
#include <time.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/ipc.h>

// define buffer size fo saving message
#define BUFFSIZE 1024

int ret_count; // variable for return value
int num = 1; // variable for counting auto messages
int msg_cnt; // variable for counting fetched messages
struct shmid_ds info; // struct for saving shared memory status

// Declare Windows
WINDOW *base_scr, *input_scr, *output_scr, *account_scr, *time_scr;

// Define struct for saving input or output data
struct message_buffer {
	char name[20];
	char msg[BUFFSIZE];
	int id;
};

// Define struct for saving user information
typedef struct checkInfo {
	char userID[20];
	char message[512];
        char messageTime[20];
        int messageID;
	// Check the number of user in chat room
	int user_count;
	// Variable for saving user name
	char log[10][20];
	char chat[2][BUFFSIZE];
} CHAT_INFO;

// Declare two instances of message_buffer struct for saving in & out message
struct message_buffer buff_in;
struct message_buffer buff_out;

// Declare for getting time
time_t now;
struct tm t;

char userID[20];
// Declare Flag variable that indicates state now
int is_running;
// Declare chat information in global variable
CHAT_INFO *chatInfo;
// Declare global variable for saving time now
char time_now[50];

// Semaphore for shared memory
sem_t *sem_w;

// mutex, condition for controlling fetch and display
pthread_mutex_t *mut;
pthread_cond_t *cond;

// Define functions
void print_chat();
void *get_input();
void chat();
void cleanup();
void die(char *msg);
void *show_time();
void *show_account();
void *FetchMessageFromShmThread();
void *DisplayMessageThread();
void *autochat();

// Fuction for removing shared memory
void shmRemove(int shmid){

	if(shmctl(shmid, IPC_RMID, 0) < 0){
		printf("Failed to delete shared memory\n");
		exit(-1);
	}
	else {
		printf("Successfully delete shared memory\n");
	}

	return;
}

int main(int argc, char* argv[]) {

	// Declare for saving shared memory id
	int shmID;
	
	// Semaphore memory allocation
	sem_w = (sem_t *)malloc(sizeof(sem_t));

	// Allocate memory 10 times of checkInfo struct to chatInfo 
	chatInfo = (CHAT_INFO *)malloc(sizeof(struct checkInfo) * 10);

	// Type casting from int to void*
	void *shmaddr = (void *) 0;

	// if user doesn't type name, the error message appears
	if(argc < 2){
		fprintf(stderr, "[Usage] : ./chat UserID \n");
		exit(-1);
	}

	// Saving user name in userID variable
	strcpy(userID, argv[1]);

	// Create shared memory id which key is 20200421 exclusively
	shmID = shmget((key_t)9507, sizeof(CHAT_INFO)*100, 0666|IPC_CREAT|IPC_EXCL);

	// If there's already shared memory exists, don't create and just get shared memory id
	if(shmID < 0){
		shmID = shmget((key_t)9507, sizeof(CHAT_INFO)*100, 0666);
	}

	// Set shared memory address
	shmaddr = shmat(shmID, (void *)0, 0000);

	// Set shared memory address in chatInfo
	chatInfo = (CHAT_INFO *) shmaddr;

	// saving user name in account list, buffer in name
	strcpy(chatInfo->log[chatInfo->user_count], userID);
	strcpy(buff_in.name, userID);
	// Increase user count that indicates the number of memebers in chat room
	chatInfo->user_count++;

	// unlink semaphore
	sem_unlink("semW");

	// If first user open the chat window, then semaphore is opened exclusively
	// If there already user in chat room, then semaphore is just opened
	if (chatInfo->user_count == 1)	
		sem_w = sem_open("semW", O_CREAT | O_EXCL, 0644, 1);
	else
		sem_w = sem_open("semW", O_CREAT, 0644, 1);

	// If semaphore open is failed, print error message and remove shared memory
	if (sem_w==SEM_FAILED){
		shmRemove(shmID);
		perror("sem failed");
		exit(-1);
	}

	// Memory allocation for mutex and condition
	mut = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	cond = (pthread_cond_t *)malloc(sizeof(pthread_cond_t));
	// Initialize condition
	pthread_cond_init(cond, NULL);

	// Initiating screen
	initscr();

	// Call chat function
	chat();

	// If user is out, Destroy mutex and condtion
	pthread_mutex_destroy(mut);
	free(mut);
	pthread_cond_destroy(cond);
	free(cond);

	// If the last user is out, Remove shared memory and unlink semaphore
	if(chatInfo->user_count==0){
		printf("bye\n");
		shmRemove(shmID);
		sem_unlink("semW");
	}

	return 0;
}

void chat(){
	// Set windows' size
	base_scr = newwin(24, 80, 0, 0);
	output_scr = subwin(base_scr, 20, 60, 0, 0);
	input_scr = subwin(base_scr, 4, 60, 20,0);
	account_scr = subwin(base_scr, 20, 20, 0, 60);
	time_scr = subwin(base_scr, 4, 20, 20, 60);

	// Delete Cursor
	curs_set(0);

	// Set box line in subwindows
	box(output_scr, ACS_VLINE, ACS_HLINE);
	box(input_scr, ACS_VLINE, ACS_HLINE);
	box(account_scr, ACS_VLINE, ACS_HLINE);
	box(time_scr, ACS_VLINE, ACS_HLINE);

	// Refresh subwindows
	wrefresh(output_scr);
	wrefresh(input_scr);
	wrefresh(account_scr);
	wrefresh(time_scr);

	// Set scroll ok
	scrollok(output_scr, TRUE);

	// Print message on output screen and draw box line, refresh
	mvwprintw(output_scr, 0, 1, "\n ***** Type /bye to quit!! ***** \n\n");
	box(output_scr, ACS_VLINE, ACS_HLINE);
	wrefresh(output_scr);

	// Initialize buffer ids and flag
	buff_in.id = 0;
	buff_out.id = 0;
	is_running = 1;

	// Declare thread for controlling chat program
	pthread_t thread[6];

	// Create 6 threads
	pthread_create(&thread[0], NULL, get_input, NULL);
	pthread_create(&thread[1], NULL, show_time, NULL);
	pthread_create(&thread[2], NULL, show_account, NULL);
	pthread_create(&thread[3], NULL, FetchMessageFromShmThread, NULL);
	pthread_create(&thread[4], NULL, DisplayMessageThread, NULL);
	pthread_create(&thread[5], NULL, autochat, NULL);

	// Wait until all thread is finished
	pthread_join(thread[0], NULL);
	pthread_join(thread[1], NULL);
	pthread_join(thread[2], NULL);
	pthread_join(thread[3], NULL);
	pthread_join(thread[4], NULL);
	pthread_join(thread[5], NULL);

	// Delete all windows
	die("finish");
}

void *get_input(){
	// Declare buffer for saving input message
	char tmp[BUFFSIZE];
	
	// Running until flag is changed
	while(is_running){
		// Get string from input screen to buffer
		mvwgetstr(input_scr, 1, 1, tmp);
		// Saving buffer's message to buff_in.msg
		sprintf(buff_in.msg, "%s", tmp);
		// if user type /bye
		// Change account list and refresh account screen
		if(strcmp(buff_in.msg, "/bye") == 0) {
			// We need to wait semaphore because this function uses shared memory
			sem_wait(sem_w);
			int i = 0;
			int j = 0;
			for(i=0; i<chatInfo->user_count; i++){
				if(strcmp(chatInfo->log[i], buff_in.name) == 0){
					for(j=i+1; j<chatInfo->user_count; j++){
						strcpy(chatInfo->log[j-1], chatInfo->log[j]);
						strcpy(chatInfo->log[j], "");
					}
				}
			}
			
			// Decrease user count
			chatInfo->user_count = chatInfo->user_count - 1;
			
			werase(account_scr);
			wrefresh(account_scr);
			// After using shared memory, semaphore is posted
			sem_post(sem_w);
			// Exit
			is_running = 0;
		}

		else{
			// We need to wait semaphore because this function uses shared memory
			sem_wait(sem_w);
			// Save user data in chatInfo
                       	strcpy(chatInfo[chatInfo->messageID].userID, buff_in.name);
                       	strcpy(chatInfo[chatInfo->messageID].messageTime, time_now);
                        strcpy(chatInfo[chatInfo->messageID].message, buff_in.msg);

                        // Increase messageID
                        chatInfo->messageID++;
			
			// Modify screen
			box(output_scr, ACS_VLINE, ACS_HLINE);
			wrefresh(output_scr);
			werase(input_scr);
        		box(input_scr, ACS_VLINE, ACS_HLINE);
	                wrefresh(input_scr);
			// After using shared memory, semaphore is posted
			sem_post(sem_w);
                }

		// Refresh output screen
		box(output_scr, ACS_VLINE, ACS_HLINE);
		wrefresh(output_scr);
		usleep(100);
	}
	return 0;
}

// Delete all windows
void cleanup(){
	delwin(base_scr);
	delwin(input_scr);
	delwin(output_scr);
	delwin(account_scr);
	delwin(time_scr);
	endwin();
}

// Delete all windows and exit
void die(char *s){
	delwin(input_scr);
	delwin(output_scr);
	delwin(account_scr);
	delwin(time_scr);
	endwin();
	perror(s);
}

// This function shows current time and elapsed time
void *show_time(){
	int hour, min, sec;
	int elapsed = 0;
	int count = 0;

	// Running until flag is changed
	while(is_running){
		// Get current time
		time(&now);
		t = *localtime(&now);

		// This function sleeps in 500ms so we have to set count 2
		// because 1s = 500ms
		if(count == 2){
			elapsed++;
			// Change elapsed time to time form
			sec = elapsed % 60;
			min = (elapsed / 60) % 60;
			hour = elapsed / 3600;
		
			// set count 0
			count = 0;
		}
		// Saving current time in form that we want
		strftime(time_now, sizeof(time_now), "%H:%M:%S", &t);

		// Print current time and elapsed time on time screen and refresh
		mvwprintw(time_scr, 1, 1, time_now);
		mvwprintw(time_scr, 2, 1, "%02d:%02d:%02d", hour, min, sec);
		//box(time_scr, ACS_VLINE, ACS_HLINE);
		wrefresh(time_scr);
		usleep(500000);
		// Increase count in 500ms
		count = count + 1;
	}
	return 0;
}

void *show_account(){
	// Running until the flag is changed
	while(is_running){
		int i=0;
		// Erase account list for showing account list's any changes
		werase(account_scr);
		// We need to wait semaphore because this function uses shared memory
		sem_wait(sem_w);
		// Print account list on account screen using user count
		for(i=0; i<chatInfo->user_count; i++){
			mvwprintw(account_scr, i+1, 1, chatInfo->log[i]);
		}
		// After using shared memory, semaphore is posted
		sem_post(sem_w);
		// Refresh account screen
		box(account_scr, ACS_VLINE, ACS_HLINE);
		wrefresh(account_scr);
		sleep(1);
	}
	return 0;
}


void *autochat(){
	char buf[BUFFSIZE];

	while (is_running) {
		// We need to wait semaphore because this function uses shared memory
		sem_wait(sem_w);
		// If user is Jico
		// save sentence and Jico's data in chatInfo
        	if (strcmp(buff_in.name, "Jico")==0){
			strcpy(buf, "Hello!! My name is Jico I love to sing any song-");
			sprintf(buff_in.msg, "%s%d", buf, num);
			strcpy(chatInfo[chatInfo->messageID].message, buff_in.msg);
               		strcpy(chatInfo[chatInfo->messageID].userID, buff_in.name);
       			chatInfo->messageID++;
			num++;
		}
		// If user is Izzy
		// save sentence and Izzy's data in chatInfo
		else if (strcmp(buff_in.name, "Izzy")==0){
			strcpy(buf, "Hi!! I am Izzy I like to play on the stage-");
			sprintf(buff_in.msg, "%s%d", buf, num);
			strcpy(chatInfo[chatInfo->messageID].message, buff_in.msg);
               		strcpy(chatInfo[chatInfo->messageID].userID, buff_in.name);
			chatInfo->messageID++;
			num++;
		}
		// After using shared memory, semaphore is posted
		sem_post(sem_w);
		// sleep between 1s ~ 2s
        	usleep(((rand() % 1001) + 1000) * 1000);
	}
	
	return 0;
}

void *FetchMessageFromShmThread(){
	
	char buf[BUFFSIZE];

        while(is_running){
		// If there is an unfetched data
		// start fetch
		if(msg_cnt != chatInfo->messageID){
			// Lock ciritical section for saving receive message
                	pthread_mutex_lock(mut);
			// We need to wait semaphore because this function uses shared memory
			sem_wait(sem_w);
                	sprintf(buf, " %s : %s",
                        	chatInfo[msg_cnt].userID, chatInfo[msg_cnt].message);
			strcpy(buff_out.msg, buf);
			// After using shared memory, semaphore is posted
			sem_post(sem_w);
			// Send condtion to Display function to make it work
                	pthread_cond_signal(cond);
			// Unlock ciritical section
       			pthread_mutex_unlock(mut);
		}
        }

	return 0;
}

void *DisplayMessageThread(){

        while(is_running){
		// If there is an undisplayed data
		// start display
		if(msg_cnt != chatInfo->messageID){
			// Lock ciritical section for displaying receive data
                	pthread_mutex_lock(mut);
			// Wait condition from Fetch function
                        pthread_cond_wait(cond, mut);
			
			// We need to wait semaphore because this function uses shared memory
			sem_wait(sem_w);
			wprintw(output_scr, " %s\n", buff_out.msg);
			// Modify message count
			msg_cnt++;
			// Refresh output screen and input screen
			box(output_scr, ACS_VLINE, ACS_HLINE);
			wrefresh(output_scr);
			// After using shared memory, semaphore is posted
			sem_post(sem_w);
			// Unlock ciritical section
                	pthread_mutex_unlock(mut);
		}
        }

	return 0;
}






