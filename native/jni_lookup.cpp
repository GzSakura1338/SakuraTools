#include "jni_lookup.h"
#include <cstring>

namespace proxy_server {

jmethodID findMethodByDescriptor(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE)
        return nullptr;
    jmethodID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *n = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetMethodName(mids[i], &n, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = mids[i];
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
    return hit;
}

int findMethodsByDescriptor(jclass klass, const char* desc, bool wantStatic, jmethodID* out, int maxOut) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE)
        return 0;
    int n = 0;
    for (jint i = 0; i < count && n < maxOut; ++i) {
        char *nm = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetMethodName(mids[i], &nm, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            out[n++] = mids[i];
            LogTo("  findMethodsByDescriptor(%s)[%d]: '%s'", desc, n - 1, nm ? nm : "?");
        }
        if (nm)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(nm));
        if (s)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    return n;
}

jmethodID findMethodByDescriptorExcept(jclass klass, const char* desc, bool wantStatic, const char* const* excl,
                                       int nExcl) {
    jint count = 0;
    jmethodID* mids = nullptr;
    if (g_jvmti->GetClassMethods(klass, &count, &mids) != JVMTI_ERROR_NONE)
        return nullptr;
    jmethodID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *nm = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetMethodName(mids[i], &nm, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetMethodModifiers(mids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        bool excluded = false;
        for (int e = 0; nm && e < nExcl; ++e)
            if (std::strcmp(nm, excl[e]) == 0) {
                excluded = true;
                break;
            }
        if (!excluded && s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = mids[i];
            LogTo("  findMethodByDescriptorExcept(%s): '%s'", desc, nm ? nm : "?");
        }
        if (nm)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(nm));
        if (s)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(s));
        if (g)
            g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(g));
    }
    g_jvmti->Deallocate(reinterpret_cast<unsigned char*>(mids));
    return hit;
}

jfieldID findFieldByDescriptor(jclass klass, const char* desc, bool wantStatic) {
    jint count = 0;
    jfieldID* fids = nullptr;
    if (g_jvmti->GetClassFields(klass, &count, &fids) != JVMTI_ERROR_NONE)
        return nullptr;
    jfieldID hit = nullptr;
    for (jint i = 0; i < count && !hit; ++i) {
        char *n = nullptr, *s = nullptr, *g = nullptr;
        if (g_jvmti->GetFieldName(klass, fids[i], &n, &s, &g) != JVMTI_ERROR_NONE)
            continue;
        jint mods = 0;
        g_jvmti->GetFieldModifiers(klass, fids[i], &mods);
        bool isStatic = (mods & 0x0008) != 0;
        if (s && std::strcmp(s, desc) == 0 && isStatic == wantStatic) {
            hit = fids[i];
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
    return hit;
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
