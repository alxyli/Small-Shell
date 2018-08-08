/* 
 * Alex Li
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

// command line input limits
#define MAX_LEN 2048
#define MAX_ARGS 513

// bool definition (for compiling without C99 flag)
typedef int bool;
#define true 1
#define false 0

// prototypes
void CheckForProcessID(char* userInput);
void ParseInputToArgv(char* userInput, char** arguments, char* outputFile, char* inputFile, int* bgProcess);
void CheckBGProcessTermination(int* exitStatus, pid_t* childProcesses, int numBGProcess);
void KillRemainingChildren(pid_t* childProcesses, int numBGProcess); // this is great
void CatchSIGTSTP(int signo);
void RunShell();

// global to toggle foreground only mode
bool foregroundOnly = false;

int main() {
	RunShell();
	return 0;
}

void CheckForProcessID(char* userInput) {
	bool processIDs = true;
	do {
		// get occurrences of $$
		char* pidToken = strstr(userInput, "$$");
		if (pidToken != NULL) {
			char buffer[512] = {'\0'};
			char pid[10] = {'\0'};
			
			// determine indices to replace with process ID
			int pidIdx = (int)(pidToken - userInput) + 1;
			int startIdx = pidIdx + 1;
			int endIdx = strlen(userInput);
		
			// copy input following $$ into buffer
			strncpy(buffer, userInput + startIdx, endIdx - startIdx);

			// replace $$ in string with process ID
			sprintf(pid, "%d", (int)getpid());
			strncpy((userInput + pidIdx - 1), pid, sizeof(pid));

			// reappend buffer to userInput string
			strcat(userInput, buffer);
		}

		// done processing userInput for $$
		else processIDs = false;

	} while (processIDs);
}

void ParseInputToArgv(char* userInput, char** arguments, char* outputFile, char* inputFile, int* bgProcess) {
		char* token;
		int numArgs = 0, i = 0;

		// parse userInput command into tokens
		token = strtok(userInput, " ");
		while (token != NULL) {
			// store output redirect filename and grab next token
			if (strcmp(token, ">") == 0) {
				token = strtok(NULL, " ");
				strcpy(outputFile, token);
				token = strtok(NULL, " ");
			}

			// store input redirect filename and grab next token
			else if (strcmp(token, "<") == 0) {
				token = strtok(NULL, " ");
				strcpy(inputFile, token);
				token = strtok(NULL, " ");
			}

			// handle background process
			else if (strcmp(token, "&") == 0) {
				token = strtok(NULL, " ");
				// toggle bgProcess flag if & is last token
				if (token == NULL && !foregroundOnly)
					*bgProcess = true;
			}

			// parse and continue adding to argument array
			else {
				arguments[numArgs] = strdup(token);
				token = strtok(NULL, " ");
				numArgs++;
			}
		}

		// add NULL to end of arguments array for execvp call
		arguments[numArgs] = NULL;
}

void CheckBGProcessTermination(int* exitStatus, pid_t* childProcesses, int numBGProcess) {
	pid_t childPid;
	int status, i;

	// check for any completed background processes
	for (i = 0; i < numBGProcess; i++) {
		while ((childPid = waitpid(childProcesses[i], &status, WNOHANG)) > 0) {
			// background process exited normally
			if (WIFEXITED(status)) {
				// retrieve and output pid/exit status
				*exitStatus = WEXITSTATUS(status);
				printf("background pid %d is done: exit value %d\n", childPid, *exitStatus);

				// set childProcesses[i] to -5 when terminated
				childProcesses[i] = -5;
			}
			
			// background process terminated by signal
			else if (WIFSIGNALED(status)) {
				// retrieve termiating signal number
				*exitStatus = WTERMSIG(status);
				printf("background pid %d is done: terminated by signal %d\n", childPid, *exitStatus);

				// set childProcesses[i] to -5 when terminated
				childProcesses[i] = -5;
			}

			fflush(stdout);
		}
	}
}

void KillRemainingChildren(pid_t* childProcesses, int numBGProcess) {
	int i;
	for (i = 0; i < numBGProcess; i++) {
		if (childProcesses[i] > 0)
			kill(childProcesses[i], SIGKILL);
	}
}

void CatchSIGTSTP(int signo) {
	if (foregroundOnly == true) {
		char* fgState = "\nExiting foreground-only mode\n";
		write(1, fgState, 30);
		foregroundOnly = false;
	}

	else {
		char* fgState = "\nEntering foreground-only mode (& is now ignored)\n";
		write(1, fgState, 50);
		foregroundOnly = true;
	}
}

void RunShell() {
	char userInput[MAX_LEN];
	char* arguments[MAX_ARGS];
	pid_t childProcesses[100]; // keep track of unterminated pids
	pid_t childPid = -5;
	int result = 0, status = 0, retStatus = -5, numBGProcess = 0, i;
	int fdOut = -1;
	int fdIn = -1;
	bool bgProcess = false;
	bool shellRunning = true;

	struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};

	// ignore SIGINT in parent process
	SIGINT_action.sa_handler = SIG_IGN;
	sigfillset(&SIGINT_action.sa_mask);
	SIGINT_action.sa_flags = 0;
	sigaction(SIGINT, &SIGINT_action, NULL);

	// set up SIGTSTP handler
	SIGTSTP_action.sa_handler = CatchSIGTSTP;
	sigfillset(&SIGTSTP_action.sa_mask);
	SIGTSTP_action.sa_flags = 0;
	sigaction(SIGTSTP, &SIGTSTP_action, NULL);

	while (shellRunning) {
	
		// check for background process termination
		CheckBGProcessTermination(&retStatus, childProcesses, numBGProcess);

		// clear file redirect strings on new prompt
		char outputFile[20] = {'\0'};
		char inputFile[20] = {'\0'};

		// clear userInput buffer before new prompt
		memset(userInput, 0, MAX_LEN);

		// display : and get user input
		printf(": ");
		fflush(stdout);
		fgets(userInput, MAX_LEN, stdin);

		// strip newline, replace with null terminator
		char* newline = strchr(userInput, '\n');
		if (newline)
			*newline = '\0';

		// exit shell when user types exit (no input processing necessary)
		if ((strcmp(userInput, "exit") == 0) || (strcmp(userInput, "exit &") == 0)) {
			// suggested by Prof. Brewster
			// kill remaining processes, use waitpid on each after
			KillRemainingChildren(childProcesses, numBGProcess);
			CheckBGProcessTermination(&retStatus, childProcesses, numBGProcess);
			shellRunning = false;
			continue;
		}

		// display last exit value or signal number of terminated foreground process
		if ((strcmp(userInput, "status") == 0) || (strcmp(userInput, "status &") == 0)) {
			if (WIFSIGNALED(status)) {
				retStatus = WTERMSIG(status);
				printf("terminated by signal %d\n", retStatus);
			}

			else {
				retStatus = WEXITSTATUS(status);
				printf("exit value %d\n", retStatus);
			}

			fflush(stdout);
			continue;
		}

		// if user enters blank line or a space, restart prompt
		if (strcmp(userInput, "") == 0) 
			continue;

		if (strcmp(userInput, " ") == 0)
			continue;

		// if user enters a comment line, restart prompt
		if (userInput[0] == '#')
			continue;

		// parse and process userInput if none of the above
		// search and replace $$ with process ID if present in userInput
		CheckForProcessID(userInput);

		// parse userInput and store tokens into arguments array
		ParseInputToArgv(userInput, arguments, outputFile, inputFile, &bgProcess);

		// check argument array for cd command
		if (strcmp(arguments[0], "cd") == 0) {
			// ignore & if it was entered
			bgProcess = false;

			// go to home directory when cd is encountered without arguments
			if (arguments[1] == NULL) {
				char* homeDir = getenv("HOME");
				chdir(homeDir);
			}

			// go to specified dir otherwise
			else
				chdir(arguments[1]);
		}
		
		// fork and exec non built-in command
		else {
			childPid = fork();
			switch (childPid)
			{
				case -1:
				perror("Error forking!");
				exit(1);
				break;

				case 0:
				// set up input file redirection
				if (inputFile[0] != '\0') {
					fdIn = open(inputFile, O_RDONLY);
					if (fdIn == -1) {
						perror("inputFile open()");
						exit(1);
					}
					result = dup2(fdIn, 0);
					if (result == -1) {
						perror("inputFile dup2 error");
						exit(1);
					}
				}

				// set up output file redirection
				if (outputFile[0] != '\0') {
					fdOut = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
					if (fdOut == -1) {
						perror("outputFile open()");
						exit(1);
					}
					result = dup2(fdOut, 1);
					if (result == -1) {
						perror("outputFile dup2 error");
						exit(1);
					}
				}

				// redirect stdin to /dev/null if no target given for background processes
				if (inputFile[0] == '\0' && bgProcess == true) {
					fdIn = open("/dev/null", O_RDONLY);
					if (fdIn == -1) {
						perror("/dev/null open()");
						exit(1);
					}
					result = dup2(fdIn, 0);
					if (result == -1) {
						perror("/dev/null input dup2 error");
						exit(1);
					}
				}

				// redirect stdout to /dev/null if no target given for background processes
				if (outputFile[0] == '\0' && bgProcess == true) {
					fdOut = open("/dev/null", O_WRONLY);
					if (fdOut == -1) {
						perror("/dev/null open()");
						exit(1);
					}
					result = dup2(fdOut, 1);
					if (result == -1) {
						perror("/dev/null output dup2 error");
						exit(1);
					}
				}

				// ignore SIGTSTP in child processes
				SIGTSTP_action.sa_handler = SIG_IGN;
				sigaction(SIGTSTP, &SIGTSTP_action, NULL);

				// restore SIGINT default behavior in foreground child
				if (!bgProcess) {
					SIGINT_action.sa_handler = SIG_DFL;
					sigaction(SIGINT, &SIGINT_action, NULL);
				}

				// execute command
				status = execvp(arguments[0], arguments);
				if (status < 0) {
					perror("-smallsh: command not found");
					retStatus = 1;
					exit(1);
				}
				break;

				default:
				// execute command in foreground if & is not specified or if currently in foreground only mode
				if (!bgProcess || foregroundOnly) {
					waitpid(childPid, &status, 0);

					// immediately print signal number if killed
					if (WIFSIGNALED(status)) {
						retStatus = WTERMSIG(status);
						printf("terminated by signal %d\n", retStatus);
						fflush(stdout);
					}

					// otherwise save exit status for status command
					else
						retStatus = WEXITSTATUS(status);
				}

				// print pid of background process when it begins
				else {
					// add background process to array for termination tracking
					childProcesses[numBGProcess] = childPid;
					numBGProcess++;
					printf("background pid is %d\n", childPid);
					fflush(stdout);
					bgProcess = false;
				}

				break;
			}
		}
	}	
}