#include "snapshot_jni.h"
#include "login_handoff.h"
#include <cstdio>
#include <cmath>

jclass LoadClassInLoader(JNIEnv* env, jobject loader, const char* name) {
    jclass type = env->GetObjectClass(loader);
    jmethodID load = env->GetMethodID(type, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    jstring text = env->NewStringUTF(name);
    auto result = static_cast<jclass>(env->CallObjectMethod(loader, load, text));
    env->DeleteLocalRef(text);
    env->DeleteLocalRef(type);
    return result;
}

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: snapshot_jni_test <jvm.dll> <Minecraft classpath>\n");
        return 2;
    }
    HMODULE library = LoadLibraryA(argv[1]);
    auto create = library ? reinterpret_cast<jint(JNICALL*)(JavaVM**, void**, void*)>(
        GetProcAddress(library, "JNI_CreateJavaVM")) : nullptr;
    if (!create) return 2;
    JavaVM* vm = nullptr;
    JNIEnv* env = nullptr;
    std::string cp = std::string("-Djava.class.path=") + argv[2];
    JavaVMOption options[] = {{cp.data(), nullptr}, {const_cast<char*>("-Xcheck:jni"), nullptr}};
    JavaVMInitArgs args{JNI_VERSION_1_8, 2, options, JNI_FALSE};
    if (create(&vm, reinterpret_cast<void**>(&env), &args) != JNI_OK) return 2;
    int result = 0;
    try {
        SnapshotLocalFrame frame(env);
        jclass cl = env->FindClass("java/lang/ClassLoader");
        jmethodID system = env->GetStaticMethodID(cl, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
        jobject loader = env->CallStaticObjectMethod(cl, system);
        if (env->ExceptionCheck()) throw std::runtime_error("system loader unavailable");
        SnapshotJni j(env, loader);
        auto boot = [&](const char* owner, const char* name, const char* mapped) {
            auto mid = j.method(owner, name, mapped, "()V", true);
            env->CallStaticVoidMethod(j.type(owner), mid);
            j.check();
        };
        boot("net.minecraft.SharedConstants", "tryDetectVersion", "m_142977_");
        boot("net.minecraft.server.Bootstrap", "bootStrap", "m_135870_");
        auto roundTrip = [&](const char* owner, jobject packet) {
            jobject bytes = j.stat("io.netty.buffer.Unpooled", "buffer", "buffer", "()Lio/netty/buffer/ByteBuf;");
            jobject buffer = j.make("net.minecraft.network.FriendlyByteBuf", "(Lio/netty/buffer/ByteBuf;)V", bytes);
            auto write = j.method(owner, "write", "m_5779_", "(Lnet/minecraft/network/FriendlyByteBuf;)V");
            env->CallVoidMethod(packet, write, buffer);
            j.check();
            jobject decoded = j.make(owner, "(Lnet/minecraft/network/FriendlyByteBuf;)V", buffer);
            require(j.integer(bytes, "io.netty.buffer.ByteBuf", "readableBytes", "readableBytes", "()I") == 0,
                "decoder did not consume the entire packet");
            j.boolean(bytes, "io.netty.buffer.ByteBuf", "release", "release", "()Z");
            return decoded;
        };
        const char* position = "net.minecraft.network.protocol.game.ClientboundPlayerPositionPacket";
        jobject empty = j.stat("java.util.Collections", "emptySet", "emptySet", "()Ljava/util/Set;");
        jobject packet = j.make(position, "(DDDFFLjava/util/Set;I)V", 123.25, 70.5, -456.75,
            (jfloat)90, (jfloat)-20, empty, (jint)LoginHandoff::teleportId);
        jobject decoded = roundTrip(position, packet);
        require(j.integer(decoded, position, "getId", "m_132825_", "()I") == LoginHandoff::teleportId,
            "local teleport id changed during encoding");
        require(std::abs(j.real(decoded, position, "getX", "m_132818_", "()D") - 123.25) < 0.001,
            "snapshot position changed during encoding");
        const char* spawn = "net.minecraft.network.protocol.game.ClientboundSetDefaultSpawnPositionPacket";
        jobject pos = j.make("net.minecraft.core.BlockPos", "(III)V", (jint)123, (jint)70, (jint)-457);
        roundTrip(spawn, j.make(spawn, "(Lnet/minecraft/core/BlockPos;F)V", pos, (jfloat)90));
        const char* time = "net.minecraft.network.protocol.game.ClientboundSetTimePacket";
        roundTrip(time, j.make(time, "(JJZ)V", (jlong)10000, (jlong)2000, JNI_TRUE));
        const char* health = "net.minecraft.network.protocol.game.ClientboundSetHealthPacket";
        roundTrip(health, j.make(health, "(FIF)V", (jfloat)17, (jint)18, (jfloat)2));
        const char* info = "net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket";
        jobject actions = j.stat("java.util.EnumSet", "allOf", "allOf", "(Ljava/lang/Class;)Ljava/util/EnumSet;",
            j.type("net.minecraft.network.protocol.game.ClientboundPlayerInfoUpdatePacket$Action"));
        jobject list = j.stat("java.util.Collections", "emptyList", "emptyList", "()Ljava/util/List;");
        roundTrip(info, j.make(info, "(Ljava/util/EnumSet;Ljava/util/Collection;)V", actions, list));
        j.field(info, "entries", "f_244436_", "Ljava/util/List;");
        j.field("net.minecraft.client.multiplayer.ClientChunkCache", "storage", "f_104410_",
            "Lnet/minecraft/client/multiplayer/ClientChunkCache$Storage;");
        const char* storage = "net.minecraft.client.multiplayer.ClientChunkCache$Storage";
        j.field(storage, "chunks", "f_104466_", "Ljava/util/concurrent/atomic/AtomicReferenceArray;");
        j.field(storage, "chunkRadius", "f_104467_", "I");
        j.field(storage, "viewCenterX", "f_104469_", "I");
        j.field(storage, "viewCenterZ", "f_104470_", "I");
        j.field("net.minecraft.world.entity.player.Player", "inventoryMenu", "f_36095_",
            "Lnet/minecraft/world/inventory/InventoryMenu;");
        j.method("net.minecraft.world.inventory.AbstractContainerMenu", "getItems", "m_38927_",
            "()Lnet/minecraft/core/NonNullList;");
        j.method("net.minecraft.network.protocol.game.ClientboundContainerSetContentPacket", "<init>", "<init>",
            "(IILnet/minecraft/core/NonNullList;Lnet/minecraft/world/item/ItemStack;)V");
        j.method("net.minecraft.network.protocol.game.ClientboundLevelChunkWithLightPacket", "<init>", "<init>",
            "(Lnet/minecraft/world/level/chunk/LevelChunk;Lnet/minecraft/world/level/lighting/LevelLightEngine;Ljava/util/BitSet;Ljava/util/BitSet;)V");
        j.method("io.netty.channel.ChannelFuture", "addListener", "addListener",
            "(Lio/netty/util/concurrent/GenericFutureListener;)Lio/netty/channel/ChannelFuture;");
        std::puts("PASS: native JNI mappings and five Minecraft packet codec round trips");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        result = 1;
    }
    vm->DestroyJavaVM();
    return result;
}
