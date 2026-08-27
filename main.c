#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <readline/readline.h>
#include <stdbool.h>



#First step is to start with file redirecting
int main(){
    char* input = readline("# ");
    for (int i = 0; i<strlen(input); i++){
        if (input[i] == '\n'){
            input[i] = '\0';
        }
    }
    char* new = strtok(input, " ");
    int c = 0;
    char **args = malloc(2000*sizeof(char*));
    bool firstcommand = false;
    while (new){
        //printf("%s", new);
        //printf("%s %d\n", new, firstcommand);
        if(strcmp(new, "<") != 0 && !firstcommand){
            args[c] = new;
        }
        if(strcmp(new, "<") == 0){
            firstcommand = true;
        }
        new = strtok(NULL, " ");
        c += 1;
    }
    for (int i = 0; i<sizeof(args); i++){
        if (args[i] == NULL){
            break;
        }
        printf("%s \n", args[i]);
    }

}