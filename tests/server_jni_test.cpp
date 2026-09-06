#include "server/internal.h"
#include "snapshot_jni.h"
#include <cstdarg>
#include <stdexcept>

JavaVM* g_vm = nullptr;
jvmtiEnv* g_jvmti = nullptr;
static std::string diagnostics;
void LogTo(const char* format, ...) {
    char text[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    diagnostics += std::string(text) + '\n';
}
void LogAndClearException(JNIEnv* env, const char* where) {
    if (env->ExceptionCheck()) {
        LogTo("JNI exception: %s", where);
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}
jclass LoadClassInLoader(JNIEnv* env, jobject loader, const char* name) {
    jclass type = env->GetObjectClass(loader);
    auto load = env->GetMethodID(type, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring value = env->NewStringUTF(name);
    auto result = static_cast<jclass>(env->CallObjectMethod(loader, load, value));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = nullptr;
    }
    env->DeleteLocalRef(value);
    env->DeleteLocalRef(type);
    return result;
}
// The harness uses real packet/Netty classes but has no running render thread.
bool InstallWorldSnapshot(JNIEnv*, jobject) {
    return false;
}
static void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    HMODULE library = LoadLibraryA(argv[1]);
    auto create =
        library ? reinterpret_cast<jint(JNICALL*)(JavaVM**, void**, void*)>(GetProcAddress(library, "JNI_CreateJavaVM"))
                : nullptr;
    if (!create)
        return 2;
    JNIEnv* env = nullptr;
    std::string cp = std::string("-Djava.class.path=") + argv[2];
    JavaVMOption options[] = {{cp.data(), nullptr}, {const_cast<char*>("-Xcheck:jni"), nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_8, 2, options, JNI_FALSE};
    if (create(&g_vm, reinterpret_cast<void**>(&env), &args) != JNI_OK)
        return 2;
    g_vm->GetEnv(reinterpret_cast<void**>(&g_jvmti), JVMTI_VERSION_1_2);
    int result = 0;
    try {
        using namespace proxy_server;
        SnapshotLocalFrame frame(env);
        jclass cl = env->FindClass("java/lang/ClassLoader");
        auto system = env->GetStaticMethodID(cl, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = env->CallStaticObjectMethod(cl, system);
        require(!env->ExceptionCheck() && loader, "system loader unavailable");
        SnapshotJni j(env, loader);
        for (const auto& boot : {std::pair{"net.minecraft.SharedConstants", "tryDetectVersion"},
                                 std::pair{"net.minecraft.server.Bootstrap", "bootStrap"}}) {
            auto method = j.method(boot.first, boot.second,
                                   std::string(boot.second) == "bootStrap" ? "m_135870_" : "m_142977_", "()V", true);
            env->CallStaticVoidMethod(j.type(boot.first), method);
            j.check();
        }
        require(cacheJavaRefs(env, loader), "production binding initialization failed");
        auto uuidFactory = server.refs.uuidNameUuidFromBytesMid;
        server.refs.uuidNameUuidFromBytesMid = nullptr;
        require(!validateRequiredBindings(), "missing UUID binding accepted");
        require(diagnostics.find("login.uuidNameUuidFromBytesMid") != std::string::npos,
                "missing binding was not named");
        server.refs.uuidNameUuidFromBytesMid = uuidFactory;
        require(validateRequiredBindings(), "restored binding rejected");
        jclass strings = j.type("java.lang.String");
        j.method("java.lang.String", "length", "length", "()I");
        require(findMethodByDescriptor(strings, "()I", false) == nullptr, "ambiguous descriptor accepted");

        auto uuid = [&](jlong value) { return j.make("java.util.UUID", "(JJ)V", (jlong)0, value); };
        server.aPlayer.uuid = uuid(1);
        server.aPlayer.name = env->NewStringUTF("A");
        server.aPlayer.ready = true;
        require(initializeBIdentity(env, uuid(2), env->NewStringUTF("B")), "B login identity initialization failed");
        require(server.bPlayer.ready && server.bPlayer.uuid && server.bPlayer.name, "B snapshot identity incomplete");
        auto list = [&](std::initializer_list<jobject> objects) {
            jobject value = j.make("java.util.ArrayList", "()V");
            for (jobject object : objects)
                j.boolean(value, "java.util.ArrayList", "add", "add", "(Ljava/lang/Object;)Z", object);
            return value;
        };
        auto channel = [&] {
            jobjectArray handlers = env->NewObjectArray(0, j.type("io.netty.channel.ChannelHandler"), nullptr);
            return j.make("io.netty.channel.embedded.EmbeddedChannel", "([Lio/netty/channel/ChannelHandler;)V",
                          handlers);
        };
        auto read = [&](jobject ch) {
            return j.object(ch, "io.netty.channel.embedded.EmbeddedChannel", "readOutbound", "readOutbound",
                            "()Ljava/lang/Object;");
        };
        auto invokeVoid = [&](jobject obj, const char* owner, const char* method, const char* desc, auto... values) {
            auto mid = j.method(owner, method, method, desc);
            env->CallVoidMethod(obj, mid, values...);
            j.check();
        };
        const char* teamType = "net.minecraft.network.protocol.game.ClientboundSetPlayerTeamPacket";
        jstring teamName = env->NewStringUTF("red");
        jobject scoreboard = j.make("net.minecraft.world.scores.Scoreboard", "()V");
        jobject team = j.make("net.minecraft.world.scores.PlayerTeam",
                              "(Lnet/minecraft/world/scores/Scoreboard;Ljava/lang/String;)V", scoreboard, teamName);
        jobject parameters = j.make("net.minecraft.network.protocol.game.ClientboundSetPlayerTeamPacket$Parameters",
                                    "(Lnet/minecraft/world/scores/PlayerTeam;)V", team);
        jobject present =
            j.stat("java.util.Optional", "of", "of", "(Ljava/lang/Object;)Ljava/util/Optional;", parameters);
        jobject empty = j.stat("java.util.Optional", "empty", "empty", "()Ljava/util/Optional;");
        auto teamPacket = [&](int operation) {
            return j.make(teamType, "(Ljava/lang/String;ILjava/util/Optional;Ljava/util/Collection;)V", teamName,
                          (jint)operation, operation == 0 ? present : empty, list({server.aPlayer.name}));
        };
        auto memberCount = [&](jobject packet) {
            jobject members = env->GetObjectField(packet, server.refs.setPlayerTeamPlayersFid);
            return j.integer(members, "java.util.Collection", "size", "size", "()I");
        };
        jobject ch = channel();
        jobject initial = teamPacket(0);
        require(sendGamePacket(env, ch, initial, false), "snapshot submission failed");
        j.object(ch, "io.netty.channel.embedded.EmbeddedChannel", "flushOutbound", "flushOutbound",
                 "()Lio/netty/channel/embedded/EmbeddedChannel;");
        require(memberCount(read(ch)) == 2, "snapshot did not map B");
        require(memberCount(initial) == 1, "A's original packet was mutated");
        require(sendGamePacket(env, ch, teamPacket(4)), "live removal failed");
        require(memberCount(read(ch)) == 2, "live removal lost B");
        require(sendGamePacket(env, ch, teamPacket(4)) && !read(ch), "duplicate removal was sent");
        require(sendGamePacket(env, ch, teamPacket(0)), "team restoration failed");
        read(ch);
        require(initializeBIdentity(env, uuid(3), env->NewStringUTF("B")), "B reconnect identity failed");
        require(sendGamePacket(env, ch, teamPacket(4)) && !read(ch), "reconnect retained old memberships");
        closeBChannel(env, ch);
        server.teams = TeamState{};
        require(!sendGamePacket(env, ch, teamPacket(0)), "closed channel accepted snapshot");
        ch = channel();
        require(sendGamePacket(env, ch, teamPacket(4)) && !read(ch), "failed snapshot committed team state");

        const char* info = "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket";
        jclass modeClass = j.type("net.minecraft.world.level.GameType");
        auto creative = env->GetStaticFieldID(modeClass, "CREATIVE", "Lnet/minecraft/world/level/GameType;");
        j.check();
        jobject mode = env->GetStaticObjectField(modeClass, creative);
        jobject entry = j.make(
            "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Entry",
            "(Ljava/util/UUID;Lcom/mojang/authlib/GameProfile;ZILnet/minecraft/world/level/GameType;Lnet/minecraft/"
            "network/chat/Component;Lnet/minecraft/network/chat/RemoteChatSession$Data;)V",
            server.aPlayer.uuid, (jobject) nullptr, JNI_TRUE, (jint)77, mode, (jobject) nullptr, (jobject) nullptr);
        jobject actions =
            j.stat("java.util.EnumSet", "noneOf", "noneOf", "(Ljava/lang/Class;)Ljava/util/EnumSet;",
                   j.type("net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Action"));
        for (const char* name : {"UPDATE_GAME_MODE", "UPDATE_LISTED", "UPDATE_LATENCY", "UPDATE_DISPLAY_NAME"}) {
            auto cls = j.type("net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Action");
            auto field = env->GetStaticFieldID(
                cls, name, "Lnet/minecraft/network/protocol/game/ClientboundPlayerInfoUpdatePacket$Action;");
            j.check();
            j.boolean(actions, "java.util.EnumSet", "add", "add", "(Ljava/lang/Object;)Z",
                      env->GetStaticObjectField(cls, field));
        }
        jobject packet = j.make(info, "(Ljava/util/EnumSet;Ljava/util/Collection;)V", actions, list({}));
        env->SetObjectField(packet, server.refs.piuEntriesField, list({entry}));
        j.check();
        for (int missing = 0; missing < 4; ++missing) {
            auto saved = server.refs;
            if (missing == 1)
                server.refs.gameTypeGetIdMid = nullptr;
            if (missing == 2)
                server.refs.piEntryLatencyMid = nullptr;
            if (missing == 3)
                server.refs.fbbWriteComponentMid = nullptr;
            jobject mirror = nullptr;
            require(buildPlayerInfoMirror(env, packet, mirror) && mirror, "production player-info conversion failed");
            server.refs = saved;
            jobject bytes = j.stat("io.netty.buffer.Unpooled", "buffer", "buffer", "()Lio/netty/buffer/ByteBuf;");
            jobject buffer = j.make("net.minecraft.network.FriendlyByteBuf", "(Lio/netty/buffer/ByteBuf;)V", bytes);
            env->CallVoidMethod(mirror, server.refs.playerInfoUpdatePacketWriteMid, buffer);
            j.check();
            jint bits = env->CallByteMethod(bytes, server.refs.byteBufGetByteMid, (jint)0);
            j.check();
            require(bits == (0x3c & ~(missing == 1   ? 4
                                      : missing == 2 ? 16
                                      : missing == 3 ? 32
                                                     : 0)),
                    "unavailable field advertised");
            jobject decoded = j.make(info, "(Lnet/minecraft/network/FriendlyByteBuf;)V", buffer);
            require(j.integer(bytes, "io.netty.buffer.ByteBuf", "readableBytes", "readableBytes", "()I") == 0,
                    "malformed player-info payload");
            require(decoded != nullptr, "mirror codec failed");
            jobject decodedEntries = env->GetObjectField(decoded, server.refs.piuEntriesField);
            jobject decodedEntry = env->CallObjectMethod(decodedEntries, server.refs.listGetMid, (jint)0);
            j.check();
            jobject decodedUuid = env->CallObjectMethod(decodedEntry, server.refs.piEntryProfileIdMid);
            j.check();
            require(j.boolean(decodedUuid, "java.util.UUID", "equals", "equals", "(Ljava/lang/Object;)Z",
                              server.bPlayer.uuid),
                    "mirror retained A's UUID instead of B's");
            const char* entryType = "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Entry";
            require(!j.boolean(decodedEntry, entryType, "listed", "f_243700_", "()Z"),
                    "B became a duplicate TAB entry");
            if (missing != 2)
                require(j.integer(decodedEntry, entryType, "latency", "f_244322_", "()I") == 77, "latency value lost");
            j.boolean(bytes, "io.netty.buffer.ByteBuf", "release", "release", "()Z");
        }
        // Fail an accepted, unflushed Netty promise while its channel is still open.
        require(writePacket(env, ch, initial, false), "deferred write rejected");
        jobject unsafe =
            j.object(ch, "io.netty.channel.Channel", "unsafe", "unsafe", "()Lio/netty/channel/Channel$Unsafe;");
        jobject pending = j.object(unsafe, "io.netty.channel.Channel$Unsafe", "outboundBuffer", "outboundBuffer",
                                   "()Lio/netty/channel/ChannelOutboundBuffer;");
        invokeVoid(pending, "io.netty.channel.ChannelOutboundBuffer", "addFlush", "()V");
        jobject failure = j.make("java.io.IOException", "()V");
        j.boolean(pending, "io.netty.channel.ChannelOutboundBuffer", "remove", "remove", "(Ljava/lang/Throwable;)Z",
                  failure);
        invokeVoid(ch, "io.netty.channel.embedded.EmbeddedChannel", "runPendingTasks", "()V");
        require(!j.boolean(ch, "io.netty.channel.Channel", "isOpen", "isOpen", "()Z"),
                "asynchronous send failure did not close B");
        std::puts(
            "PASS: production bindings, snapshot/live team path, send rollback, player-info codec and async failure");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n%s", error.what(), diagnostics.c_str());
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
        }
        result = 1;
    }
    g_vm->DestroyJavaVM();
    return result;
}
