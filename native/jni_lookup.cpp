#include "jni_lookup.h"
#include <cstring>

namespace proxy_server {

jmethodID findMethodByDescriptor(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE)
        return nullptr;
    jmethodID hit = nullptr;
    int matches = 0;
    for (jint i = 0; i < count; ++i) {
        char *n = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetMethodName(mids[i], &n, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = mids[i];
            ++matches;
            LogTo("  findMethodByDescriptor(%s, static=%d): '%s'", desc, wantStatic ? 1 : 0, n ? n : "?");
        }
        if (n)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(n));
        if (s)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    if (matches > 1) LogTo("bindings: ambiguous method descriptor %s (%d matches)", desc, matches);
    return matches == 1 ? hit : nullptr;
}


jfieldID findFieldByDescriptor(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jfieldID* fids = nullptr;
    if (g_jvmti->GetClassFields(klass, &count, &fids) != JVMTI_ERROR_NONE)
        return nullptr;
    jfieldID hit = nullptr;
    int matches = 0;
    for (jint i = 0; i < count; ++i) {
        char *n = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetFieldName(klass, fids[i], &n, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetFieldModifiers(klass, fids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = fids[i];
            ++matches;
            LogTo("  findFieldByDescriptor(%s, static=%d): '%s'", desc, wantStatic ? 1 : 0, n ? n : "?");
        }
        if (n)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(n));
        if (s)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(fids));
    if (matches > 1) LogTo("bindings: ambiguous field descriptor %s (%d matches)", desc, matches);
    return matches == 1 ? hit : nullptr;
}

jclass findLoadedBySig(JNIEnv* env, const char* sig) {
    jint count = 0;
    jclass* classes = nullptr;
    if (g_jvmti->GetLoadedClasses(&count, &classes) != JVMTI_ERROR_NONE)
        return nullptr;
    jclass hit = nullptr;
    for (jint i = 0; i < count; ++i) {
        char* s = nullptr;
        if (g_jvmti->GetClassSignature(classes[i], &s, nullptr) != JVMTI_ERROR_NONE)
            continue;
        if (s && std::strcmp(s, sig) == 0) {
            hit = (jclass)env->NewLocalRef(classes[i]);
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
            break;
        }
        if (s)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
    }
    for (jint i = 0; i < count; ++i)
        env->DeleteLocalRef(classes[i]);
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(classes));
    return hit;
}

jclass loadOrFind(JNIEnv* env, jobject mcLoader, const char* dot, const char* sig) {
    jclass c = LoadClassInLoader(env, mcLoader, dot);
    if (!c)
        c = findLoadedBySig(env, sig);
    if (!c)
        LogTo("BServer: cannot find class %s", dot);
    return c;
}

} // namespace proxy_server

std::string JniClassName(JNIEnv* env, jobject o) {
    if (!o || !g_jvmti)
        return {};
    jclass c = env->GetObjectClass(o);
    if (!c)
        return {};
    char* sig = nullptr;
    jvmtiError rc = g_jvmti->GetClassSignature(c, &sig, nullptr);
    env->DeleteLocalRef(c);
    if (rc != JVMTI_ERROR_NONE || !sig)
        return {};
    std::string out;
    const char* p = sig;
    if (*p == 'L') {
        ++p;
        for (; *p && *p != ';'; ++p)
            out.push_back(*p == '/' ? '.' : *p);
    } else
        out = sig;
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(sig));
    return out;
}
