
#include <stdlib.h>
#include <unistd.h>
#include <jni.h>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_abi_Linux_exit(JNIEnv * env, jclass _class, jint status)
{
	exit(status);
}

JNIEXPORT jint JNICALL Java_abi_Linux_write(JNIEnv * env, jclass _class, jint fd, jbyteArray buf, jint offset, jint count)
{
	jbyte * _buf = (*env)->GetByteArrayElements(env, buf, NULL);
	jint result = write(fd, _buf + offset, count);
	(*env)->ReleaseByteArrayElements(env, buf, _buf, JNI_ABORT);
	return 0;
}

JNIEXPORT jint JNICALL Java_abi_Linux_brk(JNIEnv * env, jclass _class, jint address)
{
	// memory management is handled by the JVM
	return 0;
}

#ifdef __cplusplus
}
#endif

