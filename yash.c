#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
//print array function

int bg_pids[200] = {-1};
int current_bg_amount = 0;

void printarray(char** arr){
    for(int i = 0; arr[i] != NULL; i++){
        if (arr[i] == NULL){
            break;
        }
        printf("%s\n", arr[i]);
    }
}

typedef struct{
    char **args;
    char* inFilename;
    char* outFilename;
    char* errFilename;

    bool isBG;
    bool used_stdin;
    bool used_stdout;
    bool used_stderr;
    bool piping; 

} command;

typedef struct {
    bool fg;
    char* status;
    char* command;
    int pgid;
    int jobid; 
} Job;


Job* jobStack[200] = {0};
Job* finishedJobs[200] = {0};
int amount_of_jobs = 0;
int current = 0;
int next_jobID = 1;
Job* currentForeground = NULL;

/*processes a regular command (with file redirection) and returns a command*
command* has file redirection information, background job info, and piping info.*/

command* processCommand(char* input){
    const char a[] = " ";
    char* innerPtr = NULL;
    char* tokens = strtok_r(input, a, &innerPtr);

    int c = 0; 
    char **args = calloc(sizeof(char*), 200);
    command* newcommand = malloc(sizeof(command));
    newcommand->used_stdin = true;
    newcommand->used_stdout = true;
    newcommand->used_stderr = true;
    newcommand->args = args;
    newcommand->isBG = false;

    bool redirection = false;

    //variable used to check if command is a background job
    char* temp;

    while (tokens != NULL) {
        if (strcmp(tokens, "<") == 0 ||
            strcmp(tokens, ">") == 0 ||
            strcmp(tokens, "2>") == 0) {

            char* operator = tokens;
            char* filename = strtok_r(NULL, a, &innerPtr);

            if (filename == NULL) {
                fprintf(stderr, "Missing filename after %s\n", operator);
                free(args);
                free(newcommand);
                return NULL;
            }

            if (strcmp(operator, "<") == 0) {
                newcommand->inFilename = filename;
                newcommand->used_stdin = false;
            } else if (strcmp(operator, ">") == 0) {
                newcommand->outFilename = filename;
                newcommand->used_stdout = false;
            } else {
                newcommand->errFilename = filename;
                newcommand->used_stderr = false;
            }
        } else if (strcmp(tokens, "&") != 0) {
            args[c++] = tokens;
        }

        tokens = strtok_r(NULL, a, &innerPtr);
    }


    return newcommand;
}


//change STDIN,OUT, and ERROR via dup. NO ACTUAL EXECUTION
int executeFileRedirection(command* cmd){
    if (!cmd->used_stdin){
        int ifd = open(cmd->inFilename, O_RDONLY);
            
        //error handling for robustness

        if(ifd == -1){
            perror("opening error");
            return -1;
        }
    

        dup2(ifd, STDIN_FILENO);
    }
    if (!cmd->used_stdout){
        int ofd = open(cmd->outFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        dup2(ofd, STDOUT_FILENO);
    }
    if (!cmd->used_stderr){
        int efd = open(cmd->errFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        dup2(efd, STDERR_FILENO);
    }
    return 0;
}

//executes an actual command using execvp
void executeRegularCommand(command* cmd){

    int b = executeFileRedirection(cmd);
    if (b==-1){
        exit(1);
    }
    execvp(cmd->args[0], cmd->args);
}

//processing/parses the piping command given input string
command** processInput(char* input){
    //bg flag to create dummy bg command variable at the end of the function
    bool bg = false;
    if (input[strlen(input)-1] == '&'){
        bg = true;
    }

    //----INPUT PARSING----
    char* outerPtr = NULL;
    command** subcmds = calloc(sizeof(command*),200);
    char* outertoken = strtok_r(input, "|", &outerPtr);
    int c = 0;
    //piping
    while(outertoken!=NULL){
        subcmds[c] = processCommand(outertoken);
        outertoken = strtok_r(NULL, "|", &outerPtr);
        c++;
    }
   //----INPUT PARSING----

    //check if the given input is a background job and append a background job command to the end of the commands list.
    if (bg) {
        command* newcommand = malloc(sizeof(command));
        newcommand->isBG = true;
        subcmds[c] = newcommand;
        newcommand = NULL;
    }
    return subcmds;
}
//find the length of the commands** type array.
int length(command** cmds){
    int c = 0;
    for (int j = 0; cmds[j] != NULL; j++){
        c += (!(cmds[j]->isBG)) ? 1 : 0;
    }
    return c;
}

void remove_pid(int pid){
    bool flag = false;
    for (int j = 0; j<amount_of_jobs; j++){
        if (jobStack[j]->pgid == pid){
            flag = true;
        }
        if (flag){
            jobStack[j] = jobStack[j+1];
        }
    }
    amount_of_jobs--; 
    if (amount_of_jobs==0 && currentForeground != NULL){
        next_jobID = currentForeground->jobid+1;
    } else if (amount_of_jobs==0 && currentForeground == NULL){
        next_jobID = 1;
    } else {
        next_jobID = jobStack[amount_of_jobs-1]->jobid+1;
    }
}




//checks if a command has piping or not given input string
bool isPiping(command** scmd){
    int c = 0;
    while(c<length(scmd)){
        c+=1;
    }
    if (c==1) {return false;}
    else {return true;}
}
//executes the piping command given array of sub commands. 
void executePiping(command** cmds){
    int prev_fd = -1;
    int pids[200] = {-1};
    int num_pids = 0;
    //printf("%d", length(cmds));
    for (int j = 0; j<length(cmds); j++){
        //printf("HELLO W");
        int pipefd[2] = {-1, -1};
        //No need to create pipe for the last command
        if (j < length(cmds)-1){
            pipe(pipefd);
        }
        int cpid1 = fork();
        //create a job object and set its fg = true;

        if (cpid1 == 0){ //child
            if (prev_fd != -1){//read end of the previous pipe
                dup2(prev_fd, STDIN_FILENO);
                close(prev_fd);
            }
            if(pipefd[0] != -1){
                close(pipefd[0]); //close the unused read end of the current pipe
            }
            if(j < length(cmds)-1){//wire STDOUT of current child to write end of the current pipe
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[1]);
            }
            if (executeFileRedirection(cmds[j])==-1){
                exit(1);
            }
            execvp(cmds[j]->args[0], cmds[j]->args);
            //perror(cmds[j]->args[0]);
            //_exit(127);
            exit(1);
        }
        //parent process closing its read and write end copies (read end of the previous pipe and write end of current pipe)
        if (prev_fd!=-1){
            close(prev_fd);
        }
        if (j < length(cmds)-1){
            close(pipefd[1]);
        }
        prev_fd = pipefd[0];
        pids[num_pids] = cpid1;
        num_pids++;
    }
    //wait for all child processes to finish.
    //NEED TO MODIFY LATER
    for (int i = 0; i<num_pids; i++){
        int status;
        waitpid(pids[i], &status, 0);
    }
}

//starts the background job
void printFinishedJobs(){
    for (int i = current-1; i>=0; i--){
        printf("[%d] - Done       %s\n",
        finishedJobs[i]->jobid,
        finishedJobs[i]->command
        );
        free(finishedJobs[i]);
        finishedJobs[i] = 0;
        current--;
    }
}

//continues most recently stopped bg job via "bg" command
void continue_bg(){
    if (amount_of_jobs==0){
        return;
    }
    for (int i = amount_of_jobs-1; i>=0; i--){
        if (strcmp(jobStack[i]->status, "Stopped")==0){
            jobStack[i]->status = "Running";
            
            printf("[%d] %c %s       %s\n", jobStack[i]->jobid, 
                    amount_of_jobs-i == 1 ? '+' : '-',
                    jobStack[i]->status, 
                    jobStack[i]->command);

            kill(-(jobStack[i]->pgid), SIGCONT);
            break;
        }
    }
}

void bring_to_fg(){
    if (amount_of_jobs==0){
        return;
    }
    signal(SIGTTOU, SIG_IGN); //required when using tcsetpgrp
    pid_t shell_pgid = getpgrp(); //get pgid of shell to give control back after fg process finishes
    
    currentForeground = jobStack[amount_of_jobs-1]; //set the currentForeground process. 
    currentForeground->status = "Running";

    //tcsetpgrp(0, currentForeground->pgid); //set terminal control to current foreground process

    printf("[%d] %c %s       %s\n", currentForeground->jobid, 
                    '+',
                    currentForeground->status, 
                    currentForeground->command);
    
    tcsetpgrp(0, currentForeground->pgid);
    remove_pid(jobStack[amount_of_jobs-1]->pgid);
    currentForeground->fg = true;


    kill(-currentForeground->pgid, SIGCONT);

    int status;
    pid_t result = waitpid(currentForeground->pgid, &status, WUNTRACED);
    if(result > 0){
        if (WIFSTOPPED(status)){//child process was STOPPED not TERMINATED.
            currentForeground->status="Stopped";
            jobStack[amount_of_jobs++] = currentForeground;
            currentForeground->fg = false;
        }
    }
    tcsetpgrp(0, shell_pgid);
}

void SIGCHLD_handler(int signum, siginfo_t* info, void *context){

    if (info->si_code != CLD_EXITED && info->si_code != CLD_KILLED && info->si_code != CLD_DUMPED){
        return;
    }

    bool flag = false;
    for (int i = 0; jobStack[i] != NULL; i++){
        if (jobStack[i]->pgid == info->si_pid){
            flag = true;
            finishedJobs[current] = jobStack[i];
            finishedJobs[current]->status = "Done";
            current++;
        }
    }
    if (flag){
        remove_pid(info->si_pid);
    }
}
void print_jobs(){
        for(int i = 0; i<amount_of_jobs; i++){
            printf("[%d] %c %s       %s\n", jobStack[i]->jobid, 
                    i == amount_of_jobs-1 ? '+' : '-',
                    jobStack[i]->status, 
                    jobStack[i]->command);
        }
}
int main(){
    //read main input

    //handling SIGCHLD
    struct sigaction sa;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = SIGCHLD_handler;
    sigaction(SIGCHLD, &sa, NULL);

    signal(SIGTTOU, SIG_IGN);

    int shell_pgid = getpgrp();

    while (1){
        char* input = readline("# ");
        char* temp_input = strdup(input);
        for (int i = 0; i<strlen(input);i++){
            if (input[i] == '\n'){
                input[i] = '\0';
            }
        }
        if (strlen(input) == 0){
            printFinishedJobs();
            continue;
        }
        else if (strcmp(input, "fg")==0){
            bring_to_fg();
            continue;
        } else if (strcmp(input, "bg") == 0){
            continue_bg();
            continue;
        }
        else if (strcmp(input, "jobs")==0){
            print_jobs();
            continue;
        } else {
            // PROCESS/PARSE commands 
            command** scmd = processInput(input);
            int j = 0;
            while (scmd[j+1]!=NULL){j++;}
            // bg flag to check if current command is a background job
            bool bg = scmd[j]->isBG;
            // START execution
            if(isPiping(scmd)){
                executePiping(scmd);
            } else {
                command* cmd = scmd[0];

                int cpid = fork();
                //PARENT CREATES JOB OBJECT AND PUSHES INTO JOB STACK
                if(cpid != 0){
                    setpgid(cpid, cpid); //create process group with id=cpid and make the process with id=cpid the head of that group. 
                    Job* j1 = malloc(sizeof(Job));
                    j1->pgid = cpid;
                    j1->status = "RUNNING";
                    j1->jobid = next_jobID; 
                    j1->command = temp_input;
                    if(bg==1){
                        j1->fg = false;
                        jobStack[amount_of_jobs++] = j1;
                    } else {
                        j1->fg = true;
                        currentForeground = j1;
                        tcsetpgrp(0, currentForeground->pgid);
                    }
                
                    next_jobID = j1->jobid+1;
                    j1 = NULL;
                }
                if (cpid == 0){
                    setpgid(0,0); //setpgid
                    executeRegularCommand(cmd);
                    exit(1);
                } else {
                    if(!bg){
                        int status;
                        pid_t result = waitpid(currentForeground->pgid, &status, WUNTRACED);
                        if(result > 0){
                            if (WIFSTOPPED(status)){//child process was STOPPED not TERMINATED.
                                currentForeground->status="Stopped";
                                jobStack[amount_of_jobs++] = currentForeground;
                                currentForeground->fg = false;
                            }
                        }
                        tcsetpgrp(0, shell_pgid);
                    }
                }
            }
        }
    
    }
}