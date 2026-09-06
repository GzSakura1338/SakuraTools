public class MappedMethodTest {
    private static native int coldStatus(Class<?> type);
    private static native boolean resolve(Class<?> type, String name, String forge, String descriptor, boolean isStatic);

    public static class Delayed {
        public Object m_104910_() { return this; }
        public static Delayed current() { return new Delayed(); }
    }
    public static class Inherited extends Delayed { }
    public static class Broken {
        static { fail(); }
        private static void fail() { throw new IllegalStateException("initializer failed"); }
        public Object getConnection() { return this; }
    }
    static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    static void binding(String type, String name, String forge, String descriptor) throws Exception {
        Class<?> c = Class.forName(type, false, MappedMethodTest.class.getClassLoader());
        int cold = coldStatus(c);
        check(resolve(c, name, forge, descriptor, false), "Missing " + type + "." + name);
        System.out.println("PASS: " + type + "." + name + " (initial JVMTI status=" + cold + ")");
    }
    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        Class<?> delayed = Class.forName("MappedMethodTest$Delayed", false, MappedMethodTest.class.getClassLoader());
        check(coldStatus(delayed) == 22, "expected JVMTI CLASS_NOT_PREPARED");
        check(resolve(delayed, "getConnection", "m_104910_", "()Ljava/lang/Object;", false), "cold Forge fallback failed");
        check(coldStatus(delayed) == 0, "class still unprepared");
        check(resolve(delayed, "current", "unused", "()LMappedMethodTest$Delayed;", true), "static lookup failed");
        check(resolve(Inherited.class, "getConnection", "m_104910_", "()Ljava/lang/Object;", false), "inherited lookup failed");
        for (int i = 0; i < 100; ++i) {
            try {
                resolve(delayed, "absent", "alsoAbsent", "()V", false);
                throw new AssertionError("missing method silently accepted");
            } catch (NoSuchMethodError expected) { }
            check(resolve(delayed, "getConnection", "m_104910_", "()Ljava/lang/Object;", false), "retry failed");
        }
        try {
            Class<?> broken = Class.forName("MappedMethodTest$Broken", false, MappedMethodTest.class.getClassLoader());
            resolve(broken, "getConnection", "m_104910_", "()Ljava/lang/Object;", false);
            throw new AssertionError("initializer failure lost");
        } catch (ExceptionInInitializerError expected) {
            check(expected.getCause() instanceof IllegalStateException, "wrong initializer cause");
        }
        System.out.println("PASS: cold-class preparation, Forge fallback, inherited/static lookup, 100 retries, exception preservation");
        if (args.length > 1) {
            binding("net.minecraft.client.multiplayer.ClientPacketListener", "getConnection", "m_104910_", "()Lnet/minecraft/network/Connection;");
            binding("net.minecraft.client.multiplayer.ClientPacketListener", "levels", "m_105151_", "()Ljava/util/Set;");
            binding("net.minecraft.client.multiplayer.ClientPacketListener", "registryAccess", "m_105152_", "()Lnet/minecraft/core/RegistryAccess;");
            binding("net.minecraft.core.RegistryAccess", "freeze", "m_203557_", "()Lnet/minecraft/core/RegistryAccess$Frozen;");
            binding("net.minecraft.client.multiplayer.MultiPlayerGameMode", "getPlayerMode", "m_105295_", "()Lnet/minecraft/world/level/GameType;");
            // A live client has already bootstrapped registries before entering a world.
            Class.forName("net.minecraft.SharedConstants").getMethod("m_142977_").invoke(null);
            Class.forName("net.minecraft.server.Bootstrap").getMethod("m_135870_").invoke(null);
            binding("net.minecraft.world.level.Level", "dimension", "m_46472_", "()Lnet/minecraft/resources/ResourceKey;");
            binding("net.minecraft.world.level.Level", "dimensionTypeId", "m_220362_", "()Lnet/minecraft/resources/ResourceKey;");
        }
    }
}
