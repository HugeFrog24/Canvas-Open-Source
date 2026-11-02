// Everything GameActivity's mouse methods use that run_mouse.sh does not
// extract: fields, the methods they call, and the natives, which record their
// arguments. CanvasMouse and StockMouse (generated) extend this with verbatim
// methods from Canvas's GameActivity.java and from stock Sky's, as decompiled.
package harness;

import android.graphics.PointF;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.widget.RelativeLayout;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public abstract class MouseBase {
    // A landscape phone window: 2400 x 1080, origin top-left.
    static final int WIDTH = 2400, HEIGHT = 1080;
    // jadx's name for the literal `true` in the decompiled stock code.
    static final boolean ENABLE_DISPLAY_CUTOUT_MODE = true;

    final List<String> calls = new ArrayList<>();
    private final RelativeLayout bridge = new RelativeLayout(WIDTH, HEIGHT);
    private final Window window = new Window(new View(WIDTH, HEIGHT));

    boolean m_motionEventsDisabled;
    PointF m_lastMouseLocation = new PointF();
    float[] m_gameInputOffset = new float[2];
    int[] m_gameViewLocation = new int[2];
    int[] m_imguiViewLocation = new int[2];
    View gameView;   // null: no offset, as when the game view sits at the window origin
    View imguiView;  // null: the overlay gets view coordinates
    Set<Integer> mGameControllerIds = new HashSet<>();
    boolean m_lTriggerPressed, m_rTriggerPressed;
    int m_lastDpadDirection = 23;

    public Window getWindow() { return window; }
    public RelativeLayout getBridgeView() { return bridge; }
    public boolean dispatchTouchEvent(MotionEvent e) { return false; }      // super
    public boolean onGenericMotionEvent(MotionEvent e) { return false; }    // super
    public boolean onCapturedPointer(View v, MotionEvent e) { return true; }
    boolean isEventInTextField(MotionEvent e) { return false; }
    void setPointerCapture(boolean capture) {}
    void onGamepadConnected(int vendorId, int productId) {}
    boolean onButtonPress(int keyCode, int inputSource, boolean pressed, int vendorId, int productId) {
        calls.add("onButtonPress(" + keyCode + ", " + inputSource + ", " + pressed + ")");
        return true;
    }

    public void onMouseMovedNative(int x, int y) { calls.add("onMouseMovedNative(" + x + ", " + y + ")"); }
    public void onMouseDeltaNative(int x, int y) { calls.add("onMouseDeltaNative(" + x + ", " + y + ")"); }
    public void onMouseScrollingDeltaNative(float x, float y) { calls.add("onMouseScrollingDeltaNative(" + x + ", " + y + ")"); }
    public void setGamepadProductTypeNative(int vendorId, int productId) {}
    public void onStickEventNative(float lx, float ly, float rx, float ry) {}
    void onStickEvent(float lx, float ly, float rx, float ry, int vendorId, int productId) {}  // stock helper
    public static boolean onTouchNative(int id, int action, float x, float y) { return true; }
}
