// Feeds the same synthetic hardware-mouse events to stock Sky's GameActivity
// mouse methods, Canvas HEAD's and (if generated) Canvas 1.7.2's, all verbatim
// (see run_mouse.sh), and compares the native calls they make. Landscape window
// 2400 x 1080; view coordinates, so "down the screen" is increasing y.
package harness;

import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.List;
import java.util.function.Supplier;

public class Main {
    interface Scenario { void run(MouseBase g); }

    record Side(String name, Supplier<MouseBase> make) {}

    static final List<Side> SIDES = new ArrayList<>();

    static List<String> run(MouseBase g, Scenario s) {
        g.calls.clear();
        s.run(g);
        return List.copyOf(g.calls);
    }

    // One row per call index; the first side (stock) is the reference.
    static int compare(String name, Scenario s) {
        List<List<String>> results = new ArrayList<>();
        int rows = 0;
        for (Side side : SIDES) {
            List<String> calls = run(side.make().get(), s);
            results.add(calls);
            rows = Math.max(rows, calls.size());
        }
        System.out.println("== " + name);
        StringBuilder header = new StringBuilder("           ");
        for (Side side : SIDES) header.append(String.format("%-42s", side.name()));
        System.out.println(header);
        int diffs = 0;
        for (int i = 0; i < rows; i++) {
            StringBuilder row = new StringBuilder();
            String reference = i < results.get(0).size() ? results.get(0).get(i) : "-";
            boolean differs = false;
            for (List<String> calls : results) {
                String c = i < calls.size() ? calls.get(i) : "-";
                differs |= !c.equals(reference);
                row.append(String.format("%-42s", c));
            }
            diffs += differs ? 1 : 0;
            System.out.println((differs ? "  DIFFERS  " : "  same     ") + row);
        }
        return diffs;
    }

    static void optional(String cls, String name) {
        try {
            Class<?> c = Class.forName(cls);
            SIDES.add(new Side(name, () -> {
                try { return (MouseBase) c.getDeclaredConstructor().newInstance(); }
                catch (ReflectiveOperationException e) { throw new IllegalStateException(e); }
            }));
        } catch (ClassNotFoundException e) {
            System.out.println("(no " + name + " side)");
        }
    }

    public static void main(String[] args) throws Exception {
        SIDES.add(new Side("stock 0.34.5", StockMouse::new));
        SIDES.add(new Side("Canvas HEAD", CanvasMouse::new));
        optional("harness.Stock0351Mouse", "stock 0.35.1");
        try {
            Class<?> old = Class.forName("harness.Canvas172Mouse");
            SIDES.add(new Side("Canvas 1.7.2 (before #45)", () -> {
                try { return (MouseBase) old.getDeclaredConstructor().newInstance(); }
                catch (ReflectiveOperationException e) { throw new IllegalStateException(e); }
            }));
        } catch (ClassNotFoundException e) {
            System.out.println("(no Canvas 1.7.2 side: out/canvas-1.7.2/GameActivity.java not generated)");
        }

        int diffs = 0;
        diffs += compare("hover: pointer moves down the screen, y 300 -> 700", g -> {
            g.onGenericMotionEvent(MotionEvent.mouse(MotionEvent.ACTION_HOVER_MOVE, 1200, 300, 0));
            g.onGenericMotionEvent(MotionEvent.mouse(MotionEvent.ACTION_HOVER_MOVE, 1200, 700, 0));
        });
        diffs += compare("primary-button drag: hover to y 500, press, move down to 600, then up to 400, release", g -> {
            // A real mouse hovers to the press point first, which sets the delta anchor.
            g.onGenericMotionEvent(MotionEvent.mouse(MotionEvent.ACTION_HOVER_MOVE, 1200, 500, 0));
            g.dispatchTouchEvent(MotionEvent.mouse(MotionEvent.ACTION_DOWN, 1200, 500, MotionEvent.BUTTON_PRIMARY));
            g.dispatchTouchEvent(MotionEvent.mouse(MotionEvent.ACTION_MOVE, 1200, 600, MotionEvent.BUTTON_PRIMARY));
            g.dispatchTouchEvent(MotionEvent.mouse(MotionEvent.ACTION_MOVE, 1200, 400, MotionEvent.BUTTON_PRIMARY));
            g.dispatchTouchEvent(MotionEvent.mouse(MotionEvent.ACTION_UP, 1200, 400, 0));
        });
        diffs += compare("wheel: one notch with AXIS_VSCROLL = +1", g ->
            g.onGenericMotionEvent(MotionEvent.mouse(MotionEvent.ACTION_SCROLL, 1200, 400, 0)
                .axis(MotionEvent.AXIS_VSCROLL, 1.0f)));
        diffs += compare("captured pointer (relative mode): mouse moves down 10, then up 10 (relative y > 0 = down, assumed)",
            g -> {
                g.onCapturedPointer(g.getBridgeView(), MotionEvent.captured(0, 10));
                g.onCapturedPointer(g.getBridgeView(), MotionEvent.captured(0, -10));
            });
        System.out.println("rows that differ from stock: " + diffs);
    }
}
