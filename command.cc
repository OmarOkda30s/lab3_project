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

void parse(std::vector<Token> &tokens)
{
    Command::_currentCommand.clear();
    SimpleCommand *currentSimpleCommand = new SimpleCommand();
    
    int i = 0;
    while (i < tokens.size())
    {
        Token token = tokens[i];
        
        if (token.type == TOKEN_EOF)
        {
            break;
        }
        
        if (token.type == TOKEN_PIPE)
        {
            Command::_currentCommand.insertSimpleCommand(currentSimpleCommand);
            currentSimpleCommand = new SimpleCommand();
            i++;
        }
        else if (token.type == TOKEN_REDIRECT)
        {
            Command::_currentCommand._outFile = strdup(tokens[i+1].value.c_str());
            Command::_currentCommand._append = 0;
            i += 2;
        }
        else if (token.type == TOKEN_APPEND)
        {
            Command::_currentCommand._outFile = strdup(tokens[i+1].value.c_str());
            Command::_currentCommand._append = 1;
            i += 2;
        }
        else if (token.type == TOKEN_INPUT)
        {
            Command::_currentCommand._inputFile = strdup(tokens[i+1].value.c_str());
            i += 2;
        }
        else if (token.type == TOKEN_ERROR)
        {
            Command::_currentCommand._errFile = strdup(tokens[i+1].value.c_str());
            i += 2;
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            Command::_currentCommand._background = 1;
            i++;
        }
        else
        {
            if (token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT)
            {
                currentSimpleCommand->insertArgument(strdup(token.value.c_str()));
            }
            i++;
        }
    }
    
    if (currentSimpleCommand->_numberOfArguments > 0)
    {
        Command::_currentCommand.insertSimpleCommand(currentSimpleCommand);
    }
    else
    {
        delete currentSimpleCommand;
    }
    
    Command::_currentCommand.execute();
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
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
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
        free(_simpleCommands[i]);
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
    if (_numberOfSimpleCommands == 0)
    {
        clear();
        prompt();
        return;
    }

    // Built-in exit command
    if (_numberOfSimpleCommands > 0 && _simpleCommands[0]->_numberOfArguments > 0)
    {
        if (strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0)
        {
            printf("Good bye!!\n");
            exit(0);
        }
        if (strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0)
        {
            char *dir;
            if (_simpleCommands[0]->_numberOfArguments > 1)
            {
                dir = _simpleCommands[0]->_arguments[1];
            }
            else
            {
                dir = getenv("HOME");
            }
            if (chdir(dir) < 0)
            {
                perror("cd failed");
            }
            clear();
            prompt();
            return;
        }
    }

    int defaultin = dup(0);
    int defaultout = dup(1);
    int defaulterr = dup(2);

    int fdin;
    int fdout;
    int fderr;

    if (_inputFile)
    {
        fdin = open(_inputFile, O_RDONLY);
        if (fdin < 0)
        {
            perror("Error opening input file");
            clear();
            prompt();
            return;
        }
    }
    else
    {
        fdin = dup(defaultin);
    }

    if (_errFile)
    {
        fderr = open(_errFile, O_CREAT | O_WRONLY | (_append ? O_APPEND : O_TRUNC), 0666);
    }
    else
    {
        fderr = dup(defaulterr);
    }
    dup2(fderr, 2);
    close(fderr);

    int ret = 0;
    int fd[2];

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        dup2(fdin, 0);
        close(fdin);

        if (i == _numberOfSimpleCommands - 1)
        {
            if (_outFile)
            {
                fdout = open(_outFile, O_CREAT | O_WRONLY | (_append ? O_APPEND : O_TRUNC), 0666);
            }
            else
            {
                fdout = dup(defaultout);
            }
        }
        else
        {
            pipe(fd);
            fdout = fd[1];
            fdin = fd[0];
        }

        dup2(fdout, 1);
        close(fdout);

        ret = fork();
        if (ret == 0)
        {
            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror("execvp");
            _exit(1);
        }
        else if (ret < 0)
        {
            perror("fork");
            return;
        }
    }

    dup2(defaultin, 0);
    dup2(defaultout, 1);
    dup2(defaulterr, 2);
    close(defaultin);
    close(defaultout);
    close(defaulterr);

    if (!_background)
    {
        waitpid(ret, NULL, 0);
    }

    clear();
    prompt();
}

void Command::prompt()
{
    printf("myshell>");
    fflush(stdout);
    std::string input;
    while (true)
    {
        std::getline(std::cin, input);
        if (input.empty())
        {
            printf("myshell>");
            fflush(stdout);
            continue;
        }
        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}