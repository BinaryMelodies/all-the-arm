
package abi;

public class Linux {
	public static native void exit(int status);
	public static native int write(int fd, byte[] buf, int offset, int count);
	public static native int brk(int address);
}

