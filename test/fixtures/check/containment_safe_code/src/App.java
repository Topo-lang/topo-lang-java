package app;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.stream.IntStream;

public class App {
    public static int sort(int count) {
        List<Integer> data = new ArrayList<>();
        for (int i = count; i > 0; i--) {
            data.add(i);
        }
        Collections.sort(data);
        return data.get(0);
    }

    public static int sum(int n) {
        return IntStream.rangeClosed(1, n).sum();
    }
}
