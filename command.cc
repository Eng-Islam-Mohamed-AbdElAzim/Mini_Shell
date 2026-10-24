/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"
#include <errno.h>

#define SHOW_COMMAND_TABLE 1

static int logFileFd = -1;

static void writeToLog(pid_t childPid)
{
    char line[100];
    int length = snprintf(line, sizeof(line), "Child process %d terminated\n", (int)childPid);
    if (logFileFd >= 0)
        write(logFileFd, line, length);
}

static void handleChildExit(int signalNumber)
{
    int oldErrno = errno;
    int exitStatus;
    pid_t finished;
    while ((finished = waitpid(-1, &exitStatus, WNOHANG)) > 0)
        writeToLog(finished);
    errno = oldErrno;
}

static void setUpSignals()
{
    signal(SIGINT, SIG_IGN);

    logFileFd = open("shell_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);

    struct sigaction childAction;
    memset(&childAction, 0, sizeof(childAction));
    childAction.sa_handler = handleChildExit;
    sigemptyset(&childAction.sa_mask);
    childAction.sa_flags = SA_RESTART;
    sigaction(SIGCHLD, &childAction, NULL);
}

static void closeIfOpen(int fd)
{
    if (fd >= 0)
        close(fd);
}

static bool isWordToken(const Token &token)
{
    return token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT;
}

static bool collectWord(std::vector<Token> &tokens, size_t &pos, std::string &text, bool &wasQuoted)
{
    text = tokens[pos].value;
    wasQuoted = false;

    char quoteChar = text[0];
    if (quoteChar != '"' && quoteChar != '\'')
        return true;

    wasQuoted = true;
    while (text.size() < 2 || text[text.size() - 1] != quoteChar)
    {
        if (tokens[pos + 1].type == TOKEN_EOF)
            return false;
        pos++;
        text += ' ';
        text += tokens[pos].value;
    }
    text = text.substr(1, text.size() - 2);
    return true;
}

static void addWordToCommand(SimpleCommand *target, const std::string &text, bool wasQuoted)
{
    bool hasWildcard = text.find_first_of("*?") != std::string::npos;

    if (hasWildcard && !wasQuoted)
    {
        glob_t matches;
        int result = glob(text.c_str(), 0, NULL, &matches);
        if (result == 0)
        {
            for (size_t n = 0; n < matches.gl_pathc; n++)
                target->insertArgument(strdup(matches.gl_pathv[n]));
            globfree(&matches);
            return;
        }
        globfree(&matches);
    }
    target->insertArgument(strdup(text.c_str()));
}

static void abandonCommand(Command &command, SimpleCommand *pendingSimple, const std::string &message)
{
    if (!message.empty())
        fprintf(stderr, "myshell: syntax error: %s\n", message.c_str());

    command.insertSimpleCommand(pendingSimple);
    command.clear();
    free(command._simpleCommands);
}

static bool readRedirection(std::vector<Token> &tokens, size_t &pos, Command &command, std::string &problem)
{
    TokenType kind = tokens[pos].type;
    std::string symbol = tokens[pos].value;

    pos++;
    if (!isWordToken(tokens[pos]))
    {
        problem = "missing file name after '" + symbol + "'";
        return false;
    }

    std::string fileName;
    bool wasQuoted;
    if (!collectWord(tokens, pos, fileName, wasQuoted))
    {
        problem = "missing closing quote";
        return false;
    }

    if (kind == TOKEN_INPUT)
    {
        if (command._inputFile)
        {
            problem = "input redirected more than once";
            return false;
        }
        command._inputFile = strdup(fileName.c_str());
        return true;
    }

    if (kind == TOKEN_ERROR)
    {
        if (command._errFile)
        {
            problem = "error output redirected more than once";
            return false;
        }
        command._errFile = strdup(fileName.c_str());
        return true;
    }

    if (command._outFile || (kind == TOKEN_REDIRECT_AND_ERROR && command._errFile))
    {
        problem = "output redirected more than once";
        return false;
    }
    command._outFile = strdup(fileName.c_str());
    command._append = (kind == TOKEN_REDIRECT) ? 0 : 1;

    if (kind == TOKEN_REDIRECT_AND_ERROR)
    {
        command._errFile = strdup(fileName.c_str());
        command._out_error = 1;
    }
    return true;
}

void parse(std::vector<Token> &tokens)
{
    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    /*
      Implement this function 
      you must parse for all possible tokens scenario
      right now command.execute only print the format , you will inject it in your logic.

    */

    size_t pos = 0;
    while (tokens[pos].type != TOKEN_EOF)
    {
        switch (tokens[pos].type)
        {
        case TOKEN_COMMAND:
        case TOKEN_ARGUMENT:
        {
            std::string wordText;
            bool wasQuoted;
            if (!collectWord(tokens, pos, wordText, wasQuoted))
            {
                abandonCommand(currentCommand, currentSimpleCommand, "missing closing quote");
                return;
            }
            addWordToCommand(currentSimpleCommand, wordText, wasQuoted);
            break;
        }

        case TOKEN_PIPE:
            if (currentSimpleCommand->_numberOfArguments == 0)
            {
                abandonCommand(currentCommand, currentSimpleCommand, "missing command before '|'");
                return;
            }
            currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();
            break;

        case TOKEN_BACKGROUND:
            if (tokens[pos + 1].type != TOKEN_EOF)
            {
                abandonCommand(currentCommand, currentSimpleCommand, "'&' must be the last thing in the command");
                return;
            }
            currentCommand._background = 1;
            break;

        default:
        {
            std::string problem;
            if (!readRedirection(tokens, pos, currentCommand, problem))
            {
                abandonCommand(currentCommand, currentSimpleCommand, problem);
                return;
            }
            break;
        }
        }
        pos++;
    }

    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else if (currentCommand._numberOfSimpleCommands > 0)
    {
        abandonCommand(currentCommand, currentSimpleCommand, "missing command after '|'");
        return;
    }
    else
    {
        bool lineWasEmpty = (tokens.size() == 1);
        abandonCommand(currentCommand, currentSimpleCommand, lineWasEmpty ? "" : "missing command");
        return;
    }

    currentCommand.execute();
    free(currentCommand._simpleCommands);
}

static bool runBuiltIn(Command &command)
{
    if (command._numberOfSimpleCommands != 1)
        return false;

    char **words = command._simpleCommands[0]->_arguments;
    int wordCount = command._simpleCommands[0]->_numberOfArguments;

    if (strcmp(words[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        command.clear();
        exit(0);
    }

    if (strcmp(words[0], "cd") == 0)
    {
        const char *targetDir = (wordCount > 1) ? words[1] : getenv("HOME");
        if (targetDir == NULL)
            targetDir = "/";
        if (chdir(targetDir) != 0)
            fprintf(stderr, "cd: %s: %s\n", targetDir, strerror(errno));
        return true;
    }

    return false;
}

static bool openRedirectionFiles(Command &command, int &inputFd, int &outputFd, int &errorFd)
{
    inputFd = -1;
    outputFd = -1;
    errorFd = -1;

    if (command._inputFile)
    {
        inputFd = open(command._inputFile, O_RDONLY);
        if (inputFd < 0)
        {
            fprintf(stderr, "myshell: cannot open '%s': %s\n", command._inputFile, strerror(errno));
            return false;
        }
    }

    if (command._outFile)
    {
        int flags = O_WRONLY | O_CREAT | (command._append ? O_APPEND : O_TRUNC);
        outputFd = open(command._outFile, flags, 0644);
        if (outputFd < 0)
        {
            fprintf(stderr, "myshell: cannot open '%s': %s\n", command._outFile, strerror(errno));
            closeIfOpen(inputFd);
            return false;
        }
    }

    if (command._out_error)
    {
        errorFd = dup(outputFd);
    }
    else if (command._errFile)
    {
        errorFd = open(command._errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (errorFd < 0)
        {
            fprintf(stderr, "myshell: cannot open '%s': %s\n", command._errFile, strerror(errno));
            closeIfOpen(inputFd);
            closeIfOpen(outputFd);
            return false;
        }
    }
    return true;
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
#if SHOW_COMMAND_TABLE
    print();
#endif
    fflush(stdout);
    /*
        Write your code here
    */

    if (runBuiltIn(*this))
    {
        clear();
        return;
    }

    int inputFd, outputFd, errorFd;
    if (!openRedirectionFiles(*this, inputFd, outputFd, errorFd))
    {
        clear();
        return;
    }

    int total = _numberOfSimpleCommands;
    std::vector<int> pipeFds(2 * (total - 1));
    for (int n = 0; n < total - 1; n++)
    {
        if (pipe(&pipeFds[2 * n]) < 0)
            perror("pipe");
    }

    pid_t lastPid = -1;
    for (int i = 0; i < total; i++)
    {
        pid_t childPid = fork();
        if (childPid < 0)
        {
            perror("fork");
            break;
        }

        if (childPid == 0)
        {
            if (i > 0)
                dup2(pipeFds[2 * (i - 1)], 0);
            else if (inputFd >= 0)
                dup2(inputFd, 0);

            if (i < total - 1)
                dup2(pipeFds[2 * i + 1], 1);
            else if (outputFd >= 0)
                dup2(outputFd, 1);

            if (errorFd >= 0)
                dup2(errorFd, 2);

            for (size_t n = 0; n < pipeFds.size(); n++)
                close(pipeFds[n]);
            closeIfOpen(inputFd);
            closeIfOpen(outputFd);
            closeIfOpen(errorFd);

            if (!_background)
                signal(SIGINT, SIG_DFL);

            char **args = _simpleCommands[i]->_arguments;
            execvp(args[0], args);

            if (errno == ENOENT)
                fprintf(stderr, "myshell: %s: command not found\n", args[0]);
            else
                fprintf(stderr, "myshell: %s: %s\n", args[0], strerror(errno));
            _exit(1);
        }

        lastPid = childPid;
    }

    for (size_t n = 0; n < pipeFds.size(); n++)
        close(pipeFds[n]);
    closeIfOpen(inputFd);
    closeIfOpen(outputFd);
    closeIfOpen(errorFd);

    if (lastPid > 0)
    {
        if (_background)
        {
            printf("[background] process %d started\n", (int)lastPid);
        }
        else
        {
            int exitStatus;
            if (waitpid(lastPid, &exitStatus, 0) > 0)
                writeToLog(lastPid);
        }
    }

    clear();
}

void Command::prompt()
{
    std::string input;
    while (true)
    {
        printf("myshell>");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            printf("\nGood bye!!\n");
            break;
        }

        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    setUpSignals();
    Command::_currentCommand.prompt();
    return 0;
}