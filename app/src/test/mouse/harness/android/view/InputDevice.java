// Stub for the host harness (MOUSE.md): only what GameActivity's mouse paths read.
package android.view;

public class InputDevice {
    final int vendorId, productId, sources;
    final boolean virtual;
    public InputDevice(int vendorId, int productId, int sources, boolean virtual) {
        this.vendorId = vendorId; this.productId = productId; this.sources = sources; this.virtual = virtual;
    }
    public int getVendorId() { return vendorId; }
    public int getProductId() { return productId; }
    public int getSources() { return sources; }
    public boolean isVirtual() { return virtual; }
    public String getName() { return "USB Optical Mouse (stub)"; }
}
