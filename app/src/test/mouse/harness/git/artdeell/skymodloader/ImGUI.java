// The overlay bridge: wantsMouse() is false, as for a pointer outside every
// overlay window, so the game-facing paths run.
package git.artdeell.skymodloader;

public class ImGUI {
    public static void submitPositionEvent(float x, float y) {}
    public static void submitButtonEvent(int button, boolean down) {}
    public static void submitScrollEvent(float x, float y) {}
    public static boolean wantsMouse() { return false; }
}
