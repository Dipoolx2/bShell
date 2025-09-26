/**
	* Shell framework
	* course Operating Systems
	* Radboud University
	* v22.09.05

	Student names:
	- Siert Groote
	- Peter Wardle
*/

// function/class definitions you are going to use
#include <cstdio>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <string.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <array>
#include <list>
#include <optional>

// although it is good habit, you don't have to type 'std' before many objects by including this line
using namespace std;

struct Command {
  vector<string> parts = {};
};

struct Expression {
  vector<Command> commands;
  string inputFromFile;
  string outputToFile;
  bool background = false;
};

// Parses a string to form a vector of arguments. The separator is a space char (' ').
vector<string> split_string(const string& str, char delimiter = ' ') {
  vector<string> retval;
  for (size_t pos = 0; pos < str.length(); ) {
    // look for the next space
    size_t found = str.find(delimiter, pos);
    // if no space was found, this is the last word
    if (found == string::npos) {
      retval.push_back(str.substr(pos));
      break;
    }
    // filter out consequetive spaces
    if (found != pos)
      retval.push_back(str.substr(pos, found-pos));
    pos = found+1;
  }
  return retval;
}

// wrapper around the C execvp so it can be called with C++ strings (easier to work with)
// always start with the command itself
// DO NOT CHANGE THIS FUNCTION UNDER ANY CIRCUMSTANCE
int execvp(const vector<string>& args) {
  // build argument list
  const char** c_args = new const char*[args.size()+1];
  for (size_t i = 0; i < args.size(); ++i) {
    c_args[i] = args[i].c_str();
  }
  c_args[args.size()] = nullptr;
  // replace current process with new process as specified
  int rc = ::execvp(c_args[0], const_cast<char**>(c_args));
  // if we got this far, there must be an error
  int error = errno;
  // in case of failure, clean up memory (this won't overwrite errno normally, but let's be sure)
  delete[] c_args;
  errno = error;
  return rc;
}

// Executes a command with arguments. In case of failure, returns error code.
int execute_command(const Command& cmd) {
  auto& parts = cmd.parts;
  if (parts.size() == 0)
    return EINVAL;

  // execute external commands
  int retval = execvp(parts);
  return retval ? errno : 0;
}

void display_prompt() {
  char buffer[512];
  char* dir = getcwd(buffer, sizeof(buffer));
  if (dir) {
    cout << "\e[32m" << dir << "\e[39m"; // the strings starting with '\e' are escape codes, that the terminal application interpets in this case as "set color to green"/"set color to default"
  }
  cout << "$ ";
  flush(cout);
}

string request_command_line(bool showPrompt) {
  if (showPrompt) {
    display_prompt();
  }
  string retval;
  getline(cin, retval);
  return retval;
}

// note: For such a simple shell, there is little need for a full-blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parse_command_line(string commandLine) {
  Expression expression;
  vector<string> commands = split_string(commandLine, '|');
  for (size_t i = 0; i < commands.size(); ++i) {
    string& line = commands[i];

    // ------------ TODO: Change this line s.t. quotations are picked up properly.
    vector<string> args = split_string(line, ' ');
    // ------------

    if (i == commands.size() - 1 && args.size() > 1 && args[args.size()-1] == "&") {
      expression.background = true;
      args.resize(args.size()-1);
    }
    if (i == commands.size() - 1 && args.size() > 2 && args[args.size()-2] == ">") {
      expression.outputToFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    if (i == 0 && args.size() > 2 && args[args.size()-2] == "<") {
      expression.inputFromFile = args[args.size()-1];
      args.resize(args.size()-2);
    }
    expression.commands.push_back({args});
  }
  return expression;
}

// Return -1 if no commands were triggered. Otherwise return status code.
int handle_internal_commands(Expression& expression) {
  // If there is a singular command
  if (expression.commands.size() != 1) return -1;
  vector<string> cmdParts = expression.commands[0].parts;

  //  If it is exit, exit the terminal
  if (cmdParts.size() == 1 && cmdParts[0] == "exit") {
    exit(0);

    // Something went wrong: forward error code
    perror("exit");
    return errno;
  }

  //  If it is cd, change the current directory.
  if (cmdParts.size() == 2 && cmdParts[0] == "cd") {
    int sc = chdir(cmdParts[1].c_str());

    if (sc != 0) {
      perror("cd");
      return errno; // Something went wrong.
    }
  }

  return -1;
}

void execute_commands(vector<Command>& commands) {  
  const int n_commands = commands.size();
  vector<pid_t> pids;

  // Write end for process i is at pipe i
  // Read end for process i is at pipe i - 1
  vector<array<int, 2>> pipes(n_commands - 1);

  // Initialize all pipes (before creating subprocesses)
  for (int i = 0; i < n_commands - 1; i++) {
    auto p = pipes[i].data();
    if ( pipe(p) == -1 ) { // Test for pipe creation errors
      perror("pipe");

      // Propagate error message to execute_commands caller (don't kill the whole shell).
      return;
    }
  }

  // Start executing commands.
  for (int i = 0; i < n_commands; i++) {
    pid_t pid = fork();
    if (pid == -1) { // Test for forking errors
      perror("fork");

      // Again, don't kill the whole shell if something goes wrong with this.
      return;
    }

    if (pid == 0) { // Child process
      // Check if it's the first command: Don't redirect input from a pipe for this one's stdin.
      if (i > 0) {
        int ret = dup2( pipes.at( i - 1 ).at(0), STDIN_FILENO );
        if (ret == -1) {
          perror("dup2");
          exit(errno);
        }
      }

      // Check if it's the last command: Don't redirect output to a pipe for this one's stdout.
      if (i < n_commands - 1) {
        int ret = dup2( pipes.at(i).at(1), STDOUT_FILENO );
        if (ret == -1) {
          perror("dup2");
          exit(errno);
        }
      }

      // All leftover pipe file descriptors can be closed in the child, as the pipe's 
      // entry/exit points are now redirected standard input/output.
      for (array<int, 2> p : pipes) {
        close(p.at(0));
        close(p.at(1));
      }

      // Finally execute the command.
      execvp( commands.at(i).parts );

      // If this point has been reached, something went wrong.
      string error_msg = "`" + commands.at(i).parts.at(0) + "`";
      perror(error_msg.c_str());
      exit(errno);
    }

    // Parent saves pid so we know which processes to wait for later on.
    pids.push_back(pid);
  }

  // The parent still has the pipes open. It does not need them, so they should be closed.
  for (array<int, 2>& p : pipes) {
    close(p.at(0));
    close(p.at(1));
  }

  // Parent process blocks and waits for all children to finish.
  for (int i = 0; i < n_commands; i++) {
    int status;
    if ( waitpid( pids.at(i), &status, 0 ) == -1) {
      // waitpid failed
      perror("waitpid");

      // TODO: Test for exit codes of child processes and possibly print errors accordingly.
    }
  }

}

void execute_expression(Expression& expression) {
  // Check for empty expression
  if (expression.commands.size() == 0) {
    cerr << EINVAL << endl;
    return; 
  }
  
  // Handle intern commands (like 'cd' and 'exit')
  int internal_commands_rc = handle_internal_commands(expression);
  if (internal_commands_rc != -1) { // -1 = handle_internal_commands didn't execute a command.
    // Something happened, but all errors/results are already printed.
    return; 
  }

  // Execute commands.
  execute_commands(expression.commands);
}

int shell(bool showPrompt) {
  while (cin.good()) {
    string commandLine = request_command_line(showPrompt);
    Expression expression = parse_command_line(commandLine);

    execute_expression(expression);
  }
  return 0;
}
