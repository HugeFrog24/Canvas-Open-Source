package android.view;

public abstract class InputEvent {
    InputDevice device;
    int source, deviceId;
    public InputDevice getDevice() { return device; }
    public int getSource() { return source; }
    public int getDeviceId() { return deviceId; }
    public boolean isFromSource(int s) { return (source & s) == s; }
}
