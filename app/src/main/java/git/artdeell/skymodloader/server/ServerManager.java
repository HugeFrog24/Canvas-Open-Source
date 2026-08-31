package git.artdeell.skymodloader.server;

import android.content.Context;
import android.content.SharedPreferences;

import git.artdeell.skymodloader.R;

public class ServerManager {
    public static final String PREFS_NAME = "package_configs";
    public static final String KEY_CUSTOM_SERVER = "custom_server";
    public static final String KEY_SERVER_HOST = "server_host";
    public static final String DEFAULT_HOST = "";

    public static String getDefaultHost() {
        return DEFAULT_HOST;
    }

    public static SharedPreferences getPrefs(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public static String sanitizeHost(String host) {
        if (host == null) return "";
        return host.trim().replaceFirst("^https?://", "").replaceAll("/.*$", "");
    }

    public static boolean isCustomServerEnabled(Context context) {
        return getPrefs(context).getBoolean(KEY_CUSTOM_SERVER, false);
    }

    public static String getCurrentHost(Context context) {
        return sanitizeHost(getPrefs(context).getString(KEY_SERVER_HOST, getDefaultHost()));
    }

    public static int getActiveBootLogoRes(Context context) {
        return R.drawable.banner2;
    }
}

