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
    int status; //i.e. running/stopped (0,1)
    char* command;
    int pgid;
} Job;


Job* jobStack[200] = {0};
int current = 0;
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

        //error handling for robustness
        /*
        if(ifd == -1){
            perror("opening error");
        }
        */

        dup2(ofd, STDOUT_FILENO);
    }
    if (!cmd->used_stderr){
        int efd = open(cmd->errFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);

        //error handling for robustness
        /*
        if(ifd == -1){
            perror("opening error");
        }
        */

        dup2(efd, STDERR_FILENO);
    }
    return 0;
}

//executes an actual command using execvp
void executeRegularCommand(command* cmd){
    //printf("HELLO WORLD");
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
            //printf("HELLO");
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

void startBackgroundJob(char* inputcmd){
                            

}

void signal_callback_handler(int signum) {
    printf("Caught signal");

    //revert to default
    struct sigaction sb;
    sb.sa_flags = 0;
    sigemptyset(&sb.sa_mask);
    sb.sa_handler = SIG_DFL;
    sigaction(SIGTSTP, &sb, NULL);
    //push to background stack
    jobStack[current] = currentForeground;
    jobStack[current]->status = 1; //stopped status = 1
    jobStack[current]->fg = false;

    //send signal to pgid
    kill(-currentForeground->pgid, SIGTSTP);

    //clear the currentForeground
    currentForeground = NULL;



}
void print_jobs(){
        for(int i = current-1; i>=0; i--){
            if(jobStack[i]->status != 2){
                printf("[%d] %c %s       %s\n", current-i, 
                        current-i == 1 ? '+' : '-',
                        jobStack[i]->status==1 ? "Stopped" : "Running", 
                        jobStack[i]->command);
            }
        }
}
int main(){
    //read main input
    char* input = readline("# ");
    char* temp_input = strdup(input);
    char* outerPtr = NULL;



    while (input){

        for (int i = 0; i<strlen(input);i++){
            if (input[i] == '\n'){
                input[i] = '\0';
            }
        }

        command** scmd = processInput(input);
        if(isPiping(scmd)){
            //printf("WHY");
            executePiping(scmd);
        } else {
            command* cmd = scmd[0];
            int cpid = fork();
            printf("%d", cpid);
            //background job

            if (cpid == 0){
                //printf("hello");
                int indexInTable = current-1;
                setpgid(0,0); //setpgid
                executeRegularCommand(cmd);
                exit(1);
            } else {
                int status;
                waitpid(-1, &status, 0);
            }
        }

        input = readline("# ");
    }
}