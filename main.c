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


void printarray(char** arr){
    for(int i = 0; arr[i] != NULL; i++){
        if (arr[i] == NULL){
            break;
        }
        printf("%s\n", arr[i]);
    }
}
//First step is to start with file redirecting
int main(){
    char* input = readline("# ");

    char* innerPtr = NULL;
    char* outerPtr = NULL;

    while(input){

        //Replace newline with NULL
        for (int i = 0; i<strlen(input); i++){
            if (input[i] == '\n'){
                input[i] = '\0';
            }
        }

        //Tokenize the input
        char* outertoken = strtok_r(input, "|", outerPtr);

        while (outertoken!=NULL){
            const char a[] = " ";
            char* ptr = strtok_r(outer, a, innerPtr);

            //Create args list on heap.
            int c = 0;
            char **args = calloc(sizeof(char*), 2000);

            while (ptr!=NULL){

                if (strcmp(ptr, "<")==0){ //INPUT REDIRECTION
                    ptr = strtok_r(NULL, a, innerPtr);
                    char* filename = ptr;
                    ptr = strtok_r(NULL, a, innerPtr);
                    //printf("%s", ptr);
                    int ifd = open(filename, O_RDONLY);
                    if (ifd == -1) {
                        perror("open");   // will print e.g. "open: No such file or directory"
                    }
                    //printf("%s\n", filename);
                    //printarray(args);
                    int cpid = fork();

                    //input redirection
                    if (cpid == 0) { // child
                        if (ptr != NULL && strcmp(ptr, ">")==0){//OUTPUT AFTER INPUT

                            ptr = strtok_r(NULL, a, innerPtr);
                            char* output_filename = ptr;


                            int ofd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);;
                            dup2(ofd, STDOUT_FILENO);


                        } else if (ptr != NULL && strcmp(ptr, "2>")==0){//ERROR AFTER INPUT


                            ptr = strtok_r(NULL, a, innerPtr);
                            char* output_filename = ptr;


                            int ofd = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);;
                            dup2(ofd, STDERR_FILENO);
                        }
                        dup2(ifd, STDIN_FILENO);
                        execvp(args[0], args);
                        exit(1);
                    } else {
                        int status;
                        waitpid(-1, &status, 0);
                        //fflush(stdout);
                    }
                    break;


                } else if (strcmp(ptr, ">")==0){ //OUTPUT REDIRECTION
                    //Get filename
                    ptr = strtok_r(NULL, a, innerPtr);
                    char* filename = ptr;

                    //output file descriptor
                    int ofd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);;
                    //printf("hello");

                    //fork 
                    int cpid = fork();

                    if (cpid == 0){ // CHILD
                        dup2(ofd, STDOUT_FILENO);
                        execvp(args[0], args);
                        exit(1);
                    } else {
                        int status;
                        waitpid(-1, &status, 0);
                    }
                    break;
            
                } else if (strcmp(ptr, "2>")==0){ //ERROR REDIRECTION
                    //Get filename
                    ptr = strtok_r(NULL, a, innerPtr);
                    char* filename = ptr;

                    //output file descriptor
                    int ofd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);;
                    //printf("hello");

                    //fork 
                    int cpid = fork();

                    if (cpid == 0){ // CHILD
                        dup2(ofd, STDERR_FILENO);
                        execvp(args[0], args);
                        exit(1);
                    } else {
                        int status;
                        waitpid(-1, &status, 0);
                    }
                    break;
            

                }


                args[c] = ptr;
                c+=1;
                ptr = strtok_r(NULL, a, innerPtr);
            }
        }
        input = readline("# ");
    }
}