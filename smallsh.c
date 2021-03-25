#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <limits.h>
#include <sys/wait.h>
#include <signal.h>

// Global variable to set state for SIGTSTP
bool allowBG = true;

/* Struct for the command line */
struct commandLine {
    char *command;
    char *argv[513]; // max of 512 arguments +1 for null pointer
    char *redirectionSymbols[513]; // Stores < and >. Size is abitrary
    char *redirectionFiles[513]; // Stores file names. Size is arbitrary
    bool isBackground; // Boolean to detect ampersand
};

/*
* Frees the memory in the command line struct
*/
void freeCommandStruct(struct commandLine *cmdLine) {
    char** args = cmdLine->argv;
    char** symbs = cmdLine->redirectionSymbols;
    char** files = cmdLine->redirectionFiles;

    free(cmdLine->command);

    while (*args != NULL) {
        free(*args);
        args++;
    }

    while (*symbs != NULL) {
        free(*symbs);
        symbs++;
    }

    while (*files != NULL) {
        free(*files);
        files++;
    }

    free(cmdLine);
}

/*
* Prints the exit status of the last foreground process
*/
void printStatus(int status) {
    printf("exit value %d\n", status);
}

/*
* Changes the current directory to the directory specified
* by the path string. If path is NULL, current directory 
* will be changed to HOME environment variable
*/
void cdToPath(char* path) {
    // Initializes buffer
    char cwd[PATH_MAX + 1];

    // path will be set to HOME environment variable
    if (path == NULL) {
        path = getenv("HOME");
    }

    // Changes directory and print an error if it fails
    if (chdir(path) != 0) {
        perror("\nError: ");
        fflush(stdout);
    }
    // Checks the cwd after chdir is invoked
    getcwd(cwd, PATH_MAX + 1);
}

/*
* Prints the fields of the command line struct for testing
*/
void printCmdLine(struct commandLine *aLine) {
    // Prints the command
    printf("Print Command Line: ~~~~~~~~~~~~~~\n");
    fflush(stdout);
    printf("%s\n", aLine->command);
    fflush(stdout);

    // Prints the args
    char **argPtr = aLine->argv;
    while (*argPtr != NULL) {
        printf("arg: %s\n", *argPtr);
        fflush(stdout);
        argPtr++;
    }
    char **symPtr = aLine->redirectionSymbols;
    char **filePtr = aLine->redirectionFiles;
    while(*symPtr != NULL) {
        printf("%s %s\n", *symPtr, *filePtr);
        fflush(stdout);
        symPtr++;
        filePtr++;
    }
    printf("Is BG: %d\n", aLine->isBackground);
    fflush(stdout);

}


/*
* Returns the pointer of the first occurence of a redirection symbol.
* Otherwise it returns a null terminator
*/
char findFirstRedirect (char* line) {
    for (int i = 0; i < strlen(line); i++) {
        if (strchr("<>", line[i])) {
            return line[i];
        }
    }
    return '\0';
}


/*
* Finds all substrings of $$ and replaces them with the process ID of smallsh shell
*/
char* variableExpansion(char* str) {
    bool ignore = false;
    int p_id = getpid();
    char tempReplace[8];
    sprintf(tempReplace, "%d", p_id); 
    char* source = calloc(4000,  sizeof(char));
    // Pointer used to append characters to new string
    char* strPtr = str;
    char currChar;
    char nextChar;

    // Handles edge case of 1 single character
    if (strlen(str) == 2) {
        return str;
    }

    for (int i = 0; i < strlen(str); i++) {
        currChar = strPtr[0];
        nextChar = strPtr[1];

        // Ignores the second $ of $$ when concatenating
        if (ignore) {
            strPtr++;
            ignore = false;
            continue;
        // Performs variable expansion when $$ is found
        } else if (currChar == '$'&& nextChar == '$') {
                ignore = true;
                strncat(source, tempReplace, strlen(tempReplace));
        // Cocatatenates the non-expanded characters
        } else { 
            strncat(source, &currChar, 1);    
        }
        // Advances the pointer so it points to the next character
        strPtr++;
    }
    return source;
}

/*
* Returns true if the command enter is not a built in command
*/
bool nonBuiltCommand(struct commandLine *cmdLine) {
    // Returns true if it is not one of the built in commands
    if ((strcmp(cmdLine->command, "exit") != 0) && \
        (strcmp(cmdLine->command, "cd") !=  0) && \
        (strcmp(cmdLine->command, "status") != 0)
    ) {
        return true;
    }
    return false;
}

/* Adds a process ID to an array */
void addBGProcess(int *arr, int processID) {
    for (int i = 0; i < 513; i++) {
        if(arr[i] == -1) {
            arr[i] = processID;
            return;
        }
    }
    printf("Array too full\n");
} 

/* Adds a child process ID to an array */
void addChild(int *arr, int childID) {
    for (int i = 0; i < 1000; i++) {
        if(arr[i] == -1) {
            arr[i] = childID;
            return;
        }
    }
    printf("Array too full\n");
} 

/* Reaps all the remaining background processes */
void reapBackground(int  *arr) {
    int id;
    int childStatus; 
    pid_t childPid;
    for (int i = 0; i < 513; i++) {
        if ((id = arr[i]) != -1) {
            childPid = waitpid(id, &childStatus, WNOHANG);
            // Child pid is equal to id which means it exited
            if (childPid == id && WIFEXITED(childStatus)) {
               printf("background: %d is done: exit value: %d\n", childPid, WEXITSTATUS(childStatus));
               arr[i] = -1; 
            }
        }
    }
}

void reapChildProcess(int *arr) {
    int id;
    int childStatus;
    pid_t childPid;
    for (int i = 0; i < 1000; i++) {
        if ((id = arr[i]) != -1) {
            childPid = waitpid(id, &childStatus, WNOHANG);
            // Child process exists
            if (childPid >= 0) {
                // Kills the child process
               kill(id, 9);
            }
        }
    }
}

/* Signal handler for SIGTSTP */
void handle_SIGTSTP(int signo) {
    char *message;
    if (allowBG) {
        message = "\nEntering foreground-only mode (& is now ignored)\n";
        allowBG = false;
        write(STDOUT_FILENO, message, 50);
       
    } else {
        message = "\nExiting foreground-only mode\n";
        allowBG = true;
        write(STDOUT_FILENO, message, 30);
    }
}

/*
* Runs the non built in commands for the shell 
*/
void runCommand(struct commandLine *cmdLine, int* bgArr, int*childArr, int *status, void (*func)(int signo)) {

    char *newargv[514];
    char **newArgPtr = newargv;
    char **tempPtr = cmdLine->argv;
    pid_t childPid = -5;
    pid_t waitChildPID ;
    int childStatus;
    int in;
    int out;
    // Initialize default action struct
    struct sigaction default_action = {{0}};
    // Initialize SIGTSP action struct to match shell
    struct sigaction SIGTSTP_action = {{0}};
    // Set handler to handler that was passed from main
    SIGTSTP_action.sa_handler = func;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;

    // Creates a new array to be used with execvp
    *newArgPtr = calloc(strlen(cmdLine->command) + 1, sizeof(char));
    strcpy(*newArgPtr, cmdLine->command);
    newArgPtr++;

    //Copies the argument contents to be used with execvp
    while (*tempPtr != NULL) {
        *newArgPtr = calloc((strlen(*tempPtr) + 1), sizeof(char));
        strcpy(*newArgPtr, *tempPtr);
        newArgPtr++;
        tempPtr++;
    }
    // Sets the last argument in array to be NULl for execvp
    *newArgPtr = NULL;

    // Fork a new process
    childPid = fork();
    
    switch(childPid) {
        case -1:
            perror("fork()\n");
            fflush(stdout);
            exit(1);
            break;
        // In the child process
        case 0:
            // Sets the foreground process to have the default SIGINT action
            if (!cmdLine->isBackground && !allowBG) {
                // Setting the signal handler to be the default action
                default_action.sa_handler = SIG_DFL;
                // Changes the SIGINT signal back to default action
                sigaction(SIGINT, &default_action, NULL);
            }

            // Redirects input/output when no redirection is present and bg process
            if (cmdLine->isBackground && *cmdLine->redirectionSymbols == NULL && allowBG) {
                // Opens file stream for I/O redirection          
                in = open("/dev/null", O_RDONLY);
                out = open("/dev/null", O_WRONLY | O_TRUNC | O_CREAT);
                if (in == -1) {
                    perror("in");
                }
                if (out == -1) {
                    perror("out");
                }
                // // Redirects I/O to dev/null
                dup2(in, STDIN_FILENO);
                dup2(out, STDOUT_FILENO);

            // Redirects the input and output when redirection is present
            } else {
                char **symbol = cmdLine->redirectionSymbols;
                char **file = cmdLine->redirectionFiles;
                // Performs redirection for each file
                while (*symbol != NULL) {
                    // Handles the input redirection
                    if (strcmp(*symbol, "<") == 0) {
                        in = open(*file, O_RDONLY);
                        // Checks for file descriptor error
                        if (in == -1) {
                            printf("cannot open %s for input\n", *file);
                            fflush(stdout);
                            exit(1);
                        }
                        dup2(in, STDIN_FILENO);
                    } else if (strcmp(*symbol, ">") == 0) {
                        out = open(*file,  O_WRONLY | O_TRUNC | O_CREAT, 0640);
                        if (out == -1) {
                            perror(*file);
                            fflush(stdout);
                        }
                        dup2(out,STDOUT_FILENO);
                    }
                    symbol++;
                    file++;
                }
            }
            SIGTSTP_action.sa_handler = SIG_IGN;
            sigaction(SIGTSTP, &SIGTSTP_action, NULL);
            execvp(newargv[0], newargv);
            perror(newargv[0]);
            fflush(stdout);
            exit(1);
            break;
        default:
        //In the parent process:
            // Is a background process:
            if (cmdLine->isBackground && allowBG) {
                printf("background pid is %d\n",childPid);
                fflush(stdout);
                waitChildPID = waitpid(childPid, &childStatus, WNOHANG);
                // If child terminated, prints wait status
                if (waitChildPID > 0) {
                    printf("background : %d  is done: exit value %d", waitChildPID, WEXITSTATUS(childStatus));
                    fflush(stdout);
                // Child exists and has not terminated
                } else if (waitChildPID == 0){ 
                    // Adds child process ID to array
                    addBGProcess(bgArr, childPid);
                    addChild(childArr, childPid);
                }
            // Foreground process. 
            } else {
                //Parent process will wait until child terminates
                waitChildPID =  waitpid(childPid, &childStatus, 0);

                // Adds the children process to be killed at shell exit
                addChild(childArr, childPid);
                // Sets the status if child terminated normally
                if (WIFEXITED(childStatus)) {
                    *status = WEXITSTATUS(childStatus);
                // Child process terminates abnormally.
                }else if (WIFSIGNALED(childStatus)) {
                    printf("terminated by signal %d\n", WTERMSIG(childStatus));
                }
            }
    }
    // Frees the memory
    newArgPtr = newargv;
    while (*newArgPtr != NULL) {
        free(*newArgPtr);
        newArgPtr++;
    }
}

/*
* Input to be read is separated by a space between each
* entry. Entries will be put into the respective struct
* field. Each struct field will have a null terminator denoting
* the end of the array except for the command field.
*/
struct commandLine *parseCommandLine(char *line) {
    struct commandLine *cmdLine = malloc(sizeof(struct commandLine));
    bool containsRedirection = false;
    bool isBG = false;
    bool containsArgs = false;
    bool onlyCommand = false;
    char **argsPtr = cmdLine->argv;
    char **argsSymbs = cmdLine->redirectionSymbols;
    char **argsFiles = cmdLine->redirectionFiles;

    //Initializes all values to NULL pointer
    for (int i = 0; i < 513; i++) {
        *argsPtr = NULL;
        *argsSymbs = NULL;
        *argsFiles = NULL;
        argsPtr++;
        argsSymbs++;
        argsFiles++;
    }


    
    // Checks if malloc properly allocated memory
    if (cmdLine == NULL) {
        printf("Memory not allocated for commandLine\n");
        fflush(stdout);
    }

    // Finds if a redirection is present in command line
    char firstRedirect = findFirstRedirect(line);
    if (strchr(line, '<') || strchr(line, '>')) {
        containsRedirection = true;
    }

    // Finds if a & is present in the command line
    if (line[strlen(line) - 2] == '&') {
        isBG = true;
        cmdLine->isBackground = true;
    }

    char *parsedLine[2048];

    // To be used with strtok_r
    char *savePtr;
    // Replaces the newline chracter to be a space to be used with tokens
    line[strlen(line) - 1] = ' ';
    // Temp ptr to be used for storing all the tokens
    char **tempPtr = parsedLine;


    // First token
    char *token = strtok_r(line, " ", &savePtr);
    *tempPtr = calloc(strlen(token) + 1 , sizeof(char)); // FAILING RIGHT HERE~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    strcpy(*tempPtr, token);
    // Advances the pointer
    tempPtr++;

    // Stores each string from the input command line
    while (*savePtr != '\0') {
        token = strtok_r(NULL, " ", &savePtr);
        *tempPtr = calloc(strlen(token) + 1, sizeof(char));
        strcpy(*tempPtr, token); 
        tempPtr++;
    }
    // Sets the last pointer to be a null terminator to denote end
    *tempPtr = calloc(1, sizeof(char));


    // Sets the pointer to point to the start of tokenized elements
    tempPtr = parsedLine;
    cmdLine->command = calloc(strlen(*tempPtr) + 1, sizeof(char));
    strcpy(cmdLine->command, *tempPtr);
    tempPtr++;

    //Check if only the command or ampersand is present
    if (*tempPtr[0] == '\0' || *tempPtr[0] == '&') {
        onlyCommand = true;
    }

    //Checks if there are arguments in command line
    if (!onlyCommand && (strchr("<>&", *tempPtr[0]) == NULL)) {
        containsArgs = true;
    }

    argsPtr = cmdLine->argv;
    // Runs whens args or redirection is present
    if (!onlyCommand) {
        // Parsed input contains argumens
        if (containsArgs) {
            // Sets the stopping point
            char* lastChar;
            if (isBG) {
                lastChar = "&";
            } else {
                lastChar = "\0";
            }
            if (containsRedirection) {
                // Handles the parsed string up until redirection
                while (*tempPtr[0] != firstRedirect) {
                    *argsPtr = calloc(strlen(*tempPtr) + 1, sizeof(char));
                    strcpy(*argsPtr, *tempPtr);
                    argsPtr++;
                    tempPtr++;
                }
                // Changed to NULL 2/5 19:41
                // Sets the last pointer in the argument array to be NULL
                *argsPtr = NULL;

                // Puts the redirection input into the struct
                char **argsSymb = cmdLine->redirectionSymbols;
                char **argsFile = cmdLine->redirectionFiles;
                while (strcmp(*tempPtr, lastChar) != 0) {
                    // Copies the redirect symbol into struct
                    *argsSymb = calloc(strlen(*tempPtr) + 1, sizeof(char));
                    strcpy(*argsSymb, *tempPtr);
                    tempPtr++;
                    argsSymb++;
                    // Copies the file name into struct
                    *argsFile = calloc(strlen(*tempPtr) + 1, sizeof(char));
                    strcpy(*argsFile, *tempPtr);
                    tempPtr++;
                    argsFile++;
                }
                // Sets the last pointer in the argument array to be \0
                *argsSymb = NULL; //calloc(1, sizeof(char));
                *argsFile = NULL; //calloc(1, sizeof(char));
            } else { // Contains arg. No redirection
                while (strcmp(*tempPtr, lastChar) != 0) {
                    *argsPtr = calloc(strlen(*tempPtr) + 1, sizeof(char));
                    strcpy(*argsPtr, *tempPtr);
                    fflush(stdout);
                    argsPtr++;
                    tempPtr++;
                }
            }
        // Parsed input doesn't contain arguments but contains redirection
        } else {
            // Initialize lastChar to hold last character in user input
            char* lastChar;
            if (isBG) {
                lastChar = "&";
            } else {
                lastChar = "\0";
            }
            // Initialize pointers to the beginning of array of char pointers
            argsSymbs = cmdLine->redirectionSymbols;
            argsFiles = cmdLine->redirectionFiles;
            while (strcmp(*tempPtr, lastChar) != 0) {
                // Copies the redirect symbol into struct
                *argsSymbs = calloc(strlen(*tempPtr) + 1, sizeof(char));
                strcpy(*argsSymbs, *tempPtr);
                tempPtr++;
                argsSymbs++;
                // Copies the file name into struct
                *argsFiles = calloc(strlen(*tempPtr) + 1, sizeof(char));
                strcpy(*argsFiles, *tempPtr);
                tempPtr++;
                argsFiles++;
            }
        }
    }
    // Initialize this to false
    if (!isBG) {
        cmdLine->isBackground = false;
    }

    return cmdLine;
}


int main(int argc, char *argv[]){
    // Sets max size of command line to be 2048 + 1 for null ptr
    int lineSize = 2049;
    char* inputLine;
    int bgID[513];
    int status = 0;
    int childID[1000];

    // Initialize array for bgID with dummy variable
    for (int i = 0; i < 513; i++) {
        bgID[i] = -1;
    }

    // Initialize array for child process ID with dummy variable
    for (int i = 0; i < 1000; i++) {
        childID[i] = -1;
    }

    // Initialize ignore action struct
    struct sigaction ignore_action = {{0}};
    // Setting the signal handler to ingnore signals  
    ignore_action.sa_handler = SIG_IGN;
    // Block all catchable signals
    sigfillset(&ignore_action.sa_mask);
    // Changes the action of SIGINT signal to be ignored
    sigaction(SIGINT, &ignore_action, NULL);

    // Initialize SIGTSTP struct
    struct sigaction SIGTSTP_action = {{0}};
    // Setting signal handler 
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    // Block all catchable signals
    sigfillset(&SIGTSTP_action.sa_mask);
    // No flags set
    SIGTSTP_action.sa_flags = SA_RESTART;
    // Registers the handler for SIGTSTP
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    // // Command line loop for smallsh 
    do {
        // Prints the colon prompt and grabs input from user
        printf(": ");
        fflush(stdout);
        // resets the commandLine to be empty
        inputLine = calloc(lineSize,sizeof(char));
        // Reads the user input from keyboard into input line
        fgets(inputLine, lineSize, stdin);
        
        // Returns to the command line prompt when user enters comment or blank line or exit
        if ((strcmp(inputLine, "\n") == 0) || *inputLine == '#' || strcmp(inputLine, "exit\n") == 0) {
            reapBackground(bgID);
            continue;
        }

        // Expands the $$ instances
        char* parsedInputLine = variableExpansion(inputLine);
        // Parses the variable expanded command line input
        struct commandLine *cmdLine = parseCommandLine(parsedInputLine);

        //Executes the the non built in commands 
        if (nonBuiltCommand(cmdLine)) {
            runCommand(cmdLine, bgID, childID, &status, &handle_SIGTSTP);
        }

        // Handles the cd command
        if (strcmp(cmdLine->command, "cd") == 0) {
            cdToPath(cmdLine->argv[0]);
        }
        // Handles the status command
        if (strcmp(cmdLine->command, "status") == 0) {
            printStatus(status);
        }

        // Frees allocated memory on the heap
        freeCommandStruct(cmdLine);
        free(inputLine);
        reapBackground(bgID);

    } while (strcmp(inputLine, "exit\n") != 0);
    
    // Reaps all remaining child processes created by the shell
    reapChildProcess(childID);

    exit(EXIT_SUCCESS);
}


