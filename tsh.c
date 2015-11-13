/* 
 * tsh - A tiny shell program with job control
 * Name = Ben Shaughnessy
 * Email = bshaughn@hawk.iit.edu
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	    break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	    break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	    break;
	default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

	/* Read command line */
	if (emit_prompt) {
	    printf("%s", prompt);
	    fflush(stdout);
	}
	if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	    app_error("fgets error");
	if (feof(stdin)) { /* End of file (ctrl-d) */
	    fflush(stdout);
	    exit(0);
	}

	/* Evaluate the command line */
	eval(cmdline);
	fflush(stdout);
	fflush(stdout);
    } 

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
   char *argv[MAXARGS];
   char *p = cmdline;

   if(strchr(cmdline, '|')) {
        //there is a pipe charactar in this command
        strsep(&p, "|"); //the | char in the command line is replaced with
                         //a \0 and p points to the subsequent char

        //for keeping track of the pipe
        int fds[2];
        pid_t pid1, pid2;
        pipe(fds);

        /* Now use parseline() to parse the two halfs of the command */
        parseline(cmdline, argv);
        
        if((pid1 = fork()) == 0) {
            //pre pipe call
            dup2(fds[1], 1);
            close(fds[1]);
            //exec...
            if(execv(argv[0], argv) < 0) {
                printf("Error, Unknown command");
                exit(0);
            }
       
        }
        close(fds[1]);

        //the reading part of the pipe
        parseline(p, argv);
        if((pid2 = fork()) == 0)  {
            //post pipe call
            dup2(fds[0], 0);
            //exec...
            if(execv(argv[0], argv) < 0) {
              printf("Error, Unknown command");
              exit(0);
            }
        }
        //waitpid(pid2, NULL, 0);
	    addjob(jobs, pid1, FG, cmdline); //adds a job to the job structure, the third arguement
	    addjob(jobs, pid2, FG, cmdline); //adds a job to the job structure, the third arguement

        waitfg(pid1);
        waitfg(pid2);
       
   }
   else if (strchr(cmdline, '>')) {
     char *argv[MAXARGS]; //for getting the argvs before the symbol
     char *fname[MAXARGS]; //for getting the args after the synbol, which should just be the filename
                           //it is kinda a hackish implimentation of getting the filename
     char *p = cmdline; 
     //redirect standard output
        strsep(&p, ">"); //the < char in the command line acts as a split where p has the contents after <
        parseline(p, fname); //getting the filename

        int fd = open(fname[0], O_CREAT|O_TRUNC|O_RDWR, 0644); //opening the file
        pid_t pid1; //pid for the process that will be run in this case
        parseline(cmdline, argv); //getting the command args before the carrot

        if((pid1 = fork()) == 0) { //creating the child process
            dup2(fd, 1); //changing standard Out to be the fd of the file above
            if(execv(argv[0], argv) < 0) { //execting the file
                fprintf(stderr, "Error, Unknown command\n");
                exit(0);
            }
        }

	    addjob(jobs, pid1, FG, cmdline); //adds a job to the job structure, the third arguement
        waitfg(pid1);
        close(fd);
   }
   else if (strchr(cmdline, '<')) {
        char *argv[MAXARGS];
        char *fname[MAXARGS]; //for getting the args after the synbol, which should just be the filename
        
        char *p = cmdline;
        //redirect standard input
        strsep(&p, "<"); //the > char in the command line acts as a split where p has the contents after <
        parseline(p, fname);

        int fd = open(fname[0], O_RDONLY, 0644);
        pid_t pid1; //pid for the child process that will run in this case
        parseline(cmdline, argv); //getting the command line args before the <

        if((pid1 = fork()) == 0) {
            dup2(fd, 0); //changing standard input of the process to be the fd of the open file
            if(execv(argv[0], argv) < 0) {
                fprintf(stderr, "Error, Unknown command\n");
                exit(0);
            }
        }

        addjob(jobs, pid1, FG, cmdline);
        waitfg(pid1);
        close(fd);

   } else {

   int bg = parseline(cmdline, argv);
   pid_t pid;
   if(argv[0] == NULL) {
        return;
   } 
    setpgrp();
    if(!builtin_cmd(argv)) {
	    //forking and execing a child process
	    if ((pid = fork()) == 0) {
	    	if(execv(argv[0], argv) < 0) {
	    		printf("Command Not Found!\n");
	    		exit(0);
	    	}
	    }
	    addjob(jobs, pid, bg?BG:FG, cmdline); //adds a job to the job structure, the third arguement
					      //just sets the type to a FG/BG job depending on the value
					      //of the variable bg
	    if(!bg) {
	    	waitfg(pid);
	    } else {
	    	printf("Job [%d] (%d) %s", getjobpid(jobs, pid)->jid , pid, cmdline);
	    }
    } 
   } 
    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) /* ignore spaces */
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    if (strcmp("quit", argv[0]) == 0) {
	exit(0);
    }
    else if (strcmp("fg", argv[0]) == 0) {
	do_bgfg(argv);
	//printf("Run job in FG\n");
	return 1;
    }
    else if (strcmp("bg", argv[0]) == 0) {
	do_bgfg(argv);
	//printf("Run job in BG\n");
	return 1;
    }
    else if (strcmp("jobs", argv[0]) == 0) {
	//printf("Print out the jobs\n");
	listjobs(jobs);
	return 1;
    }
    return 0;     /* not a builtin command */
}

/* 
 * stringToInt - Converts a string to an int
 */
int stringToInt(char *str) {
	int returnInt = 0;
	int i = 0; 
	for(i = 0; str[i] != '\0'; i++) {
		returnInt *= 10;
		returnInt += (str[i] - 48);
	}
	
	return returnInt;
}



/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    int jobID;
    pid_t PID;
    struct job_t * JOB;
    int isFG;
    if(strcmp("fg", argv[0]) == 0) {
	isFG = 1;
	if(argv[1] &&  argv[1][0] == '%') {
		jobID = stringToInt(argv[1] + 1);
		//printf("second arguement is good for FG with JID of %d \n", jobID);
		JOB = getjobjid(jobs, jobID);
	}
	else if(argv[1] && (argv[1][0] - 48) >= 0 && (argv[1][0] - 48) < 10) {	
		PID = stringToInt(argv[1]);
		//printf("second arguement is good for FG with PID of %d \n", PID);
		JOB = getjobpid(jobs, PID);
	}
	else {
		printf("Invalid  arguments for fg\n");
		return;
	}	
    } 
    else if (strcmp("bg", argv[0]) == 0) {
	isFG = 0;
	if(argv[1] && argv[1][0] == '%') {
		jobID = stringToInt(argv[1] + 1);
		//printf("Second arguement is good for BG with JID of %d \n", jobID);
		JOB = getjobjid(jobs, jobID);
	}
	else if(argv[1] && (argv[1][0] - 48) >= 0 && (argv[1][0] - 48) < 10) {	
		PID = stringToInt(argv[1]);
		//printf("second arguement is good for BG with PID of %d \n", PID);
		JOB = getjobpid(jobs, PID);
	}
	else {
		printf("Invalid arguements for bg\n");
		return;
	}
    }
    if(JOB) {
	kill(JOB->pid, SIGCONT);
	if(!isFG) {
		JOB->state = BG;
		printf("Job [%d] (%d) %s", JOB->jid, JOB->pid, JOB->cmdline);
	} else {
		JOB->state = FG;
		waitfg(JOB->pid);
	}
	//printf("JOB: %d %d %d\n", JOB->state, JOB->jid, JOB->pid);
    } else {
	printf("Could not find that JOB\n");
	return;
    }
    return;
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    while (fgpid(jobs) == pid) {
	sleep(1);
    }
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
   // kill(0, SIGINT);
    //pid_t pid = wait(NULL); //reap a single child. Bad implementation
    pid_t pid;  
    while((pid = waitpid((pid_t)(-1), 0, WNOHANG)) > 0) {
    	deletejob(jobs, pid);
    }
    return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    //printf("sigint received\n");
    pid_t pid;
    if(!(pid = fgpid(jobs)) == 0) {
	int jobID = getjobpid(jobs, pid)->pid;
	if(deletejob(jobs, pid) == 1) {
		//printf("Job killed\n");
		printf("Job [%d] (%d) Terminated by signal 2\n", jobID, pid);//, getjobpid(jobs, pid)->jid , pid);
	} else {
		printf("could not kill the job\n");
	}
    }
    return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    //printf("Control Z pressed");
    pid_t pid;
    pid = fgpid(jobs);
    struct job_t * JOB;// = getjobpid(jobs, pid);
    //if(!(pid = fgpid(jobs)) && (JOB = getjobpid(jobs, pid))) {
     JOB = getjobpid(jobs, pid);
     JOB->state = ST;
     kill(pid, SIGTSTP);
    printf("Job [%d] (%d) Terminated by signal 20\n", JOB->jid , pid);
    //} else {
//	printf("No fg Job to stop %d \n", pid);
    //}
    //kill(pid, SIGTSTP);
    //if(!(pid = fgpid(jobs)) == 0) {
	//getjobpid(jobs, pid).state = ST;
    //}
    return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    int i;

    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid > max)
	    max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == pid) {
	    clearjob(&jobs[i]);
	    nextjid = maxjid(jobs)+1;
	    return 1;
	}
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    int i;
    
    for(i = 0; i < MAXJOBS; i++) {
	if (jobs[i].state == FG)
	    return jobs[i].pid;
    }
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    int i;

    if (pid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid)
	    return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    int i;

    if (jid < 1)
	return NULL;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].jid == jid)
	    return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    int i;

    if (pid < 1)
	return 0;
    for (i = 0; i < MAXJOBS; i++)
	if (jobs[i].pid == pid) {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    int i;
    
    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid != 0) {
	    printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
	    switch (jobs[i].state) {
		case BG: 
		    printf("Running ");
		    break;
		case FG: 
		    printf("Foreground ");
		    break;
		case ST: 
		    printf("Stopped ");
		    break;
	    default:
		    printf("listjobs: Internal error: job[%d].state=%d ", 
			   i, jobs[i].state);
	    }
	    printf("%s", jobs[i].cmdline);
	}
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}



