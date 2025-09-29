# Basic shell implementation

This shell implementation has a few basic features:
- Execute commands.
- Chain commands together with the pipe `|`.
- Redirect standard input/output to/from files using `>` and `<`.
- Run processes on the background by ending line with `&`.

## Acknowledgements
The shell was built for the course "Operating System Concepts" at Radboud University.
Some elements, like the `execvp()` function for cpp-style strings, was given as template.
