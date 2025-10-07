
/* Linux system calls */

package abi;

public class Linux {
	public static native void exit(int status);
	public static native int read(int fd, byte[] buf, int offset, int count);
	public static native int write(int fd, byte[] buf, int offset, int count);
	public static native int open(byte[] pathname, int flags, int mode);
	public static native int close(int fd);
	public static native int brk(int address);

	public static final int O_RDONLY = 00;
	public static final int O_WRONLY = 01;
	public static final int O_RDWR = 02;
	public static final int O_CREAT = 0100;
	public static final int O_EXCL = 0200;
	public static final int O_NOCTTY = 0400;
	public static final int O_TRUNC = 01000;
	public static final int O_APPEND = 02000;
	public static final int O_NONBLOCK = 04000;
	public static final int O_NDELAY = O_NONBLOCK;
	public static final int O_SYNC = 04010000;
	public static final int O_ASYNC = 020000;
}

