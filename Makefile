
mythread.a : mythread.o
	ar rcs mythread.a mythread.o

mythread.o : mythread.c
	gcc -c mythread.c -o mythread.o

clean:
	-rm -f *.o
	-rm -f *.a

