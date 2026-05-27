package app;

import java.net.Socket;
import java.io.InputStream;

public class App {
    public static String fetch(String url) {
        try {
            Socket sock = new Socket("example.com", 80);
            InputStream in = sock.getInputStream();
            byte[] buf = new byte[1024];
            int n = in.read(buf);
            sock.close();
            return new String(buf, 0, n);
        } catch (Exception e) {
            return "";
        }
    }
}
