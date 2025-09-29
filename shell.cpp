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
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/param.h>
#include <signal.h>
#include <fcntl.h>
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
vector<string> split_string(const string& str, char delimiter = ' ', bool keep_empty = false) {
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

bool empty_or_whitespace(string str) {
  return str.find_first_not_of(' ') == std::string::npos;
}

// Split a string by a delimiter, respecting quotes.
// success = false iff there is an odd number of one of the quote types.
// Keep empty signals whether any empty splits should be recorded. 
vector<string> split_respecting_quotes(const string& line, const char delimiter, bool& success, bool keep_empty = false) {
  vector<string> parts;
  string current;
  bool in_single = false, in_double = false;

  for (char c : line) {
    if (c == '\'' && !in_double) {
      in_single = !in_single; // toggle, don't add quote to charlist.
    }
    else if (c == '"' && !in_single) {
      in_double = !in_double; // toggle, don't add quote to charlist.
    }
    else if (!in_single && !in_double) { // Not in quote
      if (isspace(c) && delimiter == ' ') {
        // Special case: splitting on spaces
        if (keep_empty || !empty_or_whitespace(current)) {
          parts.push_back( current );
        }
        current.clear();
      }
      else if (c == delimiter) {
        // Not in quote
        if (keep_empty || !empty_or_whitespace(current)) {
          parts.push_back( current );
        }
        current.clear();
      } else {
        current.push_back( c );
      }
    }
    else { // In quote: Just add the character.
      current.push_back( c );
    }
  }

  // Open quote: this is a syntax error.
  // Only set to false since it's true by default, and other sources may edit it.
  if (in_single || in_double)
    success = false;

  // Only if the string doesn't have characters and keep empty is on, save to the vector.
  if (keep_empty || !empty_or_whitespace(current))
    parts.push_back( current );

  return parts;
}

// Trims the given string of any leading or trailing whitespaces.
std::string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t");
    size_t end = s.find_last_not_of(" \t");
    return start == string::npos ? "" : s.substr(start, end - start + 1);
}

// Checks whether there are any empty (or whtespace) elements. If there are any,
// returns true. Strips any nonempty arguments of leading or trailing whitespaces.
bool process_commands(vector<string>& commands) {
  bool exists_empty = false;
  int n_commands = commands.size();

  for (auto it = commands.begin(); it != commands.end();) {
    // cout << "checking command split for `" << cmd << "`" << endl;

    string cmd = *it;
    *it = trim(cmd); // Trim the command of any whitespaces.

    // Check if command is empty
    if (it->find_first_not_of(" \t") == string::npos) {
      it = commands.erase(it);
      exists_empty = true;
    } else {
      it++; // No removal necessary.
    }
  }

  return exists_empty && n_commands > 1;
}

// note: For such a simple shell, there is little need for a full-blown parser (as in an LL or LR capable parser).
// Here, the user input can be parsed using the following approach.
// First, divide the input into the distinct commands (as they can be chained, separated by `|`).
// Next, these commands are parsed separately. The first command is checked for the `<` operator, and the last command for the `>` operator.
Expression parse_command_line(string commandLine, bool& success) {
  Expression expression;

  // Split according to quotes.
  bool balanced_quotes = true; // Separate variable used in case other parse failures are added in the future.
  vector<string> commands = split_respecting_quotes( commandLine, '|', balanced_quotes, true );
  
  // Check if there are any empty strings: If this is the case remove them from commands.
  bool incorrect_pipe_usage = process_commands( commands );

  for (size_t i = 0; i < commands.size(); ++i) {
    string& line = commands[i];

    // Same as splitting on '|', keep quotes in mind.
    vector<string> args = split_respecting_quotes( line, ' ', balanced_quotes );

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

  if (!balanced_quotes) {
    cerr << "Parse error: Unbalanced quotes in one or more commands." << endl;
    success = false;
  }

  if (incorrect_pipe_usage) {
    cerr << "Parse error: Incorrect usage of `|`." << endl;
    success = false;
  }

  return expression;
}

// Assumes the leading spaces/tabs are stripped. Checks whether the string starts with `cd` or `exit`.
bool is_internal_command(string& string) {
  return string.substr(0,2) == "cd" || string.substr(0, 4) == "exit";
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

    return sc;
  }

  return -1;
}


// Redirects standard input of the current process to the input file (if not
// empty or whitespace). Closes standard input if background is set to true.
void setup_input(const string &file_in, bool background) {
  if (!empty_or_whitespace(file_in)) {
    // There is an input file. Redirect standard in to this file.
    int fd = open(file_in.c_str(), O_RDONLY);
    if (fd == -1) {
      perror("open input");
      exit(errno);
    }

    // Use the dup2 syscall to redirect standard input to the input file.
    if (dup2(fd, STDIN_FILENO) == -1) {
      perror("dup2 input");
      exit(errno);
    }

    // Afterwards, close the fd for the file itself since it is not needed
    // anymore.
    close(fd);
  } else if (background) {
    // For background jobs with no input redirection, close stdin (this was a
    // project requirement)
    if (close(STDIN_FILENO) == -1) {
      perror("close stdin");
      exit(errno);
    }
  }
}

// Redirects standard output of the current process to the output file (if not
// empty or whitespace).
void setup_output(const string &file_out) {
  if (!empty_or_whitespace(file_out)) {
    // There is an output file: redirect standard output to this file.
    // O_WRONLY: You need to write, not read.
    // O_CREAT: Create the file if not present.
    // O_TRUNC: Overwrite existing files of the same name.
    // 0644: Seems to be standard permission flag if you want to create and
    // write to files.
    int fd = open(file_out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
      perror("open output");
      exit(errno);
    }

    // Use the dup2 syscall to redirect standard output to the output file.
    if (dup2(fd, STDOUT_FILENO) == -1) {
      perror("dup2 output");
      exit(errno);
    }

    // Close file's fd since it is not needed anymore.
    close(fd);
  }
}

// Closes all pipes as given as arguments.
void close_all_pipes(const vector<array<int, 2>> &pipes) {
  for (const auto &p : pipes) {
    // Close both ends.
    close(p[0]);
    close(p[1]);
  }
}

void execute_commands(
    vector<Command> &commands, // Commands as split with pipes
    vector<pid_t> &bg_pids,    // All pids of subprocesses that are ran in the
                               // background. Only write to.
    string &file_in,  // Input file name in case the first command uses an input
                      // file.
    string &file_out, // Output file name in case the last command uses an
                      // output file.
    bool background   // Whether `&` is used in the command (in this case run
                      // processes in the background).
) {
  int n_commands =
      commands.size(); // To remove overhead of calling .size() every time.
  vector<pid_t>
      pids; // To keep track of which subprocesses to wait for later on.

  if (n_commands == 0) {
    return;
  }
  
  // Create pipes. Only do so if there is more than one command,
  // and make one less since pipes are inbetween processes.
  // The standard output of process i redirected to the write end of pipe i.
  // The standard input of process i is redirected to the read end of pipe i-1.
  vector<array<int, 2>> pipes(n_commands > 1 ? n_commands - 1 : 0);

  for (int i = 0; i < n_commands - 1; i++) {
    // Initialize the pipe to the pipes vector.
    if (pipe(pipes[i].data()) == -1) {
      perror("pipe");
      return;
    }
  }

  // Create a new subprocess for each command.
  for (int i = 0; i < n_commands; i++) {

    // Do this through fork(). So n_commands different child forks from the
    // parent process are made.
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      return;
    }

    if (pid == 0) {
      // --- Branch of the child process start ---

      // First, setup the inputs and outputs for the first and last command.
      if (i == 0) {
        setup_input(file_in, background);
      }
      if (i == n_commands - 1) {
        setup_output(file_out);
      }

      // Setup pipes' input ends (not for the first command)
      if (i > 0) {
        // Use the dup2 syscall for this and redirect pipes (with indexes as
        // described before).
        if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
          perror("dup2 pipe in");
          exit(errno);
        }
      }
      // Setup pipes' output ends (not for the last command)
      if (i < n_commands - 1) {
        // Use the dup2 syscall again for this and redirect pipes (with indexes
        // as described before).
        if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
          perror("dup2 pipe out");
          exit(errno);
        }
      }

      // Child does not need original pipes anymore, as they are now all
      // redirected to standard input/output with dup2 syscalls.
      close_all_pipes(pipes);

      // Don't execute chained commands with ones such as `cd` or `exit`.
      bool invalid_command = is_internal_command(commands[i].parts[0]) && n_commands > 1;
      if (invalid_command) {
        exit(0);
      }

      // Execute the command.
      execute_command(commands[i]);

      // Something went wrong if we're still executing this.
      std::string msg = "`" + commands[i].parts[0] + "`";
      perror(msg.c_str());

      exit(errno);
    }

    // Parent pushes the pid to the pids list to keep track of which pids to
    // wait for.
    pids.push_back(pid);
    if (background) {
      // If it is a background process, future blocks also need to know what
      // pids to wait for.
      bg_pids.push_back(pid);
    }
  }

  // Parent closes all pipes since they're not needed here.
  close_all_pipes(pipes);

  if (background) {
    // In most shells the job number is printed but we don't have jobs (just stuff in the background) for ths shell.
    // So we just print [bg] instead.
    std::cout << "[bg] " << pids.back() << std::endl;
  } else {
    // Run in foreground; wait for all processes and block.
    for (pid_t pid : pids) {
      int status;
      if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
      } else if (WIFSIGNALED(status)) {
        // Only report errors for processes that were terminated through a
        // signal. Other processes that were terminated regularly will already
        // have printed any relevant/necessary error messages.

        int sig_code = WTERMSIG(status) ;
        std::cerr << strsignal(sig_code) << std::endl;
      }
    }
  }
}

void poll_background_processes(vector<pid_t>& pids) {
  int status;

  // Use iterator to avoid concurrent modification exceptions.
  for (auto it = pids.begin(); it != pids.end();) {
    // Poll whether the process has terminated or not.
    pid_t result = waitpid(*it, &status, WNOHANG);
    if (result == -1) {
      perror("waitpid");
      it = pids.erase(it);
    } else if (result > 0) {
      //  Process has terminated. Print done!
      std::cout << "[done] " << *it << std::endl;
      it = pids.erase(it);
    } else {
      it++; // process is still busy.
    }
  }
}

void execute_expression(Expression& expression, vector<pid_t>& bg_pids) {
  // Check for empty expression
  if (expression.commands.size() == 0) {
    cerr << strerror(EINVAL) << endl;
    return; 
  }
  
  // Handle intern commands (like 'cd' and 'exit')
  int internal_commands_rc = handle_internal_commands(expression);
  if (internal_commands_rc != -1) { // -1 = handle_internal_commands didn't execute a command.
    // Something happened, but all errors/results are already printed.
    return; 
  }

  // Execute commands.
  execute_commands(expression.commands, bg_pids, expression.inputFromFile, expression.outputToFile, expression.background);
}

int shell(bool showPrompt) {
  vector<pid_t> bg_pids;

  while (cin.good()) {
    // Poll whether any background processes have finished running in the meantime.
    poll_background_processes(bg_pids);

    string commandLine = request_command_line(showPrompt);

    bool parse_success = true;
    Expression expression = parse_command_line(commandLine, parse_success);
    if (!parse_success) continue; // Don't execute any bad inputs.

    execute_expression(expression, bg_pids);
  }
  return 0;
}
