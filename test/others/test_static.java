
import abi.Linux;

public class test_static {
	public static int static_field = 123;
	public static final int final_field = 45678910;
	static {
		static_field += final_field;
		Linux.exit(0);
	}
}

