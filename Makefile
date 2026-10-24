all: myshell

myshell: command.o tokenizer.o
	g++ -std=c++11 -o myshell command.o tokenizer.o

command.o: command.cc command.h tokenizer.h
	g++ -std=c++11 -c command.cc

tokenizer.o: tokenizer.cc tokenizer.h
	g++ -std=c++11 -c tokenizer.cc

clean:
	rm -f myshell *.o