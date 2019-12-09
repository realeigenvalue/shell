#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "format.h"
#include "log.h"

extern char *optarg; 
extern int optind;
extern int opterr;

char *history_file = NULL;
int h_flag = 0;
Log *history = NULL;
int quit = 0;
FILE *fp = NULL;
char *dir = NULL;
char *command_line = NULL;
char **oldargs = NULL;
char *background_index = NULL;

void init(int argc, char *argv[]);
char *read_command();
void dispatch(char *line);
int run_builtin(int argc, char **args, char *line);
int run_non_builtin(char **args, char *line);

void cd_builtin(char **args);
void exit_builtin();
void print_all_history_builtin();
void execute_from_history_builtin(char **args);
void prefix_from_history_builtin(char **args);

void load_history(char *h_file);
void load_script(char *c_file);

void signal_handler(int SIG) {
	if(SIG == SIGINT) {
		//do nothing
	} else if(SIG == SIGCHLD) {
		wait(NULL);
	}
}

int main(int argc, char *argv[]) {
  	// TODO: This is the entry point for your shell.
  	signal(SIGINT, signal_handler);
  	signal(SIGCHLD, signal_handler);
	init(argc, argv);
	print_shell_owner("realeigenvalue");
	do {
		quit = 0;
		dir = get_current_dir_name();
		print_prompt(dir, getpid());
		command_line = read_command();
		dispatch(command_line);
		free(dir);
		free(command_line);
	} while(!quit);
	exit(EXIT_SUCCESS);
}

void init(int argc, char *argv[]) {
	opterr = 0;
	char *command_file = NULL;
	int c, f_flag = 0;
	while((c = getopt(argc, argv, "h:f:")) != -1) {
		switch(c) {
			case 'h':
			    h_flag = 1;
			    history_file = optarg;
				break;
			case 'f':
			    f_flag = 1;
			    command_file = optarg;
				break;
			default:
				print_usage();
				exit(EXIT_FAILURE);
				break;
		}
	}
	if(argc == 1) {
		history = Log_create();
		return;
	} else if(argc == 3 && h_flag && !f_flag && 
		      strcmp(history_file, argv[optind - 1]) == 0) {
		load_history(history_file);	
	} else if(argc == 3 && !h_flag && f_flag && 
			  strcmp(command_file, argv[optind - 1]) == 0) {
		history = Log_create();
		load_script(command_file);
	} else if(argc == 5 && h_flag && f_flag && 
		     (strcmp(history_file, argv[2]) == 0 || strcmp(history_file, argv[4]) == 0) && 
		     (strcmp(command_file, argv[2]) == 0 || strcmp(command_file, argv[4]) == 0)) {
		load_history(history_file);
		load_script(command_file);
	} else {
		print_usage();
		exit(EXIT_FAILURE);
	}
}

char *read_command() {
	char *line = NULL;
	size_t len = 0;
	ssize_t read;
	if(fp != NULL) {
		if((read = getline(&line, &len, fp)) == -1) {
			printf("%s\n", "exit");
			exit_builtin();
			free(line);
			return NULL;
		}

	} else {
		read = getline(&line, &len, stdin);
	}
	int length = strlen(line);
	line[length - 1] = 0;
	if(fp != NULL) {
		printf("%s\n", line);
	}
	return line;
}

void dispatch(char *line) {
	if(line == NULL) {
		return;
	}
	background_index = strstr(line, "&");
	if(background_index != NULL) {
		*background_index = ' ';
	}
	size_t tokens = 0;
	char **args = strsplit(line, " ", &tokens);
	if(args == NULL || args[0] == NULL) {
		return;
	}
	if(run_builtin((int)tokens, args, line) == -1) {
		Log_add_command(history, line);
		run_non_builtin(args, line);
	}
	free_args(args);
}

int run_builtin(int argc, char **args, char *line) {
	if(argc == 2 && strcmp(args[0], "cd") == 0) {
		Log_add_command(history, line);
		cd_builtin(args);
		return 0;
	} else if(argc == 1 && strcmp(args[0], "exit") == 0) {
		Log_add_command(history, line);
		exit_builtin();
		return 0;
	} else if(argc == 1 && strcmp(args[0], "!history") == 0) {
		print_all_history_builtin();
		return 0;
	}
	char cmd_char = 0;
	sscanf(args[0], "%c", &cmd_char);
	if(argc == 1 && cmd_char == '#') {
		execute_from_history_builtin(args);
		return 0;
	} else if(argc == 1 && cmd_char == '!') {
		prefix_from_history_builtin(args);
		return 0;
	}
	return -1;
}

int run_non_builtin(char **args, char *line) {
	int status = 0;
	pid_t childpid = fork();
	if(childpid == 0) {
		print_command_executed(getpid());
		if(execvp(args[0], args) == -1) {
			if(dir != NULL) {
				free(dir);
			}
			if(args != NULL) {
				free_args(args);
			}
			if(line != command_line) {
				free(command_line);
			}
			if(oldargs != NULL && oldargs != args) {
				free_args(oldargs);
			}
			if(line != NULL) {
				free(line);
			}
			Log_destroy(history);
			if(history_file != NULL) {
				free(history_file);
			}
			if(fp != NULL) {
				fclose(fp);
			}
        }
        exit(EXIT_FAILURE);
	} else if(childpid > 0) {
		oldargs = NULL;
		if(background_index == NULL) {
			if(waitpid(childpid, &status, WUNTRACED) == -1) {
				print_wait_failed();
			}
			if(WEXITSTATUS(status) == EXIT_FAILURE) {
				print_exec_failed(args[0]);
			}
		}
	} else {
		print_fork_failed();
	} 
	return 0;
}

void cd_builtin(char **args) {
	if(chdir(args[1]) == -1) {
		print_no_directory(args[1]);
	}
}

void exit_builtin() {
	if(h_flag == 1) {
		Log_save(history, history_file);
		free(history_file);
	}
	Log_destroy(history);
	if(fp != NULL) {
		fclose(fp);
	}
	quit = 1;
}

void print_all_history_builtin() {
	int size = (int)Log_size(history);
	int i = 0;
	for(i = 0; i < size; i++) {
		printf("%d\t%s\n", i, Log_get_command(history, i));
	}
}

void execute_from_history_builtin(char **args) {
	char c = 0;
	int index = -1;
	char last = 0;
	int parsed = sscanf(args[0], "%c%d%c", &c, &index, &last);
	int size = (int)Log_size(history);
	if(parsed == 2 && c == '#' && index >= 0 && index < size && last == 0) {
		const char *command = Log_get_command(history, index);
		char *temp = strdup(command);
		printf("%s\n", command);
		oldargs = args;
		dispatch(temp);
		free(temp);
	} else {	
		print_invalid_index();
	}
}

void prefix_from_history_builtin(char **args) {
	char c = 0;
	int str_size = strlen(args[0]) + 1;
	char buffer[str_size];
	memset(buffer, 0, str_size);
	sscanf(args[0], "%c%[^\n]s", &c, buffer);
	int size = (int)Log_size(history);
	int index = 0;
	const char *str;
	char *start = NULL;
	for(index = 0; index < size; index++) {
		str = Log_get_command(history, index);
		start = NULL;
		if((start = strstr(str, buffer)) != NULL) {
			break;
		}
	}
	if(start != NULL) {
		printf("%s\n", str);
		char *temp = strdup(str);
		oldargs = args;
		dispatch(temp);
		free(temp);
	} else {
		print_no_history_match();
	}
}

void load_history(char *h_file) {
	history_file = get_full_path(h_file);
	history = Log_create_from_file(h_file);
}

void load_script(char *c_file) {
	char *file = get_full_path(c_file);
	fp = fopen(file, "r");
	free(file);
	if(fp == NULL) {
    	Log_destroy(history);
		print_script_file_error();
		exit(EXIT_FAILURE);
    }
}