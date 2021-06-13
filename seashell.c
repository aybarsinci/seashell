// Authors: Tunaberk Almaci, Aybars Inci

#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h> //termios, TCSANOW, ECHO, ICANON
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
const char *sysname = "seashell";

#define PRINT_RED(string) printf("%s %s  %s", "\x1B[31m", string, "\x1b[0m")
#define PRINT_GREEN(string) printf("%s %s  %s", "\x1B[32m", string, "\x1b[0m")
#define PRINT_BLUE(string) printf("%s %s  %s", "\x1B[34m", string, "\x1b[0m")

enum return_codes
{
	SUCCESS = 0,
	EXIT = 1,
	UNKNOWN = 2,
};
struct command_t
{
	char *name;
	bool background;
	bool auto_complete;
	int arg_count;
	char **args;
	char *redirects[3];		// in/out redirection
	struct command_t *next; // for piping
};
/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command)
{
	int i = 0;
	printf("Command: <%s>\n", command->name);
	printf("\tIs Background: %s\n", command->background ? "yes" : "no");
	printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
	printf("\tRedirects:\n");
	for (i = 0; i < 3; i++)
		printf("\t\t%d: %s\n", i, command->redirects[i] ? command->redirects[i] : "N/A");
	printf("\tArguments (%d):\n", command->arg_count);
	for (i = 0; i < command->arg_count; ++i)
		printf("\t\tArg %d: %s\n", i, command->args[i]);
	if (command->next)
	{
		printf("\tPiped to:\n");
		print_command(command->next);
	}
}
/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command)
{
	if (command->arg_count)
	{
		for (int i = 0; i < command->arg_count; ++i)
			free(command->args[i]);
		free(command->args);
	}
	for (int i = 0; i < 3; ++i)
		if (command->redirects[i])
			free(command->redirects[i]);
	if (command->next)
	{
		free_command(command->next);
		command->next = NULL;
	}
	free(command->name);
	free(command);
	return 0;
}
/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt()
{
	char cwd[1024], hostname[1024];
	gethostname(hostname, sizeof(hostname));
	getcwd(cwd, sizeof(cwd));
	printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
	return 0;
}
/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command)
{
	const char *splitters = " \t"; // split at whitespace
	int index, len;
	len = strlen(buf);
	while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
	{
		buf++;
		len--;
	}
	while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
		buf[--len] = 0; // trim right whitespace

	if (len > 0 && buf[len - 1] == '?') // auto-complete
		command->auto_complete = true;
	if (len > 0 && buf[len - 1] == '&') // background
		command->background = true;

	char *pch = strtok(buf, splitters);
	command->name = (char *)malloc(strlen(pch) + 1);
	if (pch == NULL)
		command->name[0] = 0;
	else
		strcpy(command->name, pch);

	command->args = (char **)malloc(sizeof(char *));

	int redirect_index;
	int arg_index = 0;
	char temp_buf[1024], *arg;
	while (1)
	{
		// tokenize input on splitters
		pch = strtok(NULL, splitters);
		if (!pch)
			break;
		arg = temp_buf;
		strcpy(arg, pch);
		len = strlen(arg);

		if (len == 0)
			continue;										 // empty arg, go for next
		while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
		{
			arg++;
			len--;
		}
		while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
			arg[--len] = 0; // trim right whitespace
		if (len == 0)
			continue; // empty arg, go for next

		// piping to another command
		if (strcmp(arg, "|") == 0)
		{
			struct command_t *c = malloc(sizeof(struct command_t));
			int l = strlen(pch);
			pch[l] = splitters[0]; // restore strtok termination
			index = 1;
			while (pch[index] == ' ' || pch[index] == '\t')
				index++; // skip whitespaces

			parse_command(pch + index, c);
			pch[l] = 0; // put back strtok termination
			command->next = c;
			continue;
		}

		// background process
		if (strcmp(arg, "&") == 0)
			continue; // handled before

		// handle input redirection
		redirect_index = -1;
		if (arg[0] == '<')
			redirect_index = 0;
		if (arg[0] == '>')
		{
			if (len > 1 && arg[1] == '>')
			{
				redirect_index = 2;
				arg++;
				len--;
			}
			else
				redirect_index = 1;
		}
		if (redirect_index != -1)
		{
			command->redirects[redirect_index] = malloc(len);
			strcpy(command->redirects[redirect_index], arg + 1);
			continue;
		}

		// normal arguments
		if (len > 2 && ((arg[0] == '"' && arg[len - 1] == '"') || (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
		{
			arg[--len] = 0;
			arg++;
		}
		command->args = (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
		command->args[arg_index] = (char *)malloc(len + 1);
		strcpy(command->args[arg_index++], arg);
	}
	command->arg_count = arg_index;
	return 0;
}
void prompt_backspace()
{
	putchar(8);	  // go back 1
	putchar(' '); // write empty over
	putchar(8);	  // go back 1 again
}
/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command)
{
	int index = 0;
	char c;
	char buf[4096];
	static char oldbuf[4096];

	// tcgetattr gets the parameters of the current terminal
	// STDIN_FILENO will tell tcgetattr that it should write the settings
	// of stdin to oldt
	static struct termios backup_termios, new_termios;
	tcgetattr(STDIN_FILENO, &backup_termios);
	new_termios = backup_termios;
	// ICANON normally takes care that one line at a time will be processed
	// that means it will return if it sees a "\n" or an EOF or an EOL
	new_termios.c_lflag &= ~(ICANON | ECHO); // Also disable automatic echo. We manually echo each char.
	// Those new settings will be set to STDIN
	// TCSANOW tells tcsetattr to change attributes immediately.
	tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

	//FIXME: backspace is applied before printing chars
	show_prompt();
	int multicode_state = 0;
	buf[0] = 0;
	while (1)
	{
		c = getchar();
		// printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

		if (c == 9) // handle tab
		{
			buf[index++] = '?'; // autocomplete
			break;
		}

		if (c == 127) // handle backspace
		{
			if (index > 0)
			{
				prompt_backspace();
				index--;
			}
			continue;
		}
		if (c == 27 && multicode_state == 0) // handle multi-code keys
		{
			multicode_state = 1;
			continue;
		}
		if (c == 91 && multicode_state == 1)
		{
			multicode_state = 2;
			continue;
		}
		if (c == 65 && multicode_state == 2) // up arrow
		{
			int i;
			while (index > 0)
			{
				prompt_backspace();
				index--;
			}
			for (i = 0; oldbuf[i]; ++i)
			{
				putchar(oldbuf[i]);
				buf[i] = oldbuf[i];
			}
			index = i;
			continue;
		}
		else
			multicode_state = 0;

		putchar(c); // echo the character
		buf[index++] = c;
		if (index >= sizeof(buf) - 1)
			break;
		if (c == '\n') // enter key
			break;
		if (c == 4) // Ctrl+D
			return EXIT;
	}
	if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
		index--;
	buf[index++] = 0; // null terminate string

	strcpy(oldbuf, buf);

	parse_command(buf, command);

	// print_command(command); // DEBUG: uncomment for debugging

	// restore the old settings
	tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
	return SUCCESS;
}
int process_command(struct command_t *command);
int main()
{
	while (1)
	{
		struct command_t *command = malloc(sizeof(struct command_t));
		memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

		int code;
		code = prompt(command);
		if (code == EXIT)
			break;

		code = process_command(command);
		if (code == EXIT)
			break;

		free_command(command);
	}

	printf("\n");
	return 0;
}

// Auxaliary Method Declarations
// ------------------------------
void vis_table(char arr[3][3]);
void user_move(int input, char arr[3][3]);
bool valid_input(int input, char arr[3][3]);
bool ai_move(char arr[3][3]);
bool win_condition(char arr[3][3]);
bool check_draw(char arr[3][3]);
void path_finder(const char[], char *, size_t);
// ------------------------------s

int process_command(struct command_t *command)
{
	int r;
	if (strcmp(command->name, "") == 0)
		return SUCCESS;

	if (strcmp(command->name, "exit") == 0)
		return EXIT;

	if (strcmp(command->name, "cd") == 0)
	{
		if (command->arg_count > 0)
		{
			r = chdir(command->args[0]);
			if (r == -1)
				printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
			return SUCCESS;
		}
	}

		/*
		Part 2
		
		*/

	if (strcmp(command->name, "shortdir") == 0)
	{

		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;
		char *option = command->args[1];

		if (strcmp(option, "set") == 0)
		{
			char *name = command->args[2];
			char *path = "/tmp/shortdirs.txt";
			FILE *fptr = fopen(path, "a+");
			char temp_name[1024];
			strcpy(temp_name, name);
			char cwd[1024];
			getcwd(cwd, sizeof(cwd));
			char current_path[1024];
			strcpy(current_path, cwd);
			strcat(temp_name, " -> ");
			strcat(temp_name, current_path);
			char holder[4096];
			char *current_line;
			int line, count;
			line = -1;
			count = 1;
			while (fgets(holder, sizeof(holder), fptr) != NULL)
			{
				current_line = strtok(holder, " -> ");
				if (strcmp(current_line, name) == 0)
				{
					line = count;
					break;
				}
				else
				{
					count++;
				}
			}
			fclose(fptr);
			fptr = fopen(path, "a+");
			if (line == -1)
			{
				fprintf(fptr, "%s\n", temp_name);
				fclose(fptr);
			}
			else
			{
				char *temp_path = "/tmp/temp_shortdirs.txt";
				FILE *temp_f_ptr = fopen(temp_path, "a+");
				char holder[4096];
				char *current_line;
				int count = 0;
				while (fgets(holder, sizeof(holder), fptr) != NULL)
				{
					count++;
					if (count != line)
					{
						fprintf(temp_f_ptr, "%s", holder);
					}
					else
					{
						fprintf(temp_f_ptr, "%s", temp_name);
					}
				}
				fclose(fptr);
				fclose(temp_f_ptr);
				remove(path);
				rename(temp_path, path);
			}
		}
		else if (strcmp(option, "jump") == 0)
		{
			char *name = command->args[2];
			char *path = "/tmp/shortdirs.txt";
			FILE *fptr = fopen(path, "r");
			char holder[4096];
			char *current_line;
			int flag = 0;
			char to_path[PATH_MAX];
			while (fgets(holder, sizeof(holder), fptr) != NULL)
			{
				current_line = strtok(holder, " -> ");
				if (strcmp(current_line, name) == 0)
				{
					flag = 1;
					current_line = strtok(NULL, " -> ");
					char *new_line_truncated;
					new_line_truncated = strtok(current_line, "\n");
					strcpy(to_path, new_line_truncated);
				}
			}
			if (flag == 1)
			{
				fclose(fptr);
				char *abs_path = to_path;
				r = chdir(abs_path);
			}
			else if (flag == 0)
			{
				printf("The short directory name is not associated to any directory path.");
			}
		}
		else if (strcmp(option, "del") == 0)
		{
			char *name = command->args[2];
			char *path = "/tmp/shortdirs.txt";
			FILE *fptr = fopen(path, "r");
			char holder[4096];
			char *current_line;
			int line, count;
			count = 1;
			int flag = 0;
			while (fgets(holder, sizeof(holder), fptr) != NULL)
			{
				current_line = strtok(holder, " -> ");
				if (strcmp(current_line, name) == 0)
				{
					flag = 1;
					line = count;
					break;
				}
				else
				{
					count++;
				}
			}
			if (flag == 0)
			{
				printf("No such short directory name is found");
			}
			else if (flag == 1)
			{
				fclose(fptr);
				fptr = fopen(path, "r");
				char *temp_path = "/tmp/temp_shortdirs.txt";
				FILE *temp_f_ptr = fopen(temp_path, "w+");
				char holder[4096];
				char *current_line;
				int count = 0;
				while (fgets(holder, sizeof(holder), fptr) != NULL)
				{
					count++;
					if (count != line)
					{
						fprintf(temp_f_ptr, "%s", holder);
					}
				}
				fclose(fptr);
				fclose(temp_f_ptr);
				remove(path);
				rename(temp_path, path);
			}
		}
		else if (strcmp(option, "clear") == 0)
		{
			char *path = "/tmp/shortdirs.txt";
			fclose(fopen(path, "w"));
		}
		else if (strcmp(option, "list") == 0)
		{
			char *path = "/tmp/shortdirs.txt";
			FILE *fptr = fopen(path, "r");
			char holder[4096];
			while (fgets(holder, sizeof(holder), fptr) != NULL)
			{
				printf("%s", holder);
			}
			fclose(fptr);
		}
		return SUCCESS;
	}

	// Part 3
	if (strcmp(command->name, "highlight") == 0)
	{

		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		char delims[] = {" ,.:;\t\r\n\v\f\0"};
		char *word = command->args[1];
		char *color = command->args[2];
		char *file_path = command->args[3];
		FILE *fptr = fopen(file_path, "r");
		if (fptr == NULL) 
		{printf("The short directory name is not associated to any directory path.");
			printf("No such file exists.");
			return EXIT;
		}
		char holder[1024];
		char *current_word;
		while (fgets(holder, sizeof(holder), fptr) != NULL)
		{
			char containing_line[1024];
			strcpy(containing_line, holder);
			int word_exists = 0;
			current_word = strtok(holder, delims);
			while (current_word != NULL)
			{
				if (strcasecmp(current_word, word) == 0)
				{
					printf("%s\n", current_word);
					word_exists = 1;
					break;
				}
				current_word = strtok(NULL, delims);
			}
			if (word_exists == 1)
			{
				char *current_word_2 = strtok(containing_line, delims);
				while (current_word_2 != NULL)
				{
					if (strcasecmp(current_word_2, word) == 0)
					{
						if (strcasecmp(color, "r") == 0)
						{
							PRINT_RED(current_word_2);
						}
						if (strcasecmp(color, "g") == 0)
						{
							PRINT_GREEN(current_word_2);
						}
						if (strcasecmp(color, "b") == 0)
						{
							PRINT_BLUE(current_word_2);
						}
					}
					else
					{
						printf("%s ", current_word_2);
					}
					current_word_2 = strtok(NULL, delims);
				}
				printf("\n");
			}
		}
		fclose(fptr);
		return SUCCESS;
	}

	// Part 4
	if (strcmp(command->name, "goodMorning") == 0)
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		char *crontabFile = "/tmp/sch_jobs.txt";
		char *time = command->args[1];
		char *mFile = command->args[2];
		char *hour;
		hour = strtok(time, ".");
		char *min;
		min = strtok(NULL, ".");
		char *crontab_path[512];
		char *rythmbox_path[512];
		path_finder("crontab", crontab_path, sizeof(crontab_path));
		path_finder("rhythmbox-client", rythmbox_path, sizeof(rythmbox_path));
		FILE *fptr = fopen(crontabFile, "a+");
		fprintf(fptr, "%s %s * * * XDG_RUNTIME_DIR=/run/user/$(id -u) %s --play-uri=%s\n", min, hour, rythmbox_path, mFile);
		fclose(fptr);
		remove(fptr);
		char *args[3];
		args[0] = "crontab";
		args[1] = crontabFile;
		args[2] = NULL;
		pid_t pid = fork();
		if (pid == 0)
		{
			execv(crontab_path, args);
		}
		else
		{
			if (!command->background)
				wait(0); // wait for child process to finish
			return SUCCESS;
		}
	}

	// Part 5
	if (strcmp(command->name, "kdiff") == 0)
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		char *option = command->args[1];
		char *first_file_path = command->args[2];
		char *second_file_path = command->args[3];
		FILE *fptr1 = fopen(first_file_path, "r");
		FILE *fptr2 = fopen(second_file_path, "r");

		if (fptr1 == NULL && fptr2 == NULL) 
		{
			printf("None of the files exists. \n");
			return UNKNOWN;
		}

		if (fptr1 == NULL) 
		{
			printf("The first file does not exist. \n");
			return UNKNOWN;
		}

		if (fptr2 == NULL) {
			printf("The second file does not exist. \n");
			return UNKNOWN;
		}

		int len1 = strlen(first_file_path);
		char *last_four1 = &first_file_path[len1-4];

		int len2 = strlen(second_file_path);
		char *last_four2 = &second_file_path[len2-4];

		if ( (strcmp(last_four1, ".txt") != 0) || (strcmp(last_four2, ".txt") != 0)) 
		{
			printf("Both of the files must be txt files. \n");
			return EXIT;
		}

		if (strcmp(option, "-a") == 0)
		{
			char ch1 = fgetc(fptr1);
			char ch2 = fgetc(fptr2);
			char holder1[1024];
			char holder2[1024];
			char *current_line_1;
			char *current_line_2;
			int count = 0;
			int line = 1;
			while (((current_line_1 = fgets(holder1, sizeof(holder1), fptr1)) != NULL) && ((current_line_2 = fgets(holder2, sizeof(holder2), fptr2)) != NULL))
			{
				if (strcmp(current_line_1, current_line_2) != 0)
				{
					printf("%s: Line %d: %s \n", first_file_path, line, current_line_1);
					printf("%s: Line %d: %s \n", second_file_path, line, current_line_2);
					count++;
				}
				line++;
				ch1 = fgetc(fptr1);
				ch2 = fgetc(fptr2);
			}

			if (ch1 == EOF && ch2 == EOF)
			{
				if (count == 0)
				{
					printf("The files are identical.\n");
				}
				else
				{
					printf("%d different line(s) found.\n", count);
				}
			}
			else
			{
				if (count == 0)
				{
					if (ch1 == EOF) 
					{
						printf("The files differ. The second file is longer than the first one. But they are identical in the common lines. \n");
					}
					if (ch2 == EOF) 
					{
						printf("The files differ. The first file is longer than the second one. But they are identical in the common lines. \n");
					}
				}
				else
				{
					printf("%d different line(s) found.\n", count);
				}
			}
		}
		else if (strcmp(option, "-b") == 0)
		{
			char *buffer1;
			char *buffer2;
			int i;
			int count = 0;
			fseek(fptr1, 0, SEEK_END);
			fseek(fptr2, 0, SEEK_END);
			long filelen1 = ftell(fptr1);
			long filelen2 = ftell(fptr2);
			rewind(fptr1);
			rewind(fptr2);
			buffer1 = (char *)malloc((filelen1 + 1) * sizeof(char));
			buffer2 = (char *)malloc((filelen2 + 1) * sizeof(char));
			long base_len;
			if (buffer1 < buffer2)
			{
				base_len = filelen1;
			}
			else
			{
				base_len = filelen2;
			}
			for (i = 0; i < base_len - 1; i++)
			{
				fread(buffer1, 1, 1, fptr1);
				fread(buffer2, 1, 1, fptr2);
				if (memcmp(buffer1, buffer2, sizeof(char)) != 0)
				{
					count++;
				}
			}
			if (filelen1 == filelen2)
			{
				if (count == 0)
				{
					printf("The files are identical.\n");
				}
				else
				{
					printf("The files differ in %d bytes.\n", count);
				}
			}
			else
			{
				if (filelen1 > filelen2)
				{
					printf("THe first file is longer than the second file.\n");
					count = count + (filelen1 - base_len);
					printf("The files differ in %d bytes.\n", count);
				}
				else
				{
					printf("The second file is longer than the first file.\n");
					count = count + (filelen2 - base_len);				
					printf("The files differ in %d bytes.\n", count);
				}
			}
		}
		return SUCCESS;
	}

	// Part 6
	if (strcmp(command->name, "iambored") == 0)
	{
		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		printf("-----------------------------------------------------\n");
		printf("||              ||		 \n");
		printf("||              ||		 \n");
		printf("||              ||        || \n");
		printf("||              ||        || \n");
		printf("||              ||        || \n");
		printf("||     ||||     ||  ___   ||  ___   ___   _ _   ___\n");
		printf("||     ||||     || |___|  || |     |   | | | | |___|\n");
		printf("||_____||||_____|| |____  || |___  |___| |   | |____  \n");
		printf("-----------------------------------------------------\n");

		PRINT_RED("Which option suits you best?");
		printf("\n");
		printf("---------------------------------------\n");
		printf("Option 1: Magic - 8 Ball\n");
		printf("Option 2: Tic Tac Toe\n");
		printf("Option 3: Guess my height\n");
		printf("Option 4: Exit\n");

		int option;
		char buffer[100];
		fgets(buffer, 99, stdin);
		sscanf(buffer, "%d", &option);

		while (option != 4)
		{

			if (option == 1)
			{
				printf("Ask the Oracle anything you want to learn.\n");
				char *question;
				fgets(buffer, 99, stdin);
				sscanf(buffer, "%s", question);

				srand(time(NULL));
				int r = (rand() % 20);
				switch (r)
				{
				case 1:
					PRINT_GREEN("It is certain.\n");
					break;
				case 2:
					PRINT_GREEN("It is decidedly so.\n");
					break;
				case 3:
					PRINT_GREEN("Without a doubt.\n");
					break;
				case 4:
					PRINT_GREEN("Yes- definitely.\n");
					break;
				case 5:
					PRINT_GREEN("You may rely on it.\n");
					break;
				case 6:
					PRINT_GREEN("As I see it, yes.\n");
					break;
				case 7:
					PRINT_GREEN("Most likely.\n");
					break;
				case 8:
					PRINT_GREEN("Outlook good.\n");
					break;
				case 9:
					PRINT_GREEN("Yes.\n");
					break;
				case 10:
					PRINT_GREEN("Signs points to yes.\n");
					break;
				case 11:
					PRINT_GREEN("Reply hazy, try again.\n");
					break;
				case 12:
					PRINT_GREEN("Ask again later.\n");
					break;
				case 13:
					PRINT_GREEN("Better not tell you now.\n");
					break;
				case 14:
					PRINT_GREEN("Cannot predict now.\n");
					break;
				case 15:
					PRINT_GREEN("Concentrate and ask again.\n");
					break;
				case 16:
					PRINT_GREEN("Don't count on it.\n");
					break;
				case 17:
					PRINT_GREEN("My reply is no.\n");
					break;
				case 18:
					PRINT_GREEN("My sources say no.\n");
					break;
				case 19:
					PRINT_GREEN("Outlook not so good.\n");
					break;
				case 0:
					PRINT_GREEN("Very doubtful.\n");
					break;
				}
				sleep(2);
			}
			else if (option == 2)
			{
				char b = ' ';
				char x = 'X';
				char o = 'O';
				bool game_state = true;
				char table[3][3] = {
					{b, b, b},
					{b, b, b},
					{b, b, b}};
				printf("You will be playing against Tic Tac Toe bot.\n");
				printf("Your symbol is X and Tic Tac Toe bot's symbol is O.\n");
				printf("You can play your turns by entering cordination of the Tic Tac Toe table.\n");
				printf("THe first move is yours.\n");

				while (game_state)
				{
					bool valid = true;
					int user_turn;
					while (valid)
					{
						vis_table(table);
						printf("\nYour turn:");

						fgets(buffer, 99, stdin);
						sscanf(buffer, "%d", &user_turn);

						valid = valid_input(user_turn, table);
					}
					user_move(user_turn, table);

					if (win_condition(table) || check_draw(table))
					{
						sleep(2);
						break;
					}

					vis_table(table);

					printf("\n");
					printf("Tic Tao Toe bot's turn:\n");
					bool ai_ct = ai_move(table);
					if (ai_ct)
					{
						bool z = true;
						while (z)
						{
							srand(time(NULL));
							int n1 = (rand() % 3);
							srand(time(NULL));
							int n2 = (rand() % 3);
							if (table[n1][n2] == b)
							{
								table[n1][n2] = o;
								z = false;
							}
						}
					}

					if (win_condition(table) || check_draw(table))
					{
						sleep(2);
						break;
					}
				}
				vis_table(table);
			}
			else if (option == 3)
			{
				printf("Andy: Let's see if you can guess my height.\n");
				printf("Andy: My height is between 50 and 200 cm.\n");
				bool t = true;
				srand(time(NULL));
				int height = (rand() % (200 - 50 + 1)) + 50;
				while (t)
				{
					int guess;
					fgets(buffer, 99, stdin);
					sscanf(buffer, "%d\n", &guess);
					if (guess < height)
					{
						PRINT_RED("Andy: Go higher.\n\n");
					}
					else if (guess > height)
					{
						PRINT_BLUE("Andy: Go lower.\n\n");
					}
					else
					{
						t = false;
						PRINT_GREEN("Andy: Yessss! You guessed my exact height.\n\n");
					}
				}
			}
			else
			{
			}
			PRINT_RED("Which option suits you best?\n");
			printf("\n");
			printf("---------------------------------------\n");
			printf("Option 1: Magic - 8 Ball\n");
			printf("Option 2: Tic Tac Toe\n");
			printf("Option 3: Guess my height\n");
			printf("Option 4: Exit\n");
			fgets(buffer, 99, stdin);
			sscanf(buffer, "%d", &option);
		}
	}

	pid_t pid = fork();
	if (pid == 0) // child
	{
		/// This shows how to do exec with environ (but is not available on MacOs)
		// extern char** environ; // environment variables
		// execvpe(command->name, command->args, environ); // exec+args+path+environ

		/// This shows how to do exec with auto-path resolve
		// add a NULL argument to the end of args, and the name to the beginning
		// as required by exec

		// increase args size by 2
		command->args = (char **)realloc(
			command->args, sizeof(char *) * (command->arg_count += 2));

		// shift everything forward by 1
		for (int i = command->arg_count - 2; i > 0; --i)
			command->args[i] = command->args[i - 1];

		// set args[0] as a copy of name
		command->args[0] = strdup(command->name);
		// set args[arg_count-1] (last) to NULL
		command->args[command->arg_count - 1] = NULL;

		char command_path[PATH_MAX];
		path_finder(command->name, command_path, sizeof(command_path));
		execv(command_path, command->args);
		exit(0);
	}
	else
	{
		if (!command->background)
			wait(0); // wait for child process to finish
		return SUCCESS;
	}

	// TODO: your implementation here

	printf("-%s: %s: command not found\n", sysname, command->name);
	return UNKNOWN;
}

// Part 1
void path_finder(const char name[], char *path, size_t size)
{
	char *allPaths = strdup(getenv("PATH"));
	char *forFree = allPaths;
	const char *item;

	while ((item = strsep(&allPaths, ":")) != NULL)
	{
		snprintf(path, size, "%s/%s", item, name);
		if (access(path, X_OK | F_OK) != -1)
		{
			break;
		}
	}
	free(forFree);
}

// Auxaliary Methods

void vis_table(char arr[3][3])
{
	int a = 254;
	char c = a;

	printf("-------\n");
	printf("|%c|%c|%c|\n", arr[0][0], arr[0][1], arr[0][2]);
	printf("-------\n");
	printf("|%c|%c|%c|\n", arr[1][0], arr[1][1], arr[1][2]);
	printf("-------\n");
	printf("|%c|%c|%c|\n", arr[2][0], arr[2][1], arr[2][2]);
	printf("-------\n");
}
void user_move(int input, char arr[3][3])
{

	if (input == 11)
	{
		arr[0][0] = 'X';
	}
	else if (input == 12)
	{
		arr[0][1] = 'X';
	}
	else if (input == 13)
	{
		arr[0][2] = 'X';
	}
	else if (input == 21)
	{
		arr[1][0] = 'X';
	}
	else if (input == 22)
	{
		arr[1][1] = 'X';
	}
	else if (input == 23)
	{
		arr[1][2] = 'X';
	}
	else if (input == 31)
	{
		arr[2][0] = 'X';
	}
	else if (input == 32)
	{
		arr[2][1] = 'X';
	}
	else if (input == 33)
	{
		arr[2][2] = 'X';
	}
}
bool valid_input(int input, char arr[3][3])
{
	bool valid;

	if (input == 11)
	{
		if (arr[0][0] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 12)
	{
		if (arr[0][1] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 13)
	{
		if (arr[0][2] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 21)
	{
		if (arr[1][0] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 22)
	{
		if (arr[1][1] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 23)
	{
		if (arr[1][2] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 31)
	{
		if (arr[2][0] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 32)
	{
		if (arr[2][1] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	else if (input == 33)
	{
		if (arr[2][2] == ' ')
		{
			valid = false;
		}
		else
		{
			printf("You have entered invalid cordinates\n");
			valid = true;
		}
	}
	return valid;
}
bool ai_move(char arr[3][3])
{
	char c = 'X';
	char b = ' ';
	char o = 'O';
	bool f = false;
	if (arr[0][0] == c && arr[0][1] == c && arr[0][2] == b)
	{ //ok
		arr[0][2] = o;
	}
	else if (arr[0][1] == c && arr[0][2] == c && arr[0][0] == b)
	{ //ok
		arr[0][0] = o;
	}
	else if (arr[1][0] == c && arr[1][1] == c && arr[1][2] == b)
	{ //ok
		arr[1][2] = o;
	}
	else if (arr[1][1] == c && arr[1][2] == c && arr[1][0] == b)
	{ //ok
		arr[1][0] = o;
	}
	else if (arr[2][0] == c && arr[2][1] == c && arr[2][2] == b)
	{ //ok
		arr[2][2] = o;
	}
	else if (arr[2][1] == c && arr[2][2] == c && arr[2][0] == b)
	{ // ok
		arr[2][0] = o;
	}
	else if (arr[0][0] == c && arr[1][0] == c && arr[2][0] == b)
	{ //ok
		arr[2][0] = o;
	}
	else if (arr[1][0] == c && arr[2][0] == c && arr[0][0] == b)
	{ // ok
		arr[0][0] = o;
	}
	else if (arr[0][1] == c && arr[1][1] == c && arr[2][1] == b)
	{ //ok
		arr[2][1] = o;
	}
	else if (arr[1][1] == c && arr[2][1] == c && arr[0][1] == b)
	{ //ok
		arr[0][1] = o;
	}
	else if (arr[0][2] == c && arr[1][2] == c && arr[2][2] == b)
	{ //ok
		arr[2][2] = o;
	}
	else if (arr[1][2] == c && arr[2][2] == c && arr[0][2] == b)
	{ //ok
		arr[0][2] = o;
	}
	else if (arr[0][0] == c && arr[0][2] == c && arr[0][1] == b)
	{ //ok
		arr[0][1] = o;
	}
	else if (arr[1][0] == c && arr[1][2] == c && arr[1][1] == b)
	{ //ok
		arr[1][1] = o;
	}
	else if (arr[2][0] == c && arr[2][2] == c && arr[2][1] == b)
	{ //ok
		arr[2][1] = o;
	}
	else if (arr[0][0] == c && arr[2][0] == c && arr[1][0] == b)
	{ //ok
		arr[1][0] = o;
	}
	else if (arr[0][1] == c && arr[2][1] == c && arr[1][1] == b)
	{ //ok
		arr[1][1] = o;
	}
	else if (arr[0][2] == c && arr[2][2] == c && arr[1][2] == b)
	{ //ok
		arr[1][2] = o;
	}
	else if (arr[0][0] == c && arr[1][1] == c && arr[2][2] == b)
	{ //ok
		arr[2][2] = o;
	}
	else if (arr[1][1] == c && arr[2][2] == c && arr[0][0] == b)
	{ //ok
		arr[0][0] = o;
	}
	else if (arr[0][0] == c && arr[2][2] == c && arr[1][1] == b)
	{ //ok
		arr[1][1] = o;
	}
	else if (arr[2][0] == c && arr[1][1] == c && arr[0][2] == b)
	{ //ok
		arr[0][2] = o;
	}
	else if (arr[1][1] == c && arr[0][2] == c && arr[2][0] == b)
	{ //ok
		arr[2][0] = o;
	}
	else if (arr[2][0] == c && arr[0][2] == c && arr[1][1] == b)
	{ //ok
		arr[1][1] = o;
	}
	else
	{
		f = true;
	}
	return f;
}
bool win_condition(char arr[3][3])
{
	bool f = false;
	char c = 'X';
	char b = ' ';
	char o = 'O';
	if (arr[0][1] == c && arr[0][2] == c && arr[0][0] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[1][1] == c && arr[1][2] == c && arr[1][0] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[2][1] == c && arr[2][2] == c && arr[2][0] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[0][0] == c && arr[1][0] == c && arr[2][0] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[0][1] == c && arr[1][1] == c && arr[2][1] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[0][2] == c && arr[1][2] == c && arr[2][2] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[0][0] == c && arr[1][1] == c && arr[2][2] == c)
	{
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[2][0] == c && arr[1][1] == c && arr[0][2] == c)
	{ //ok
		f = true;
		printf("Congratulaitons you have won.\n");
	}
	else if (arr[0][1] == o && arr[0][2] == o && arr[0][0] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[1][1] == o && arr[1][2] == o && arr[1][0] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[2][1] == o && arr[2][2] == o && arr[2][0] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[0][0] == o && arr[1][0] == o && arr[2][0] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[0][1] == o && arr[1][1] == o && arr[2][1] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[0][2] == o && arr[1][2] == o && arr[2][2] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[0][0] == o && arr[1][1] == o && arr[2][2] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	else if (arr[2][0] == o && arr[1][1] == o && arr[0][2] == o)
	{
		f = true;
		printf("You have lost. Try again.\n");
	}
	return f;
}
bool check_draw(char arr[3][3])
{
	bool f = false;
	char b = ' ';
	if ((arr[0][0] != b) && (arr[0][1] != b) && (arr[0][2] != b) &&
		(arr[1][0] != b) && (arr[1][1] != b) && (arr[1][2] != b) &&
		(arr[2][0] != b) && (arr[2][1] != b) && (arr[2][2] != b))
	{
		printf("STALEMATE\n");
		f = true;
	}
	return f;
}