
all: syscall.so abi/Linux.class
	make -C hello
	make -C div10
	make -C env
	make -C others

clean:
	rm -f syscall.so abi_Linux.h abi/Linux.class
	make -C hello clean
	make -C div10 clean
	make -C env clean
	make -C others clean

distclean: clean
	rm -f *~ abi/*~
	make -C hello distclean
	make -C div10 distclean
	make -C env distclean
	make -C others distclean

abi/Linux.class: abi/Linux.java
	javac -h . abi/Linux.java

syscall.so: syscall.c
	gcc -shared -fPIC -o $@ $< -I/usr/lib64/jvm/java-21-openjdk-21/include -I/usr/lib64/jvm/java-21-openjdk-21/include/linux

.PHONY: all clean distclean

