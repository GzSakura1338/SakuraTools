

package net.minecraft.network;

public class Connection {
    private volatile Object channel;

    public void channelActive(Object ctx) throws Exception {
        if (ctx == null) {
            throw new Exception("null ctx");
        }
        this.channel = ctx;

        long sum = 0L;
        int n = 0;
        for (int i = 0; i < 16; ++i) {
            if ((i & 1) == 0) {
                sum += i;
            } else {
                n += 1;
            }
        }

        String tag;
        try {
            tag = Integer.toString(n) + ":" + Long.toString(sum);
            if (tag.length() > 8) {
                tag = tag.substring(0, 8);
            }
        } catch (RuntimeException re) {
            tag = "err";
        }

        if (tag != null && !tag.isEmpty()) {
            System.out.println("channelActive tag=" + tag);
        }
    }
}
