#include "status_response.h"
#include "proxy.h"

namespace {
std::u16string displayLine(const std::u16string& text, int maxWidth) {
    std::u16string out;
    int width = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char16_t c = text[i];
        if (c == u'\u00a7') { if (i + 1 < text.size()) ++i; continue; }
        if (c < 0x20 || c == 0x7f || c == u'\u2028' || c == u'\u2029') c = u' ';
        const bool pair = c >= 0xd800 && c <= 0xdbff && i + 1 < text.size()
            && text[i + 1] >= 0xdc00 && text[i + 1] <= 0xdfff;
        if (!pair && c >= 0xd800 && c <= 0xdfff) continue;
        // Conservative default-font widths keep both MOTD lines inside the list row.
        int advance = c == u' ' ? 4 : (c < 0x80 ? 6 : 9);
        if (width + advance > maxWidth - 9) { out += u'\u2026'; break; }
        out += c;
        if (pair) out += text[++i];
        width += advance;
    }
    return out;
}
}

std::u16string FormatServerMotd(const std::u16string& serverName,
                               const std::u16string& serverAddress) {
    std::u16string title = displayLine(serverName.empty() ? serverAddress : serverName, 135);
    if (title.empty()) title = u"\u5f85\u8fde\u63a5";
    std::u16string detail = serverAddress.empty()
        ? u"\u7b49\u5f85 A \u8fde\u63a5\u670d\u52a1\u5668"
        : displayLine(serverAddress, 240);
    return u"\u00a7d\u00a7lSakura Tools \u00a7r\u00a78\u2192 \u00a7f" + title
        + u"\n\u00a77" + detail + u"\u00a7r";
}

jobject BuildStatusResponse(JNIEnv* env, jobject mcLoader, const std::u16string& motdText) {
    if (env->PushLocalFrame(32) != JNI_OK) {
        LogTo("STATUS: cannot allocate local frame");
        env->ExceptionDescribe();
        env->ExceptionClear();
        return nullptr;
    }
    const char* step = "loading status classes";
    auto load = [&](const char* name) {
        step = name;
        return LoadClassInLoader(env, mcLoader, name);
    };
    auto build = [&]() -> jobject {
        jclass component = load("net.minecraft.network.chat.Component");
        if (!component) return nullptr;
        jclass version = load("net.minecraft.network.protocol.status.ServerStatus$Version");
        if (!version) return nullptr;
        jclass players = load("net.minecraft.network.protocol.status.ServerStatus$Players");
        if (!players) return nullptr;
        jclass serverStatus = load("net.minecraft.network.protocol.status.ServerStatus");
        if (!serverStatus) return nullptr;
        jclass packetClass = load("net.minecraft.network.protocol.status.ClientboundStatusResponsePacket");
        if (!packetClass) return nullptr;
        step = "java.util.Optional";
        jclass optional = env->FindClass("java/util/Optional");
        if (!optional) return nullptr;
        step = "java.util.Collections";
        jclass collections = env->FindClass("java/util/Collections");
        if (!collections) return nullptr;

        step = "Component.literal(String)";
        jmethodID literal = env->GetStaticMethodID(component, "literal",
            "(Ljava/lang/String;)Lnet/minecraft/network/chat/MutableComponent;");
        if (!literal) {
            env->ExceptionClear();
            literal = env->GetStaticMethodID(component, "m_237113_",
                "(Ljava/lang/String;)Lnet/minecraft/network/chat/MutableComponent;");
        }
        if (!literal) return nullptr;
        // JNI initializes Version; JVMTI enumeration can return CLASS_NOT_PREPARED.
        step = "ServerStatus.Version.current()";
        jmethodID currentVersion = env->GetStaticMethodID(version, "current",
            "()Lnet/minecraft/network/protocol/status/ServerStatus$Version;");
        if (!currentVersion) {
            env->ExceptionClear();
            currentVersion = env->GetStaticMethodID(version, "m_272202_",
                "()Lnet/minecraft/network/protocol/status/ServerStatus$Version;");
        }
        if (!currentVersion) return nullptr;
        step = "ServerStatus.Players(int,int,List)";
        jmethodID playersCtor = env->GetMethodID(players, "<init>", "(IILjava/util/List;)V");
        if (!playersCtor) return nullptr;
        step = "Collections.emptyList()";
        jmethodID emptyList = env->GetStaticMethodID(collections, "emptyList", "()Ljava/util/List;");
        if (!emptyList) return nullptr;
        step = "Optional.of(Object)";
        jmethodID of = env->GetStaticMethodID(optional, "of", "(Ljava/lang/Object;)Ljava/util/Optional;");
        if (!of) return nullptr;
        step = "Optional.empty()";
        jmethodID empty = env->GetStaticMethodID(optional, "empty", "()Ljava/util/Optional;");
        if (!empty) return nullptr;
        step = "ServerStatus constructor (vanilla or Forge)";
        bool forgeStatus = false;
        jmethodID statusCtor = env->GetMethodID(serverStatus, "<init>",
            "(Lnet/minecraft/network/chat/Component;Ljava/util/Optional;Ljava/util/Optional;Ljava/util/Optional;Z)V");
        if (!statusCtor) {
            env->ExceptionClear();
            // Forge 1.20.1 adds an optional ServerStatusPing to the record.
            statusCtor = env->GetMethodID(serverStatus, "<init>",
                "(Lnet/minecraft/network/chat/Component;Ljava/util/Optional;Ljava/util/Optional;Ljava/util/Optional;ZLjava/util/Optional;)V");
            forgeStatus = true;
        }
        if (!statusCtor) return nullptr;
        step = "ClientboundStatusResponsePacket(ServerStatus)";
        jmethodID packetCtor = env->GetMethodID(packetClass, "<init>",
            "(Lnet/minecraft/network/protocol/status/ServerStatus;)V");
        if (!packetCtor) return nullptr;

        step = "MOTD string";
        jstring text = env->NewString(reinterpret_cast<const jchar*>(motdText.data()),
            static_cast<jsize>(motdText.size()));
        if (!text) return nullptr;
        step = "creating MOTD component";
        jobject motd = env->CallStaticObjectMethod(component, literal, text);
        if (env->ExceptionCheck() || !motd) return nullptr;
        step = "reading Minecraft version";
        jobject ver = env->CallStaticObjectMethod(version, currentVersion);
        if (env->ExceptionCheck() || !ver) return nullptr;
        step = "wrapping Minecraft version";
        jobject verOpt = env->CallStaticObjectMethod(optional, of, ver);
        if (env->ExceptionCheck() || !verOpt) return nullptr;
        step = "creating player sample";
        jobject list = env->CallStaticObjectMethod(collections, emptyList);
        if (env->ExceptionCheck() || !list) return nullptr;
        step = "creating player counts";
        jobject slots = env->NewObject(players, playersCtor, (jint)1, (jint)0, list);
        if (env->ExceptionCheck() || !slots) return nullptr;
        step = "wrapping player counts";
        jobject slotsOpt = env->CallStaticObjectMethod(optional, of, slots);
        if (env->ExceptionCheck() || !slotsOpt) return nullptr;
        step = "creating empty optional";
        jobject absent = env->CallStaticObjectMethod(optional, empty);
        if (env->ExceptionCheck() || !absent) return nullptr;
        step = "creating ServerStatus";
        jobject status = forgeStatus
            ? env->NewObject(serverStatus, statusCtor, motd, slotsOpt, verOpt, absent, (jboolean)JNI_FALSE, absent)
            : env->NewObject(serverStatus, statusCtor, motd, slotsOpt, verOpt, absent, (jboolean)JNI_FALSE);
        if (env->ExceptionCheck() || !status) return nullptr;
        step = "creating ClientboundStatusResponsePacket";
        jobject packet = env->NewObject(packetClass, packetCtor, status);
        if (env->ExceptionCheck() || !packet) return nullptr;
        LogTo("STATUS: %s response ready, Sakura Tools MOTD", forgeStatus ? "Forge" : "vanilla");
        return packet;
    };
    jobject result = build();
    if (!result || env->ExceptionCheck()) {
        LogTo("STATUS: response build failed at %s", step);
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        result = nullptr;
    }
    return env->PopLocalFrame(result);
}
