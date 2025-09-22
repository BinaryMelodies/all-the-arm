
/* Tests the command line arguments and the environment variables */

package env;

import abi.Linux;

public class env {
	public static void main(String args[]) {
		// include the native library
		System.load(System.getProperty("user.dir") + "/syscall.so");

		// convert arguments and environment variable to a format suitable for the low-level interface
		byte[][] argv = new byte[args.length + 1][];

		// quick and dirty argv[0] generator
		String current_class_name = env.class.getName();
		int dollar_separator = current_class_name.indexOf('$');
		if(dollar_separator != -1)
			current_class_name = current_class_name.substring(0, dollar_separator);
		argv[0] = (current_class_name.replace('.', '/') + ".class").getBytes();

		for(int i = 0; i < args.length; i++)
			argv[i + 1] = args[i].getBytes();
		java.util.Map<String, String> env = java.lang.System.getenv();

		byte[][] envp = new byte[env.size() + 1][];
		int index = 0;
		for(java.util.Map.Entry<String, String> entry : env.entrySet())
		{
			envp[index] = (entry.getKey() + "=" + entry.getValue()).getBytes();
			index++;
		}
		envp[index] = null;

		// transfer to low-level interface
		_start(argv, envp);
	}

	static void _start(byte[][] argv, byte[][] envp) {
		Linux.exit(main(argv.length, argv, envp));
	}

	static void putstr(byte[] s) {
		Linux.write(1, s, 0, s.length);
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

	static int main(int argc, byte[][] argv, byte[][] envp) {
		putstr("argc = ".getBytes());
		putdec_u(argc);
		putstr("\n".getBytes());
		for(int i = 0; i < argc; i++)
		{
			putstr("argv[".getBytes());
			putdec_u(i);
			putstr("] = \"".getBytes());
			putstr(argv[i]);
			putstr("\"\n".getBytes());
		}
		for(int i = 0; envp[i] != null; i++)
		{
			putstr("envp[".getBytes());
			putdec_u(i);
			putstr("] = \"".getBytes());
			putstr(envp[i]);
			putstr("\"\n".getBytes());
		}
		return 0;
	}
}

