package git.artdeell.skymodloader.auth;

import com.tgc.sky.accounts.SystemAccountType;

public class Apple extends WebLogin {
    public Apple() {
        super("Apple", SystemAccountType.kSystemAccountType_Apple);
    }
}
