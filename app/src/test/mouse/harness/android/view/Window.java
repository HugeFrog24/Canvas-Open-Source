package android.view;

public class Window {
    final View decor;
    public Window(View decor) { this.decor = decor; }
    public View getDecorView() { return decor; }
}
