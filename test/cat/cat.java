
package cat;

import abi.Linux;

public class cat {
	public static void main(String[] args) {
		// include the native library
		System.load(System.getProperty("user.dir") + "/syscall.so");

		// transfer to low-level interface
		_start();
	}

	public static void _start() {
		Linux.exit(main());
	}

	public static int main() {
		byte[] c = new byte[1];
		while(Linux.read(0, c, 0, 1) == 1)
		{
			Linux.write(1, c, 0, 1);
		}
		return 0;
	}
}

