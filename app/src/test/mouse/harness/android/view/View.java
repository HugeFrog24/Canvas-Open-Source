package android.view;

public class View {
    final int width, height;
    public View(int width, int height) { this.width = width; this.height = height; }
    public int getWidth() { return width; }
    public int getHeight() { return height; }
    public void getLocationOnScreen(int[] out) { out[0] = 0; out[1] = 0; }
    public void requestPointerCapture() {}
    public void releasePointerCapture() {}
}
