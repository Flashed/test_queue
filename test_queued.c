#include <stdio.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#define RUNNING_DIR "/tmp"
#define MESSAGES_DIR "/tmp/messages"
#define LOCK_FILE "test_queued.lock"
#define LOG_FILE "test_queued.log"
#define QUEUE_FILE "/dev/queue_pop"


void log_message(char *filename,char *message){
    FILE *logfile;
    logfile = fopen(filename,"a");
    if(!logfile)
        return;
    fprintf(logfile,"%s\n",message);
    fclose(logfile);
}

void signal_handler(int sig){
    switch(sig){
    case SIGHUP:
        log_message(LOG_FILE,"Hangup Signal Catched");
        break;
    case SIGTERM:
        log_message(LOG_FILE,"Terminate Signal Catched");
        exit(0);
        break;
    }
}

void daemonize(){
    int i,lfp;
    char str[10];

    if(getppid() == 1)
        return;
    i = fork();

    if(i < 0)
        exit(1);
    if(i > 0)
        exit(0);
    setsid();

    for(i = getdtablesize(); i >= 0; --i)
        close(i);

    i = open("/dev/null",O_RDWR);
    dup(i);
    dup(i);
    umask(027);

    chdir(RUNNING_DIR);

    lfp = open(LOCK_FILE,O_RDWR|O_CREAT,0640);
    if(lfp < 0)
        exit(1);
    if(lockf(lfp,F_TLOCK,0) < 0)
        exit(1);

    sprintf(str,"%d",getpid());
    write(lfp,str,strlen(str));

    signal(SIGCHLD,SIG_IGN);
    signal(SIGTSTP,SIG_IGN);
    signal(SIGTTOU,SIG_IGN);
    signal(SIGTTIN,SIG_IGN);

    signal(SIGHUP,signal_handler);
    signal(SIGTERM,signal_handler);
}

int read_message(char *to, char *from)
{
    int fd_from, fd_to= -1;
    char buf[4096];
    ssize_t nread, copied = 0;

    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;

    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;

        if (fd_to == -1){
        	fd_to = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        	if (fd_to < 0)
        		goto out_error;
        }

        do {
            nwritten = write(fd_to, out_ptr, nread);

            if (nwritten >= 0)
            {
                nread -= nwritten;
                copied += nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }

    if (nread == 0)
    {
    	if (fd_to != -1)
    	{
			if (close(fd_to) < 0)
			{
				fd_to = -1;
				goto out_error;
			}
    	}

		close(fd_from);

        return copied;
    }

  out_error:

    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);

    return -1;
}

int main(int argc,char **argv){

	int lfp, len=10, mess_count = 0, mess_size = 0;
	char pid_str[len], mess_file_name[125], log_mess[125];
	pid_t pid;
	if (argc > 1){
		if (strcmp(argv[1], "stop") == 0){
			lfp = open(RUNNING_DIR "/" LOCK_FILE,O_RDONLY,0640);
			if(lfp < 0){
				printf("Failed to open lock file\n");
				exit(1);
			}
			if(read(lfp, pid_str, len) < 0){
				printf("Failed to read lock file\n");
				exit(1);
			}
			pid = atoi(pid_str);
			kill(pid, SIGTERM);
			printf("Successfully stoped!\n");
			exit(0);
		}
	}

	daemonize();
	//create messages dir
	struct stat st = {0};

	if (stat(MESSAGES_DIR, &st) == -1) {
	    mkdir(MESSAGES_DIR, 0700);
	}

    while(1){
    	sprintf(mess_file_name, MESSAGES_DIR"/message_%d", mess_count);
    	if ((mess_size = read_message(mess_file_name, QUEUE_FILE)) < 0){
    		log_message(LOG_FILE, "Failed to read message from "QUEUE_FILE);
    	} else if (mess_size > 0){
    		mess_count ++;
    		sprintf(log_mess, "Read %s message.", mess_file_name);
    		log_message(LOG_FILE, log_mess);
    		continue;
    	}
    	sleep(1);
    }

}
