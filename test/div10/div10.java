
package div10;

import abi.Linux;

public class div10 {
	public static void main(String args[]) {
		System.load(System.getProperty("user.dir") + "/syscall.so");
		_start();
	}

	static void _start() {
		Linux.exit(main());
	}


	static void putdec_u(int value) {
		int current_brk_value = Linux.brk(-1);
		byte[] buffer = new byte[4 * 5 / 2];
		int ptr = 4 * 5 / 2;
		do
		{
			int d = value % 10;
			value /= 10;
			buffer[--ptr] = (byte)('0' + d);
		} while(value != 0);
		Linux.write(1, buffer, ptr, 4 * 5 / 2 - ptr);
		Linux.brk(current_brk_value);
	}

	static int main() {
		putdec_u(12345);
		return 0;
	}
}

