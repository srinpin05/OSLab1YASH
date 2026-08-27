main: main.c
	gcc main.c -o main -lreadline
run: main
	./main
