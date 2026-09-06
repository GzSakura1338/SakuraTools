import java.lang.reflect.*;
import java.util.*;

public class StatusCodecSmoke {
    private static native int probeColdVersion(ClassLoader loader);
    private static native Object buildNative(ClassLoader loader, String name, String address);

    private static String motdText(Object packet) throws Exception {
        Object status = packet.getClass().getMethod("f_134886_").invoke(packet);
        Object motd = status.getClass().getMethod("f_134900_").invoke(status);
        return (String)motd.getClass().getMethod("getString").invoke(motd);
    }

    public static void main(String[] args) throws Exception {
        System.load(args[1]);
        ClassLoader loader = StatusCodecSmoke.class.getClassLoader();
        int coldError = probeColdVersion(loader);
        if (coldError != 22) throw new AssertionError("Expected CLASS_NOT_PREPARED (22), got " + coldError);
        System.out.println("PASS: reproduced original JVMTI CLASS_NOT_PREPARED failure");
        Class.forName("net.minecraft.SharedConstants").getMethod("m_142977_").invoke(null);
        Object packet = buildNative(loader, "\u5e03\u5409\u5c9b", "mc.example.test");
        if (packet == null) throw new AssertionError("Native status builder failed");
        Class<?> statusClass = Class.forName("net.minecraft.network.protocol.status.ServerStatus");
        boolean forge = Arrays.stream(statusClass.getRecordComponents()).anyMatch(c -> c.getName().equals("forgeData"));
        if (!args[0].equals(forge ? "forge" : "vanilla"))
            throw new AssertionError("Unexpected ServerStatus variant");
        Class<?> packetClass = Class.forName("net.minecraft.network.protocol.status.ClientboundStatusResponsePacket");
        Object status = packetClass.getMethod("f_134886_").invoke(packet);
        Object motd = statusClass.getMethod("f_134900_").invoke(status);
        Object ver = ((Optional<?>)statusClass.getMethod("f_134902_").invoke(status)).orElseThrow();
        String expectedMotd = "\u00a7d\u00a7lSakura Tools \u00a7r\u00a78\u2192 \u00a7f\u5e03\u5409\u5c9b\n\u00a77mc.example.test\u00a7r";
        if (!expectedMotd.equals(motd.getClass().getMethod("getString").invoke(motd))
                || (int)ver.getClass().getMethod("f_134963_").invoke(ver) != 763)
            throw new AssertionError("Incorrect MOTD or detected protocol");
        Class<?> byteBuf = Class.forName("io.netty.buffer.ByteBuf");
        Object raw = Class.forName("io.netty.buffer.Unpooled").getMethod("buffer").invoke(null);
        Class<?> friendly = Class.forName("net.minecraft.network.FriendlyByteBuf");
        Object buf = friendly.getConstructor(byteBuf).newInstance(raw);
        try {
            packetClass.getMethod("m_5779_", friendly).invoke(packet, buf);
            int before = (int)byteBuf.getMethod("readableBytes").invoke(buf);
            Object decoded = packetClass.getConstructor(friendly).newInstance(buf);
            if (!packet.equals(decoded) || (int)byteBuf.getMethod("readableBytes").invoke(buf) != 0)
                throw new AssertionError("Status codec round-trip failed");
            System.out.println("PASS: " + (forge ? "Forge" : "vanilla") + " 1.20.1 styled Unicode MOTD round-trip, " + before + " bytes, protocol 763");
        } finally {
            byteBuf.getMethod("release").invoke(buf);
        }
        ClassLoader missingClass = new ClassLoader(loader) {
            @Override public Class<?> loadClass(String name) throws ClassNotFoundException {
                if (name.endsWith("ServerStatus$Version")) throw new ClassNotFoundException(name);
                return super.loadClass(name);
            }
        };
        String waiting = motdText(buildNative(loader, "", ""));
        if (!waiting.contains("\u5f85\u8fde\u63a5") || !waiting.contains("\u7b49\u5f85 A"))
            throw new AssertionError("Missing waiting state");
        String switched = motdText(buildNative(loader, "Creative", "creative.test"));
        if (!switched.contains("Creative") || !switched.contains("creative.test") || switched.contains("mc.example.test"))
            throw new AssertionError("Stale target server");
        String clipped = motdText(buildNative(loader, "\u00a7k\u670d\u52a1\u5668\n".repeat(40), "x".repeat(200)));
        if (clipped.contains("\u00a7k") || clipped.lines().count() != 2 || !clipped.contains("\u2026"))
            throw new AssertionError("Invalid line formatting or truncation");
        System.out.println("PASS: waiting state, target changes, and long-name formatting");
        if (buildNative(missingClass, "", "") != null) throw new AssertionError("Expected missing-class failure");
        System.out.println("PASS: missing class returns null without a pending JNI exception");
    }
}
