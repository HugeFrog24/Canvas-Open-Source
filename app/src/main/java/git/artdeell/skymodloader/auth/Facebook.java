package git.artdeell.skymodloader.auth;

import com.tgc.sky.accounts.SystemAccountType;

public class Facebook extends WebLogin {
    public Facebook() {
        super("Facebook", SystemAccountType.kSystemAccountType_Facebook);
    }
}
