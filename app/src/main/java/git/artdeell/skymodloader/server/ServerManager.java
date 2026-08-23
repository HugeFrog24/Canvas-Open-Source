package git.artdeell.skymodloader.server;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.core.content.ContextCompat;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import git.artdeell.skymodloader.AccountStorage;
import git.artdeell.skymodloader.R;

public class ServerManager {
    public static final String PREFS_NAME = "package_configs";
    public static final String KEY_CUSTOM_SERVER = "custom_server";
    public static final String KEY_SERVER_HOST = "server_host";
    public static final String LIVE_HOST = "live.radiance.thatgamecompany.com";

    public static final List<ApprovedServer> APPROVED_SERVERS = Collections.unmodifiableList(Arrays.asList(
        new ApprovedServer(
            "radiance",
            "Radiance Official Private Server",
            "sky.thatskyradiance.duckdns.org",
            R.color.teal_700,
            R.drawable.status_dot_radiance,
            "sky.thatskyradiance.duckdns.org",
            R.drawable.server_icon_radiance
        )
    ));

    public static String getDefaultHost() {
        if (!APPROVED_SERVERS.isEmpty()) {
            return APPROVED_SERVERS.get(0).host;
        }
        return "sky.thatskyradiance.duckdns.org";
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

    public static ApprovedServer getActiveApprovedServer(Context context) {
        if (!isCustomServerEnabled(context)) return null;
        String currentHost = getCurrentHost(context);
        for (ApprovedServer server : APPROVED_SERVERS) {
            if (server.host.equalsIgnoreCase(currentHost)) {
                return server;
            }
        }
        return null;
    }

    public static boolean isLiveActive(Context context) {
        return !isCustomServerEnabled(context);
    }

    public static boolean isCustomThirdPartyActive(Context context) {
        return isCustomServerEnabled(context) && getActiveApprovedServer(context) == null;
    }

    public static int getActiveBootLogoRes(Context context) {
        ApprovedServer activeServer = getActiveApprovedServer(context);
        if (activeServer != null && activeServer.hasCustomIcon()) {
            return activeServer.iconRes;
        }
        return R.drawable.banner2;
    }

    public static void activateServer(Context context, ApprovedServer server) {
        getPrefs(context).edit()
            .putBoolean(KEY_CUSTOM_SERVER, true)
            .putString(KEY_SERVER_HOST, server.host)
            .apply();
        AccountStorage.sync(context);
    }

    public static void activateLiveServer(Context context) {
        getPrefs(context).edit()
            .putBoolean(KEY_CUSTOM_SERVER, false)
            .apply();
        AccountStorage.sync(context);
    }

    public interface ServerSelectionCallback {
        void onServerSelected();
    }

    public static void showServerSelectionDialog(Activity activity, ServerSelectionCallback callback) {
        View dialogView = LayoutInflater.from(activity).inflate(R.layout.dialog_server_select, null);
        LinearLayout listContainer = dialogView.findViewById(R.id.dialog_server_list_container);

        AlertDialog dialog = new AlertDialog.Builder(activity)
            .setView(dialogView)
            .create();

        if (dialog.getWindow() != null) {
            dialog.getWindow().setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
        }

        ApprovedServer activeServer = getActiveApprovedServer(activity);
        boolean isLive = isLiveActive(activity);

        View liveItem = LayoutInflater.from(activity).inflate(R.layout.server_list_item, listContainer, false);
        TextView liveName = liveItem.findViewById(R.id.item_server_name);
        TextView liveHost = liveItem.findViewById(R.id.item_server_host);
        View liveDot = liveItem.findViewById(R.id.item_server_dot);
        ImageView liveIcon = liveItem.findViewById(R.id.item_server_icon);
        TextView liveBadge = liveItem.findViewById(R.id.item_server_badge);

        liveName.setText(R.string.server_live);
        liveHost.setText(LIVE_HOST);
        liveDot.setBackgroundResource(R.drawable.status_dot_online);
        if (liveIcon != null) {
            liveIcon.setImageResource(R.drawable.ic_server);
        }
        if (isLive) {
            liveBadge.setVisibility(View.VISIBLE);
            liveName.setTextColor(ContextCompat.getColor(activity, R.color.teal_700));
        }

        liveItem.setOnClickListener(v -> {
            activateLiveServer(activity);
            Toast.makeText(activity, R.string.server_connected_live, Toast.LENGTH_SHORT).show();
            dialog.dismiss();
            if (callback != null) callback.onServerSelected();
        });
        listContainer.addView(liveItem);

        for (ApprovedServer server : APPROVED_SERVERS) {
            View serverItem = LayoutInflater.from(activity).inflate(R.layout.server_list_item, listContainer, false);
            TextView nameView = serverItem.findViewById(R.id.item_server_name);
            TextView hostView = serverItem.findViewById(R.id.item_server_host);
            View dotView = serverItem.findViewById(R.id.item_server_dot);
            ImageView iconView = serverItem.findViewById(R.id.item_server_icon);
            TextView badgeView = serverItem.findViewById(R.id.item_server_badge);

            nameView.setText(server.name);
            hostView.setText(server.description != null ? server.description : server.host);
            dotView.setBackgroundResource(server.statusDotRes);
            if (iconView != null) {
                iconView.setImageResource(server.iconRes);
            }

            boolean isCurrent = activeServer != null && activeServer.id.equals(server.id);
            if (isCurrent) {
                badgeView.setVisibility(View.VISIBLE);
                nameView.setTextColor(ContextCompat.getColor(activity, server.accentColorRes));
            }

            serverItem.setOnClickListener(v -> {
                activateServer(activity, server);
                Toast.makeText(activity, activity.getString(R.string.server_connected_to, server.name), Toast.LENGTH_SHORT).show();
                dialog.dismiss();
                if (callback != null) callback.onServerSelected();
            });
            listContainer.addView(serverItem);
        }

        dialogView.findViewById(R.id.dialog_server_cancel).setOnClickListener(v -> dialog.dismiss());
        dialog.show();
    }
}
