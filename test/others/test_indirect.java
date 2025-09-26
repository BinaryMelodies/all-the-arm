
package others;

import abi.Linux;

public class test_indirect {
	public interface Call {
		public int call(int a, int b);
	}

	public static int add(int a, int b) {
		return a + b;
	}

	public static int mul(int a, int b) {
		return a * b;
	}

	public static int invoke(Call fun) {
		return fun.call(2, 3);
	}

	public static void _start() {
		Linux.exit(invoke(test_indirect::mul));
	}

	public static void main(String[] args) {
		System.load(System.getProperty("user.dir") + "/syscall.so");
		_start();
	}
}

