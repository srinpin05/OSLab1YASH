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


//processes a regular command (with file redirection) and returns a command*
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
    newcommand->isBG = false;
    newcommand->args = args;


    bool redirection = false;

    //variable used to check if command is a background job
    char* temp;

    while (tokens!=NULL){
        
        if(strcmp(tokens, "<")==0){
            //set redirection flag
            redirection = true;
            tokens = strtok_r(NULL, a, &innerPtr);
            printf("%s \n", tokens);
            char* filename = tokens;

            newcommand->inFilename = filename;
            newcommand->used_stdin = false;

            
        } else if (strcmp(tokens, ">")==0){ //OUTPUT REDIRECTION
            //set redirection flag
            redirection = true;
            //Get filename
            tokens = strtok_r(NULL, a, &innerPtr);
            char* filename = tokens;

            newcommand->outFilename = filename;
            newcommand->used_stdout = false;

            
        } else if (strcmp(tokens, "2>") == 0){
            //set redirection flag
            redirection = true;
            tokens = strtok_r(NULL, a, &innerPtr);
            char* filename = tokens;

            newcommand->errFilename = filename;
            newcommand->used_stderr = false;
        }
        //stop taking arguments into the args list if redirection already happened
        if(redirection == false){
            args[c] = tokens;
            c+=1;
        }
        temp = tokens;
        tokens = strtok_r(NULL, a, &innerPtr);
    }
    if(strcmp(temp, "&")==0){
        newcommand->isBG=true;
    }


    return newcommand;
}


//execute a regular non piping command
void executeRegularCommand(command* cmd){
    if (cmd)

}


//checks if a command has piping or not given input string
bool isPiping(char* input){
    command** scmd = processPiping(input);

    int c = 0;
    while(scmd[c] != NULL){
        c+=1;
    }
    if (c==1) {return false;}
    else {return true;}
}
//processing/parses the piping command given input string
command** processPiping(char* input){
    char* outerPtr = NULL;
    char* outertoken = strtok_r(input, "|", &outerPtr);
    command** subcmds = calloc(sizeof(command*),200);
    int c = 0;

    //piping
    while(outertoken!=NULL){
        subcmds[c] = processCommand(outertoken);
        char* outertoken = strtok_r(NULL, "|", &outerPtr);
        c++;
    }
    return subcmds;
}
//executes the piping command given array of sub commands. 
void executePiping(command** cmds){
    int prev_fd = -1;
        for (int j = 0; cmds[j+1] != NULL; j++){
            int pipefd[2];
            if (cmds[j+1] != NULL){
                pipe(pipefd);
            }
            int cpid1 = fork();
            if (cpid1 == 0){ //child
                if (prev_fd != -1){//read end of previous child
                    dup2(prev_fd, STDIN_FILENO);
                    close(prev_fd);
                }
                if (!cmds[j]->used_stderr) {
                    int efd = open(cmds[j]->errFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(efd, STDERR_FILENO);
                }
                if (!cmds[j]->used_stdout) {
                    int ofd = open(cmds[j]->outFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(ofd, STDOUT_FILENO);
                } 
                if (!cmds[j]->used_stdin) {
                    int ifd = open(cmds[j]->inFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(ifd, STDIN_FILENO);
                }
                close(pipefd[0]); //close the unused read end
                dup2(pipefd[1], STDOUT_FILENO);
                execvp(cmds[j]->args[0], cmds[j]->args);
                exit(1);
            }
            prev_fd = pipefd[1];
        }
}

//starts the background job

void startBackgroundJob(char* inputcmd){
    command* bg = processCommand(inputcmd);
    if (isPiping(inputcmd)){
        int pid = fork();
        if (pid == 0){
            executePiping(processPiping(inputcmd));
            exit(1);
        }
        bg_pids[current_bg_amount] = pid;
        current_bg_amount += 1;

    } else {
        int pid = fork();
        if (pid == 0){
            executeRegularCommand(bg);
            exit(1);
        }
        bg_pids[current_bg_amount] = pid;
        current_bg_amount += 1;
    }

}

//ends the background job
void endBackgroundJob(){

}

int main(){
    //read main input
    char* input = readline("# ");

    char* outerPtr = NULL;

    while (input){

        for (int i = 0; i<strlen(input);i++){
            if (input[i] == '\n'){
                input[i] = '\0';
            }
        }

        

        char* outertoken = strtok_r(input, "|", &outerPtr);
        command** cmds = calloc(sizeof(command*),200);
        int c = 0;

        //piping
        while(outertoken!=NULL){
            cmds[c] = processCommand(outertoken);
            char* outertoken = strtok_r(NULL, "|", &outerPtr);
            c++;
        }
        int prev_fd = -1;
        for (int j = 0; cmds[j+1] != NULL; j++){
            int pipefd[2];
            if (cmds[j+1] != NULL){
                pipe(pipefd);
            }
            int cpid1 = fork();
            if (cpid1 == 0){ //child
                if (prev_fd != -1){//read end of previous child
                    dup2(prev_fd, STDIN_FILENO);
                    close(prev_fd);
                }
                if (!cmds[j]->used_stderr) {
                    int efd = open(cmds[j]->errFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(efd, STDERR_FILENO);
                }
                if (!cmds[j]->used_stdout) {
                    int ofd = open(cmds[j]->outFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(ofd, STDOUT_FILENO);
                } 
                if (!cmds[j]->used_stdin) {
                    int ifd = open(cmds[j]->inFilename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    dup2(ifd, STDIN_FILENO);
                }
                close(pipefd[0]); //close the unused read end
                dup2(pipefd[1], STDOUT_FILENO);
                execvp(cmds[j]->args[0], cmds[j]->args);
                exit(1);
            }
            prev_fd = pipefd[1];
        }



    }


}