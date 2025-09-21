
import abi.Linux;

public class test_clinit {
	static {
		byte[] message = "Called from <clinit>\n".getBytes();
		Linux.write(1, message, 0, message.length);
	}

	public static void _start() {
		byte[] message = "Called from _start\n".getBytes();
		Linux.write(1, message, 0, message.length);
		Linux.exit(123);
	}
}

