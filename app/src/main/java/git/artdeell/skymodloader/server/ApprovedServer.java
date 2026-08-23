package git.artdeell.skymodloader.server;

import androidx.annotation.ColorRes;
import androidx.annotation.DrawableRes;

import git.artdeell.skymodloader.R;

public class ApprovedServer {
    public final String id;
    public final String name;
    public final String host;
    @ColorRes
    public final int accentColorRes;
    @DrawableRes
    public final int statusDotRes;
    public final String description;
    @DrawableRes
    public final int iconRes;

    public ApprovedServer(String id, String name, String host, @ColorRes int accentColorRes, @DrawableRes int statusDotRes, String description, @DrawableRes int iconRes) {
        this.id = id;
        this.name = name;
        this.host = host;
        this.accentColorRes = accentColorRes;
        this.statusDotRes = statusDotRes;
        this.description = description;
        this.iconRes = (iconRes != 0) ? iconRes : R.drawable.ic_server;
    }

    public ApprovedServer(String id, String name, String host, @ColorRes int accentColorRes, @DrawableRes int statusDotRes, String description) {
        this(id, name, host, accentColorRes, statusDotRes, description, R.drawable.ic_server);
    }

    public ApprovedServer(String id, String name, String host, @ColorRes int accentColorRes, @DrawableRes int statusDotRes) {
        this(id, name, host, accentColorRes, statusDotRes, host, R.drawable.ic_server);
    }

    public boolean hasCustomIcon() {
        return iconRes != 0 && iconRes != R.drawable.ic_server;
    }
}
