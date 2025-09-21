
import abi.Linux;

public class test_string {
	public static void _start() {
		byte[] message = "Hello, World!".getBytes();
		Linux.write(1, message, 0, message.length);
		Linux.exit(0);
	}
}

