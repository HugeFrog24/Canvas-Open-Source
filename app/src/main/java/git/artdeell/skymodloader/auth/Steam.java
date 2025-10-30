package git.artdeell.skymodloader.auth;

import com.tgc.sky.accounts.SystemAccountType;

public class Steam extends WebLogin{
    public static final String TOKEN = "cKN45n7UTSKHNoyzdugWNE:APA91bFg8MGDK26uj-RjRrRSANDGST4AqE29kh3ygCzN0IZWLgGis2K16aD9JoYXnaRBD2LgghA18Bc0ZG76AuWEzr3eAMTSRen8SsBPjtPftUVnuXECrjVfhd9z_WeDbx9MaHUO7GS9";
    public Steam() {
        super("Steam", TOKEN, SystemAccountType.kSystemAccountType_Steam);
    }
}
