package git.artdeell.skymodloader;


import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AlertDialog;

import com.tgc.sky.BuildConfig;
import com.tgc.sky.GameActivity;

import java.io.File;
import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;

import git.artdeell.skymodloader.elfmod.ElfRefcountLoader;
import git.artdeell.skymodloader.iconloader.IconLoader;

public class MainActivity extends Activity {
    private SharedPreferences sharedPreferences;
    public static String SKY_PACKAGE_NAME;
    private Map<String, Integer> skyPackages;

    public static DeviceInfo deviceInfo;


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        deviceInfo = getDeviceInfo();
        sharedPreferences = getSharedPreferences("package_configs", Context.MODE_PRIVATE);
        SKY_PACKAGE_NAME = sharedPreferences.getString("sky_package_name", "com.tgc.sky.android");
        sharedPreferences.edit().putString("sky_package_name", SKY_PACKAGE_NAME).apply();
        skyPackages = new HashMap<>();
        skyPackages.put("com.tgc.sky.android", 0);
        skyPackages.put("com.tgc.sky.android.test.gold", 1);
        skyPackages.put("com.tgc.sky.android.huawei", 2);
        loadGame();
    }

    private void loadGame() {
        PackageManager pm = getPackageManager();
        try {
            PackageInfo info = pm.getPackageInfo(SKY_PACKAGE_NAME, PackageManager.GET_SHARED_LIBRARY_FILES);
            SMLApplication.skyPName = info.packageName;
            SMLApplication.skyRes = pm.getResourcesForApplication(info.packageName);
            SMLApplication.smlRes = getResources();
            String versionName = info.versionName;
            BuildConfig.SKY_VERSION = versionName.substring(0, versionName.indexOf(' ')).trim();
            BuildConfig.VERSION_CODE = info.versionCode;

            String nativeLibraryDir = info.applicationInfo.nativeLibraryDir;
            String libPath = nativeLibraryDir;

            // Check if nativeLibraryDir is empty or doesn't contain libs (split APK case)
            File libDir = new File(nativeLibraryDir);
            if (!libDir.exists() || libDir.listFiles() == null || libDir.listFiles().length == 0) {
                // Extract from split APK
                libPath = extractLibrariesFromApk(info.applicationInfo);
            }

            File modsDir = new File(getFilesDir(), "mods");
            File configDir = new File(getFilesDir(), "config");
            if (!configDir.isDirectory() && !configDir.mkdirs()) throw new IOException("Failed to create mod configuration directory");

            ElfLoader loader = new ElfLoader(libPath + ":/system/lib64");
            loader.loadLib("libBootloader.so");
            System.loadLibrary("ciphered");

            setDeviceInfoNative(
                deviceInfo.xdpi,
                deviceInfo.ydpi,
                deviceInfo.density,
                Optional.ofNullable(deviceInfo.deviceName).orElse(""),
                Optional.ofNullable(deviceInfo.deviceManufacturer).orElse(""),
                Optional.ofNullable(deviceInfo.deviceModel).orElse("")
            );

            IconLoader.findIcons();
            BuildConfig.VERSION_CODE = sharedPreferences.getBoolean("skip_updates", false) ? 0x99999 : info.versionCode;
            Integer gameType = skyPackages.getOrDefault(SKY_PACKAGE_NAME, 0);
            MainActivity.settle(
                    info.versionCode,
                    gameType == null ? 0 : gameType,
                    BuildConfig.SKY_SERVER_HOSTNAME,
                    configDir.getAbsolutePath(),
                    SMLApplication.skyRes.getAssets()
            );

            if (SKY_PACKAGE_NAME.equals("com.tgc.sky.android.test.gold")) {
                SKY_PACKAGE_NAME = "com.tgc.sky.android.test.";
                BuildConfig.SKY_SERVER_HOSTNAME = "beta.radiance.thatgamecompany.com";
                BuildConfig.SKY_BRANCH_NAME = "Test";
                BuildConfig.SKY_STAGE_NAME = "Test";
            }

            if(sharedPreferences.getBoolean("custom_server", false)){
                BuildConfig.SKY_SERVER_HOSTNAME = sharedPreferences.getString("server_host", BuildConfig.SKY_SERVER_HOSTNAME);
                MainActivity.customServer(BuildConfig.SKY_SERVER_HOSTNAME);
            }

            new ElfRefcountLoader(libPath + ":/system/lib64", modsDir).load();
            BuildConfig.APPLICATION_ID = SKY_PACKAGE_NAME;
            startActivity(new Intent(this, GameActivity.class));
        } catch (PackageManager.NameNotFoundException e) {
            alertDialog(getString(R.string.sky_not_installed));
        } catch (Throwable e) {
            alertDialog(e);
        }
    }

    private String extractLibrariesFromApk(ApplicationInfo appInfo) throws IOException {
        File extractDir = new File(getFilesDir(), "extracted_libs");
        if (!extractDir.exists() && !extractDir.mkdirs()) {
            throw new IOException("Failed to create extraction directory");
        }

        // Find split APK containing native libs
        String[] splitSourceDirs = appInfo.splitSourceDirs;
        if (splitSourceDirs != null) {
            for (String splitApk : splitSourceDirs) {
                if (splitApk.contains("arm64_v8a") || splitApk.contains("config.arm64")) {
                    extractLibsFromZip(splitApk, extractDir);
                    break;
                }
            }
        }

        return extractDir.getAbsolutePath();
    }

    private void extractLibsFromZip(String apkPath, File destDir) throws IOException {
        java.util.zip.ZipFile zipFile = new java.util.zip.ZipFile(apkPath);
        java.util.Enumeration<? extends java.util.zip.ZipEntry> entries = zipFile.entries();

        while (entries.hasMoreElements()) {
            java.util.zip.ZipEntry entry = entries.nextElement();
            String name = entry.getName();

            if (name.startsWith("lib/arm64-v8a/") && name.endsWith(".so")) {
                String libName = name.substring(name.lastIndexOf('/') + 1);
                File destFile = new File(destDir, libName);

                if (!destFile.exists()) {
                    try (java.io.InputStream in = zipFile.getInputStream(entry);
                         java.io.FileOutputStream out = new java.io.FileOutputStream(destFile)) {
                        byte[] buffer = new byte[8192];
                        int read;
                        while ((read = in.read(buffer)) != -1) {
                            out.write(buffer, 0, read);
                        }
                    }
                    destFile.setExecutable(true);
                }
            }
        }
        zipFile.close();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
    }

    public void alertDialog(Throwable th) {
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        th.printStackTrace(pw);
        String stackTrace = sw.toString();
        pw.close();

        AlertDialog dialog = getAlertDialog(stackTrace);

        // Get the neutral button and override its behavior (to ensure it doesn't dismiss)
        dialog.getButton(AlertDialog.BUTTON_NEUTRAL).setOnClickListener(v -> {
            copyToClipboard(stackTrace);
        });
    }

    private @NonNull AlertDialog getAlertDialog(String stackTrace) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setMessage(stackTrace);

        // Existing OK button to dismiss the dialog
        builder.setPositiveButton(android.R.string.ok, (d, w) -> finish());

        // Show the dialog without setting neutral button here, so it doesn't close by default
        AlertDialog dialog = builder.create();

        // Add the "Copy" button manually after creating the dialog
        dialog.setButton(AlertDialog.BUTTON_NEUTRAL, "Copy", (d, which) -> {
            copyToClipboard(stackTrace);
        });

        // Show the dialog
        dialog.show();
        return dialog;
    }

    private void copyToClipboard(String stackTrace) {
        ClipboardManager clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData clip = ClipData.newPlainText("Stack Trace", stackTrace);
        clipboard.setPrimaryClip(clip);

        // Only show the toast for versions below API level 33 (Android 13)
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            Toast.makeText(this, "Stack trace copied to clipboard", Toast.LENGTH_SHORT).show();
        }
    }

    public void alertDialog(String th) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setMessage(th);
        builder.setPositiveButton(android.R.string.ok, (d, w) -> finish());
        builder.show();
    }

    public DeviceInfo getDeviceInfo() {
        DisplayMetrics displayMetrics = getResources().getDisplayMetrics();
        DeviceInfo deviceInfo = new DeviceInfo();
        deviceInfo.xdpi = displayMetrics.xdpi;
        deviceInfo.ydpi = displayMetrics.ydpi;
        deviceInfo.density = displayMetrics.density;

        deviceInfo.deviceName = Settings.Global.getString(getContentResolver(), "device_name");
        if (deviceInfo.deviceName == null || deviceInfo.deviceName.isEmpty()) {
            deviceInfo.deviceName = Settings.Secure.getString(getContentResolver(), "bluetooth_name");
        }
        deviceInfo.deviceName = (deviceInfo.deviceName == null || deviceInfo.deviceName.isEmpty()) ? "NO_DEVICE_NAME" : deviceInfo.deviceName;

        deviceInfo.deviceManufacturer = Build.MANUFACTURER;
        deviceInfo.deviceModel = Build.MODEL;
        return deviceInfo;
    }

    public static native void settle(int _gameVersion, int _gameType, String _hostName, String _configDir, AssetManager _gameAssets);
    public static native void setDeviceInfoNative(float _xdpi, float _ydpi, float _density, String _deviceName, String _manufacturer, String _model);
    public static native void onKeyboardCompleteNative(String message);
    public static native void customServer(String url);
    public static native void lateInitUserLibs();
    public static native void getSysetemUI(Object systemUI);

}