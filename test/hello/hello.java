
package hello;

import abi.Linux;

public class hello {
	public static void main(String[] args) {
		System.load(System.getProperty("user.dir") + "/syscall.so");
		_start();
	}

	static int main() {
		Linux.write(1, new byte[] { 'H', 'e', 'l', 'l', 'o', '!', '\n', '\0' }, 0, 7);
		return 123;
	}

	static void _start() {
		Linux.exit(main());
	}
}

