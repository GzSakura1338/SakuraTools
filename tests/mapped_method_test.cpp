#include "mapped_method.h"
#include "include/jvmti.h"

extern "C" JNIEXPORT jint JNICALL
Java_MappedMethodTest_coldStatus(JNIEnv* env, jclass, jclass type) {
    JavaVM* vm = nullptr;
    jvmtiEnv* jvmti = nullptr;
    env->GetJavaVM(&vm);
    if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) return -1;
    jint count = 0;
    jmethodID* methods = nullptr;
    jvmtiError error = jvmti->GetClassMethods(type, &count, &methods);
    if (methods) jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));
    return error;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_MappedMethodTest_resolve(JNIEnv* env, jclass, jclass type, jstring name,
                             jstring forge, jstring descriptor, jboolean isStatic) {
    const char* n = env->GetStringUTFChars(name, nullptr);
    const char* f = env->GetStringUTFChars(forge, nullptr);
    const char* d = env->GetStringUTFChars(descriptor, nullptr);
    jmethodID method = n && f && d ? FindMappedMethod(env, type, n, f, d, isStatic != 0) : nullptr;
    if (n) env->ReleaseStringUTFChars(name, n);
    if (f) env->ReleaseStringUTFChars(forge, f);
    if (d) env->ReleaseStringUTFChars(descriptor, d);
    return method != nullptr;
}
