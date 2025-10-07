
#include <stdlib.h>
#include <fcntl.h>
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
	return result;
}

JNIEXPORT jint JNICALL Java_abi_Linux_read(JNIEnv * env, jclass _class, jint fd, jbyteArray buf, jint offset, jint count)
{
	jbyte * _buf = (*env)->GetByteArrayElements(env, buf, NULL);
	jint result = read(fd, _buf + offset, count);
	(*env)->ReleaseByteArrayElements(env, buf, _buf, 0);
	return result;
}

JNIEXPORT jint JNICALL Java_abi_Linux_open(JNIEnv * env, jclass _class, jbyteArray pathname, jint flags, jint mode)
{
	jbyte * _pathname = (*env)->GetByteArrayElements(env, pathname, NULL);
	jint result = open(_pathname, flags, mode);
	(*env)->ReleaseByteArrayElements(env, pathname, _pathname, JNI_ABORT);
	return result;
}

JNIEXPORT jint JNICALL Java_abi_Linux_close(JNIEnv * env, jclass _class, jint fd)
{
	return close(fd);
}

JNIEXPORT jint JNICALL Java_abi_Linux_brk(JNIEnv * env, jclass _class, jint address)
{
	// memory management is handled by the JVM
	return 0;
}

#ifdef __cplusplus
}
#endif

