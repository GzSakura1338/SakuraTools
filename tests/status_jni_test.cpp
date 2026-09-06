#include "../native/status_response.h"
#include "../native/proxy.h"

#include <cstdarg>
#include <cstdio>

void LogTo(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

jclass LoadClassInLoader(JNIEnv* env, jobject loader, const char* name) {
    jclass type = env->GetObjectClass(loader);
    jmethodID method = env->GetMethodID(type, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring text = env->NewStringUTF(name);
    jclass result = static_cast<jclass>(env->CallObjectMethod(loader, method, text));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = nullptr;
    }
    env->DeleteLocalRef(text);
    env->DeleteLocalRef(type);
    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_StatusCodecSmoke_probeColdVersion(JNIEnv* env, jclass, jobject loader) {
    JavaVM* vm = nullptr;
    jvmtiEnv* jvmti = nullptr;
    env->GetJavaVM(&vm);
    if (vm->GetEnv(reinterpret_cast<void**>(&jvmti), JVMTI_VERSION_1_2) != JNI_OK) return -1;
    jclass version = LoadClassInLoader(env, loader,
        "net.minecraft.network.protocol.status.ServerStatus$Version");
    if (!version) return -2;
    jint count = 0;
    jmethodID* methods = nullptr;
    jvmtiError error = jvmti->GetClassMethods(version, &count, &methods);
    if (methods) jvmti->Deallocate(reinterpret_cast<unsigned char*>(methods));
    env->DeleteLocalRef(version);
    return error;
}

extern "C" JNIEXPORT jobject JNICALL
Java_StatusCodecSmoke_buildNative(JNIEnv* env, jclass, jobject loader, jstring name, jstring address) {
    auto read = [&](jstring value) {
        jsize length = env->GetStringLength(value);
        const jchar* chars = env->GetStringChars(value, nullptr);
        std::u16string result(chars, chars + length);
        env->ReleaseStringChars(value, chars);
        return result;
    };
    return BuildStatusResponse(env, loader, FormatServerMotd(read(name), read(address)));
}
