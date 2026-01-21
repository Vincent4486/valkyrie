// SPDX-License-Identifier: BSD-3-Clause

#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

char *version = "1.0";

typedef struct
{
   char *version;
   char *cwd;
   char starter;
} Shell;

typedef struct
{
   char *executable;
   int argc;
   char **argv;
   char *input_file;  // stdin redirection (<)
   char *output_file; // stdout redirection (>, >>)
   int append_output; // 1 for >>, 0 for >
} Command;

typedef struct
{
   Command **commands;
   int count;
} Pipeline;

typedef struct
{
   char *name;
   char *value;
} Variable;

#define MAX_VARS 100
Variable vars[MAX_VARS];
int var_count = 0;
char remaining_command[PATH_MAX] = {0};

Shell g_Shell;

void init();
void loop();
void execute(Command *command);
void execute_pipeline(Pipeline *pipeline);
Command *parse(char command[PATH_MAX]);
Pipeline *parse_pipeline(char command[PATH_MAX]);
void set(const char *var, const char *value);

void Command_CD(Command *command);
void Command_HELP(Command *command);
void Command_EXIT(Command *command);
int open_input_file(const char *filename);
int open_output_file(const char *filename, int append);

void sigint_handler(int sig)
{
   // Ignore SIGINT in the shell
   // Let child processes handle it
}

void set(const char *var, const char *value)
{
   // Check if variable already exists
   for (int i = 0; i < var_count; i++)
   {
      if (strcmp(vars[i].name, var) == 0)
      {
         free(vars[i].value);
         vars[i].value = malloc(strlen(value) + 1);
         strcpy(vars[i].value, value);
         printf("Set %s=%s\n", var, value);
         return;
      }
   }

   // Add new variable
   if (var_count < MAX_VARS)
   {
      vars[var_count].name = malloc(strlen(var) + 1);
      strcpy(vars[var_count].name, var);
      vars[var_count].value = malloc(strlen(value) + 1);
      strcpy(vars[var_count].value, value);
      var_count++;
      printf("Set %s=%s\n", var, value);
   }
   else
   {
      printf("Error: too many variables\n");
   }
}

const char *get_var(const char *name)
{
   for (int i = 0; i < var_count; i++)
   {
      if (strcmp(vars[i].name, name) == 0)
      {
         return vars[i].value;
      }
   }
   return NULL;
}

char *find_executable_in_path(const char *executable)
{
   const char *path_str = get_var("PATH");
   if (path_str == NULL) return NULL;

   // Make a copy of PATH since strtok modifies it
   char *path_copy = malloc(strlen(path_str) + 1);
   strcpy(path_copy, path_str);

   char *dir = strtok(path_copy, ":");
   while (dir != NULL)
   {
      // Build full path
      char full_path[PATH_MAX];
      snprintf(full_path, PATH_MAX, "%s/%s", dir, executable);

      // Check if file exists and is executable
      if (access(full_path, X_OK) == 0)
      {
         char *result = malloc(strlen(full_path) + 1);
         strcpy(result, full_path);
         free(path_copy);
         return result;
      }

      dir = strtok(NULL, ":");
   }

   free(path_copy);
   return NULL;
}

void execute_with_redirect(const char *full_path, Command *command,
                           int stdin_fd, int stdout_fd)
{
   pid_t pid = fork();

   if (pid == 0)
   {
      // Child process
      // Redirect stdin if needed
      if (stdin_fd != STDIN_FILENO)
      {
         dup2(stdin_fd, STDIN_FILENO);
         close(stdin_fd);
      }
      // Redirect stdout if needed
      if (stdout_fd != STDOUT_FILENO)
      {
         dup2(stdout_fd, STDOUT_FILENO);
         close(stdout_fd);
      }

      // Build argv array
      char **argv = malloc(sizeof(char *) * (command->argc + 2));
      argv[0] = command->executable;
      for (int i = 0; i < command->argc; i++)
      {
         argv[i + 1] = command->argv[i];
      }
      argv[command->argc + 1] = NULL;

      execv(full_path, argv);
      perror("execv");
      exit(1);
   }
   else if (pid > 0)
   {
      // Parent process
      if (stdin_fd != STDIN_FILENO) close(stdin_fd);
      if (stdout_fd != STDOUT_FILENO) close(stdout_fd);

      int status;
      waitpid(pid, &status, 0);
   }
   else
   {
      perror("fork");
   }
}

int open_input_file(const char *filename)
{
   int fd = open(filename, O_RDONLY);
   if (fd == -1)
   {
      perror("open");
   }
   return fd;
}

int open_output_file(const char *filename, int append)
{
   int flags = O_WRONLY | O_CREAT;
   if (append)
   {
      flags |= O_APPEND;
   }
   else
   {
      flags |= O_TRUNC;
   }
   int fd = open(filename, flags, 0644);
   if (fd == -1)
   {
      perror("open");
   }
   return fd;
}

void execute_pipeline(Pipeline *pipeline)
{
   if (pipeline == NULL || pipeline->count == 0) return;

   int **pipes = malloc(sizeof(int *) * (pipeline->count - 1));
   for (int i = 0; i < pipeline->count - 1; i++)
   {
      pipes[i] = malloc(sizeof(int) * 2);
      if (pipe(pipes[i]) == -1)
      {
         perror("pipe");
         return;
      }
   }

   pid_t *pids = malloc(sizeof(pid_t) * pipeline->count);

   for (int i = 0; i < pipeline->count; i++)
   {
      Command *cmd = pipeline->commands[i];
      char *full_path = find_executable_in_path(cmd->executable);

      if (full_path == NULL)
      {
         printf("Unknown command: %s\n", cmd->executable);
         continue;
      }

      pids[i] = fork();

      if (pids[i] == 0)
      {
         // Child process
         // Set up stdin
         if (i > 0)
         {
            dup2(pipes[i - 1][0], STDIN_FILENO);
         }
         else if (cmd->input_file != NULL)
         {
            int fd = open_input_file(cmd->input_file);
            if (fd != -1)
            {
               dup2(fd, STDIN_FILENO);
               close(fd);
            }
         }

         // Set up stdout
         if (i < pipeline->count - 1)
         {
            dup2(pipes[i][1], STDOUT_FILENO);
         }
         else if (cmd->output_file != NULL)
         {
            int fd = open_output_file(cmd->output_file, cmd->append_output);
            if (fd != -1)
            {
               dup2(fd, STDOUT_FILENO);
               close(fd);
            }
         }

         // Close all pipes in child
         for (int j = 0; j < pipeline->count - 1; j++)
         {
            close(pipes[j][0]);
            close(pipes[j][1]);
         }

         // Build argv
         char **argv = malloc(sizeof(char *) * (cmd->argc + 2));
         argv[0] = cmd->executable;
         for (int j = 0; j < cmd->argc; j++)
         {
            argv[j + 1] = cmd->argv[j];
         }
         argv[cmd->argc + 1] = NULL;

         execv(full_path, argv);
         perror("execv");
         exit(1);
      }
      else if (pids[i] == -1)
      {
         perror("fork");
      }

      free(full_path);
   }

   // Parent closes all pipes
   for (int i = 0; i < pipeline->count - 1; i++)
   {
      close(pipes[i][0]);
      close(pipes[i][1]);
      free(pipes[i]);
   }
   free(pipes);

   // Wait for all children
   for (int i = 0; i < pipeline->count; i++)
   {
      if (pids[i] > 0)
      {
         int status;
         waitpid(pids[i], &status, 0);
      }
   }
   free(pids);
}

Pipeline *parse_pipeline(char command[PATH_MAX])
{
   // Count pipes
   int pipe_count = 0;
   for (int i = 0; command[i] != '\0'; i++)
   {
      if (command[i] == '|' &&
          (i == 0 || command[i - 1] != '>' || command[i + 1] != '>'))
      {
         pipe_count++;
      }
   }

   if (pipe_count == 0)
   {
      // No pipes, just parse as single command
      Pipeline *pipeline = malloc(sizeof(Pipeline));
      pipeline->commands = malloc(sizeof(Command *));
      pipeline->commands[0] = parse(command);
      pipeline->count = 1;
      return pipeline;
   }

   // Split by pipes
   Pipeline *pipeline = malloc(sizeof(Pipeline));
   pipeline->commands = malloc(sizeof(Command *) * (pipe_count + 1));
   pipeline->count = pipe_count + 1;

   char *copy = malloc(strlen(command) + 1);
   strcpy(copy, command);

   char *cmd_str = copy;
   for (int i = 0; i <= pipe_count; i++)
   {
      char *pipe_pos = strchr(cmd_str, '|');
      if (pipe_pos != NULL)
      {
         *pipe_pos = '\0';
      }

      pipeline->commands[i] = parse(cmd_str);

      if (pipe_pos != NULL)
      {
         cmd_str = pipe_pos + 1;
      }
   }

   free(copy);
   return pipeline;
}

void Command_EXIT(Command *command)
{
   if (command->argc == 0)
   {
      exit(0);
   }
   else
   {
      char *endptr;
      long exit_code = strtol(command->argv[0], &endptr, 10);

      if (*endptr != '\0')
      {
         printf("Error: invalid exit code '%s'\n", command->argv[0]);
         return;
      }

      if (command->argc > 1)
      {
         printf("Error: unexpected tokens after exit code\n");
         return;
      }

      printf("Exiting...\n");
      exit((int)exit_code);
   }
}

void Command_CD(Command *command)
{
   if (command->argc == 0)
   {
      printf("Error: cd requires a directory\n");
   }
   else
   {
      if (chdir(command->argv[0]) != 0)
      {
         perror("cd");
      }
      else
      {
         char *cwd = getcwd(NULL, 0);
         if (cwd != NULL)
         {
            free(g_Shell.cwd);
            g_Shell.cwd = cwd;
         }
      }
   }
}

void Command_HELP(Command *command)
{
   printf("Available commands:\n");
   printf("  exit [code] - Exit the shell\n");
   printf("  cd [dir] - Change directory\n");
   printf("  help - Show this help message\n");
}

void execute(Command *command)
{
   if (command == NULL) return;

   if (strcmp(command->executable, "exit") == 0)
   {
      Command_EXIT(command);
   }
   else if (strcmp(command->executable, "cd") == 0)
   {
      Command_CD(command);
   }
   else if (strcmp(command->executable, "help") == 0)
   {
      Command_HELP(command);
   }
   else if (strchr(command->executable, '=') != NULL)
   {
      const char *equal = strchr(command->executable, '=');
      size_t var_len = equal - command->executable;
      char *var = malloc(var_len + 1);
      strncpy(var, command->executable, var_len);
      var[var_len] = '\0';
      const char *value = equal + 1;
      set(var, value);
      free(var);
   }
   else
   {
      // Try to find and execute in PATH
      char *full_path = find_executable_in_path(command->executable);
      if (full_path != NULL)
      {
         // Handle input/output redirection
         int stdin_fd = STDIN_FILENO;
         int stdout_fd = STDOUT_FILENO;

         if (command->input_file != NULL)
         {
            stdin_fd = open_input_file(command->input_file);
            if (stdin_fd == -1)
            {
               free(full_path);
               goto cleanup;
            }
         }

         if (command->output_file != NULL)
         {
            stdout_fd =
                open_output_file(command->output_file, command->append_output);
            if (stdout_fd == -1)
            {
               if (stdin_fd != STDIN_FILENO) close(stdin_fd);
               free(full_path);
               goto cleanup;
            }
         }

         execute_with_redirect(full_path, command, stdin_fd, stdout_fd);
         free(full_path);
      }
      else
      {
         printf("Unknown command: %s\n", command->executable);
      }
   }

cleanup:
   free(command->executable);
   for (int i = 0; i < command->argc; i++)
   {
      free(command->argv[i]);
   }
   free(command->argv);
   if (command->input_file) free(command->input_file);
   if (command->output_file) free(command->output_file);
   free(command);
}

Command *parse(char command[PATH_MAX])
{
   // Remove trailing newline from fgets
   size_t len = strlen(command);
   if (len > 0 && command[len - 1] == '\n')
   {
      command[len - 1] = '\0';
   }

   // Check for && and split if found
   char *and_pos = strstr(command, "&&");
   if (and_pos != NULL)
   {
      // Store remaining command
      strcpy(remaining_command, and_pos + 2);
      // Null-terminate at &&
      *and_pos = '\0';
   }
   else
   {
      remaining_command[0] = '\0';
   }

   // Make a copy since strtok modifies the string
   char *copy = malloc(strlen(command) + 1);
   strcpy(copy, command);

   const char *token = strtok(copy, " ");

   if (token == NULL)
   {
      free(copy);
      return NULL;
   }

   Command *cmd = malloc(sizeof(Command));
   cmd->executable = malloc(strlen(token) + 1);
   strcpy(cmd->executable, token);
   cmd->input_file = NULL;
   cmd->output_file = NULL;
   cmd->append_output = 0;

   // Collect remaining tokens as argv with variable expansion and redirection
   char **argv = malloc(sizeof(char *) * 100);
   int argc = 0;

   token = strtok(NULL, " ");
   while (token != NULL)
   {
      // Check for redirection operators
      if (strcmp(token, ">") == 0)
      {
         token = strtok(NULL, " ");
         if (token != NULL)
         {
            cmd->output_file = malloc(strlen(token) + 1);
            strcpy(cmd->output_file, token);
            cmd->append_output = 0;
         }
      }
      else if (strcmp(token, ">>") == 0)
      {
         token = strtok(NULL, " ");
         if (token != NULL)
         {
            cmd->output_file = malloc(strlen(token) + 1);
            strcpy(cmd->output_file, token);
            cmd->append_output = 1;
         }
      }
      else if (strcmp(token, "<") == 0)
      {
         token = strtok(NULL, " ");
         if (token != NULL)
         {
            cmd->input_file = malloc(strlen(token) + 1);
            strcpy(cmd->input_file, token);
         }
      }
      else
      {
         // Expand variables within token
         char expanded[PATH_MAX] = {0};
         int exp_pos = 0;
         int i = 0;

         while (token[i] != '\0')
         {
            if (token[i] == '$')
            {
               // Extract variable name
               int var_start = i + 1;
               int var_end = var_start;
               while (token[var_end] != '\0' &&
                      (isalnum(token[var_end]) || token[var_end] == '_'))
               {
                  var_end++;
               }

               // Extract variable name
               char var_name[PATH_MAX];
               strncpy(var_name, token + var_start, var_end - var_start);
               var_name[var_end - var_start] = '\0';

               // Get variable value
               const char *var_value = get_var(var_name);
               if (var_value != NULL)
               {
                  strcpy(expanded + exp_pos, var_value);
                  exp_pos += strlen(var_value);
               }
               else
               {
                  // Variable not found, keep $VAR as is
                  expanded[exp_pos++] = '$';
                  strcpy(expanded + exp_pos, var_name);
                  exp_pos += strlen(var_name);
               }

               i = var_end;
            }
            else
            {
               expanded[exp_pos++] = token[i];
               i++;
            }
         }
         expanded[exp_pos] = '\0';

         argv[argc] = malloc(strlen(expanded) + 1);
         strcpy(argv[argc], expanded);
         argc++;
      }
      token = strtok(NULL, " ");
   }

   cmd->argc = argc;
   cmd->argv = argv;

   free(copy);
   return cmd;
}

void loop()
{
   while (true)
   {
      printf("sh-%s %s %c ", g_Shell.version, g_Shell.cwd, g_Shell.starter);

      char input[PATH_MAX];
      if (remaining_command[0] != '\0')
      {
         // Use remaining command from previous &&
         strcpy(input, remaining_command);
         remaining_command[0] = '\0';
      }
      else
      {
         fgets(input, PATH_MAX, stdin);
      }

      Pipeline *pipeline = parse_pipeline(input);
      if (pipeline != NULL)
      {
         if (pipeline->count == 1 && pipeline->commands[0] != NULL)
         {
            // Single command
            execute(pipeline->commands[0]);
         }
         else if (pipeline->count > 1)
         {
            // Multiple commands (pipe)
            execute_pipeline(pipeline);
            // Clean up pipeline
            for (int i = 0; i < pipeline->count; i++)
            {
               if (pipeline->commands[i] != NULL)
               {
                  free(pipeline->commands[i]->executable);
                  for (int j = 0; j < pipeline->commands[i]->argc; j++)
                  {
                     free(pipeline->commands[i]->argv[j]);
                  }
                  free(pipeline->commands[i]->argv);
                  if (pipeline->commands[i]->input_file)
                     free(pipeline->commands[i]->input_file);
                  if (pipeline->commands[i]->output_file)
                     free(pipeline->commands[i]->output_file);
                  free(pipeline->commands[i]);
               }
            }
            free(pipeline->commands);
            free(pipeline);
         }
      }
   }
}

void init()
{
   g_Shell.version = version;
   g_Shell.starter = '$';

   // Set up signal handler for Ctrl+C
   signal(SIGINT, sigint_handler);

   char *cwd;
   cwd = getcwd(NULL, 0);
   if (cwd != NULL)
   {
      g_Shell.cwd = cwd;
   }
   else
   {
      g_Shell.cwd = "/";
   }

   set("PATH", "/usr/local/bin:/usr/bin:/bin");
}

int main(int argc, char **argv)
{
   // init();
   // loop();
   return 0;
}
