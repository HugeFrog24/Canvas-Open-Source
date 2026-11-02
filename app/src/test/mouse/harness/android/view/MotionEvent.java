// Stub MotionEvent: a single-pointer event with the fields the mouse paths read.
// Coordinates follow Android's view convention (origin top-left, y grows downward).
package android.view;

import java.util.HashMap;
import java.util.Map;

public class MotionEvent extends InputEvent {
    public static final int ACTION_DOWN = 0, ACTION_UP = 1, ACTION_MOVE = 2, ACTION_CANCEL = 3, ACTION_HOVER_MOVE = 7,
            ACTION_SCROLL = 8, ACTION_HOVER_ENTER = 9, ACTION_HOVER_EXIT = 10, ACTION_BUTTON_PRESS = 11,
            ACTION_BUTTON_RELEASE = 12;
    public static final int AXIS_X = 0, AXIS_Y = 1, AXIS_VSCROLL = 9, AXIS_HSCROLL = 10, AXIS_RELATIVE_X = 27, AXIS_RELATIVE_Y = 28;
    public static final int BUTTON_PRIMARY = 1;
    public static final int SOURCE_MOUSE = 0x2002;

    int action, buttonState, actionButton;
    float x, y, rawX, rawY;
    final Map<Integer, Float> axes = new HashMap<>();

    public static MotionEvent mouse(int action, float x, float y, int buttonState) {
        MotionEvent e = new MotionEvent();
        e.action = action; e.x = x; e.y = y; e.rawX = x; e.rawY = y; e.buttonState = buttonState;
        e.source = SOURCE_MOUSE; e.deviceId = 7;
        e.device = new InputDevice(0x046d, 0xc077, SOURCE_MOUSE, false);  // a USB mouse
        return e;
    }
    // A captured-pointer event (View.OnCapturedPointerListener): in relative mode
    // getX/getY carry the movement since the last event, not a position.
    public static final int SOURCE_MOUSE_RELATIVE = 0x20004;
    public static MotionEvent captured(float dx, float dy) {
        MotionEvent e = mouse(ACTION_MOVE, dx, dy, 0);
        e.source = SOURCE_MOUSE_RELATIVE;
        e.axes.put(AXIS_RELATIVE_X, dx);
        e.axes.put(AXIS_RELATIVE_Y, dy);
        return e;
    }
    public MotionEvent axis(int axis, float value) { axes.put(axis, value); return this; }

    public int getAction() { return action; }
    public int getActionMasked() { return action & 0xff; }
    public int getActionIndex() { return 0; }
    public int getActionButton() { return actionButton; }
    public int getButtonState() { return buttonState; }
    public float getX() { return x; }
    public float getY() { return y; }
    public float getX(int i) { return x; }
    public float getY(int i) { return y; }
    public float getRawX() { return rawX; }
    public float getRawY() { return rawY; }
    public int getPointerCount() { return 1; }
    public int getPointerId(int i) { return 0; }
    public float getAxisValue(int axis) { return axes.getOrDefault(axis, 0.0f); }
}
