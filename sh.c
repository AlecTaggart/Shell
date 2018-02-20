/*
Supports all unix commands. 
Supports piping
Supports file redirection
Supports history command/time keeping and use
Supports directory control
Supports '&&' and '||' command control
*/


#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include "list.h"

#define MAX_CMD_LEN 512
#define HISTORY_SIZE 10

int num_commands = 0;
struct list list;

char **build_args(char command[]);
int check_command(char command[]);
int run_unix_cmd(char command[]);

/*split contains split type and left and right side buffers*/
struct split{
    int type;
    char left[MAX_CMD_LEN];
    char right[MAX_CMD_LEN];
};

/*history node contains process timing number and command buffer*/
struct history_node{
    struct list_elem elem;
    int number;
    double time;
    char command[MAX_CMD_LEN];
};

/*used to kill current process*/
void control_c(){
    return;
}

/*mallocs and builds history node*/
struct history_node *history_node_new(int number, double time, char command[]){
    struct history_node *hn = (struct history_node *) malloc(sizeof(struct history_node));
    if (hn == NULL) {
        printf("history_node_new(): Cannot malloc. Exiting.\n");
        exit(-1);
    }        
    hn->number = number;
    hn->time = time;
    strcpy(hn->command, command);
    return hn;
}

/*compares given commands returning 1 if equal*/
int compare_command(char given_command[], char command[]){
    int done=0;
    for (int i = 0; i < 512; ++i){
        if(given_command[i] == command[i]){
            if(command[i+1] == '\0'){
                return 1;
            }
        }
        else{
            break;
        }
    }
    return 0;
}

/*compares command to number ignoring first value*/
int is_number(char command[]){
    char val = command[1];
    if(val >= '0' && val <= '9'){
        return 1;
    }
    return 0;
}

/*compares command to history / ! prefix*/
int is_history(char command[]){
    char history[8] = "history\0";
    char bang[2] = "!\0";
    if(compare_command(command, history) == 1){
        return 1;
    }
    if(compare_command(command, bang) == 1){
        if(is_number(command)){
            return 3;
        }
        return 2;
    }
    return 0;
}


/*compares command to cd prefix*/
int is_cd(char command[]){
    char cd[3] = "cd\0";
    return compare_command(command, cd);
}

/*processes command returning split object 
containing split type and respective left and right sides*/
struct split* is_multi_command(char command[]){
    struct split *split = (struct split*) malloc(sizeof(struct split));
    split->type = -1;
    int left_pos = 0, right_pos = 0;
    char input[1] = "<";
    char carrot[1] = ">";
    char pipe[1] = "|";
    char and[1] = "&";
    char or[1] = "|";
    int size = snprintf(NULL, 0, "%s", command);
    char left[size];
    char right[size];

    for (int i = 0; i < size; ++i){
        if(split->type == -1){
            if(command[i] == carrot[0]){
                split->type = 1;
                i++;
                if(command[i] == carrot[0]){
                    split->type = 2;
                    i++;
                }
                if(command[i] == ' '){
                    i++;
                }
            }
            else if(command[i] == input[0]){
                split->type = 3;
                i++;
                if(command[i] == ' '){
                    i++;
                }
            }
            else if(command[i] == pipe[0]){
                split->type = 4;
                i++;
                if(command[i] == ' '){
                    i++;
                }
            }
            else if(command[i] == and[0]){
                i++;
                if(command[i] == and[0]){
                    split->type = 5;
                    i++;
                }
                if(command[i] == ' '){
                    i++;
                }
            }else if(command[i] == or[0]){
                i++;
                if(command[i] == or[0]){
                    split->type = 6;
                    i++;
                }
                if(command[i] == ' '){
                    i++;
                }
            }
        }
        if(split->type <= 0){
            left[left_pos] = command[i];
            left_pos++;
        }
        else{
            right[right_pos] = command[i];
            right_pos++;
        }
    }
    left[left_pos] = '\0';
    right[right_pos] = '\0';
    strcpy(split->left, left);
    strcpy(split->right, right);
    return split; 
}

/*reads in new command from stdin*/
void read_line(char *line){
    char buf[1];
    int i = 0;

    while(read(0, buf, 1) > 0) {
        if (buf[0] == '\n') {
            break;
        }
        line[i] = buf[0];
        i++;
        if (i > (MAX_CMD_LEN - 1)) {
            break;
        }
    }
    line[i] = '\0';
    return;
}

/*prints out shell prompt*/
void print_prompt(){
    int size = snprintf(NULL, 0, "%d", num_commands);
    char buf[6 + size];
    snprintf(buf, sizeof(buf), "[%d] $ ", num_commands);
    write(1, buf, sizeof(buf));
}


/*changes current working directory*/
int cd(char command[]){
    char* token = strtok(command, " ");
    token = strtok(NULL, " ");
    if(token == NULL){
        return chdir("/home");
    }
    return chdir(token);
}


/*builds an array of command args and returns*/
char** build_args(char command[]){
    int i = 0;
    size_t size;
    char *token = strtok(command, " ");
    char **args = malloc(4*128); /*128 max arg pointer count*/
    args[i] = token;
    token = strtok(NULL, " ");
    while(i++, token != NULL) {
        if(token == "|" || token == ">" || token == ">>"){
            break;
        }
        args[i] = token;
        token = strtok(NULL, " ");
    }
    args[i] = token;
    return args;
}


/*runs history depending on type. 1=History 2=!string 3=!number*/
int history(int type, char *prefix){
    struct list_elem *e;
    struct history_node *hn;
    int prefix_num;
    if(type == 1){
        for (e = list_begin(&list); e != list_end(&list); e = list_next(e)) {
            hn = list_entry(e, struct history_node, elem);
                int size = snprintf(NULL, 0, "%d [%lf seconds] %s\n", hn->number, hn->time, hn->command);
                char output[size++];
                snprintf(output, size, "%d [%lf seconds] %s\n", hn->number, hn->time, hn->command);
                write(1, output, size);
        }
        return 0;
    }
    else if(type > 1){
        if(type == 3){
            prefix_num = atoi(prefix);
        }
        for (e = list_rbegin(&list); e != list_begin(&list); e = list_prev(e)) {
            hn = list_entry(e, struct history_node, elem);

            if(type == 2 && compare_command(hn->command, prefix) == 1){
                check_command(hn->command);
                return 0;
            }
            else if(type == 3 && hn->number == prefix_num){
                check_command(hn->command);
                return 0;
            }
        }    
    }return -1;
}

/*returns the prefix of given command*/
void get_prefix(char command[], char *prefix){
    int i;
    for(i = 1; i < strlen(command); i++){
        if(command[i] == '\n'){
            break;
        }
        prefix[i-1] = command[i];
    }
    prefix[i-1] = '\0';
}

/*opens a file buffer depending on split type and checks command using output buffer*/
int file_redirect(struct split* split){
    pid_t id;
    int fd, run;
    int type = split->type;

    if(type == 1){
        if ((fd = open(split->right, O_CREAT | O_WRONLY | O_TRUNC, 0644)) < 0) {
            printf("cannot open out buffer: exit.\n");
            exit(1);
        }
    }
    else if(type == 2){
        if ((fd = open(split->right, O_CREAT | O_WRONLY | O_APPEND, 0644)) < 0) {
            printf("cannot open out buffer: exit.\n");
            exit(1);
        }
    }else{
        if ((fd = open(split->right, O_RDONLY, 0644)) < 0) {
            printf("cannot open out buffer: exit.\n");
            exit(1);
        }
    }

    id = fork();

    if (id == 0) {
        if(type < 3){
            close(1);
            dup(fd);  
            close(fd);
        }else{
            close(0);
            dup(fd);
            close(fd);
        }
        run = check_command(split->left);
        exit(0);
    }
    else{
        close(fd);
        id = wait(NULL);
    }return run;
}

/*Takes in split and checks command on each side redirecting output through pipe*/
int pipe_redirect(struct split* split){
    pid_t id;
    int fildes[2], run = 0;
    pipe(fildes);
    id = fork();

    if (id == 0) {
        close(1);
        dup(fildes[1]);
        close(fildes[1]);
        close(fildes[0]);
        exit(check_command(split->left));
    }

    id = fork();

    if (id == 0 && run == 1) {
        close(0);
        dup(fildes[0]);
        close(fildes[0]);
        close(fildes[1]);
        exit(check_command(split->right));
    }

    close(fildes[0]);
    close(fildes[1]);
    id = wait(NULL);
    // printf("FIRST id = %d\n", id);
    id = wait(NULL);

    return run;
}

/* FINISH THIS */
int multi_command(struct split* split){
    int type = split->type;
    int id = fork();
    if(type == 5){
        if(id == 0){
            if(check_command(split->left) == 0){
                id = fork();
                if(id == 0){
                    check_command(split->right);
                }

            }
        }
    }
    else if(type == 6){

    }
}

/*Runs given unix command*/
int run_unix_cmd(char command[]){
    char **args;
    int run;
    pid_t id;
    id = fork();
    if(id == 0){
        args = build_args(command);
        if(run = execvp(args[0], args), run != -1){
            run = 0;
        }
        free(args);
        printf("%s: command not found\n", command);
        exit(0);
    }
    else{
        id = wait(NULL);
    }return run;
}

/*Type checks command calling appropriate functions. Creates and adds node to history list.*/
int check_command(char command[]){
    clock_t start, end;
    double cpu_time_used;
    struct split* split;
    int type, run, time_it = 1;
    num_commands++;

    struct history_node *hn = history_node_new(num_commands, 0, command);
    if(num_commands > 10){
        free(list_pop_front(&list));
    }
    list_push_back(&list, &hn->elem);

    /*always check for a split before any other command so we can seperate command calls*/
    if(split = is_multi_command(command), split->type > 0){
        if(split->type <= 3){
            printf("%s\n", "File Redirect");
            start = clock();
            run = file_redirect(split);
            end = clock();
        }
        else{
            printf("%s\n", "Pipe Redirect");
            start = clock();
            run = pipe_redirect(split);
            printf("Pipe Exited with %d status\n", run);
            end = clock();
        }
    }
    else if(is_cd(command) == 1){
        printf("%s\n", "cd command.");
        start = clock();
        if(run = cd(command), run != 0){
            printf("%s\n", "'cd' failed to find directory.");
        }
        end = clock();
    }
    else if(type = is_history(command), type > 0){
        printf("%s\n", "history command");
        time_it = 0;
        start = clock();
        char prefix[strlen(command)];
        get_prefix(command, prefix);
        if(run = history(type, prefix), run != 0){
            printf("%s\n", "'history' failed to find command.");
        } 
        end = clock();
 
    }
    else{
        printf("%s %s\n", "UNIX command", command);
        start = clock();
        run = run_unix_cmd(command);
        printf("UNIX exited with %d status.\n", run);
        end = clock();
    }
    hn->time = ((double) (end - start)) / CLOCKS_PER_SEC;
    return run;
}

/*reads and processes command calling check command if valid non exit*/
bool process_one_command(){

    bool done = false;
    char command[MAX_CMD_LEN];
    print_prompt();
    read_line(command);
    
    if((int)(command[0]) == 0){
        return done;
    }
    if(compare_command(command, "exit") == 1){
        done = true;
    }
    else if(command[0] == 0x0C){
        printf("%s\n", "found ctl-L");
        check_command("clear\0");
    }
    else{
        check_command(command);
    }
    return done;
}

/*main*/
int main(int argc, char **argv){
    signal(SIGINT, control_c);
    bool done = false;
    list_init(&list);

    while (!done) {
        done = process_one_command();
    }
    return 0;
}

//control L stopped working even though passing it the correct value.
//right now im trying to get a exit status from each command this way i can implement && and ||
//HOWEVER i dont know how to get exit status out of the children i fork 
//currently no pipe commands work because of this ask benson how to do this then fix the code.
