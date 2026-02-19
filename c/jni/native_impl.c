#include <jni.h>
#include <stdio.h>
#include <string.h>
#include "com_example_ipc_jni_NativeBridge.h"

JNIEXPORT jstring JNICALL
Java_com_example_ipc_jni_NativeBridge_process
  (JNIEnv *env, jobject obj, jstring input) {

    const char *json = (*env)->GetStringUTFChars(env, input, 0);

    printf("Received from Java: %s\n", json);

    const char *response = "{\"status\":\"OK\",\"result\":\"JNI processed\"}";

    (*env)->ReleaseStringUTFChars(env, input, json);

    return (*env)->NewStringUTF(env, response);
}
