package git.artdeell.skymodloader.auth;

import android.app.Dialog;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.util.Log;
import android.view.ViewGroup;
import android.view.Window;
import android.webkit.WebResourceRequest;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import com.tgc.sky.BuildConfig;
import com.tgc.sky.GameActivity;
import com.tgc.sky.accounts.SystemAccountClientInfo;
import com.tgc.sky.accounts.SystemAccountClientRequestState;
import com.tgc.sky.accounts.SystemAccountClientState;
import com.tgc.sky.accounts.SystemAccountInterface;
import com.tgc.sky.accounts.SystemAccountServerInfo;
import com.tgc.sky.accounts.SystemAccountServerState;
import com.tgc.sky.accounts.SystemAccountType;

import org.json.JSONObject;

import java.io.IOException;
import java.io.InputStream;
import java.io.UnsupportedEncodingException;
import java.net.URL;
import java.net.URLConnection;
import java.net.URLEncoder;

/* loaded from: classes.dex */
public final class Facebook implements SystemAccountInterface {

    /* renamed from: a, reason: collision with root package name */
    public SystemAccountClientInfo clientInfo;

    /* renamed from: b, reason: collision with root package name */
    public SystemAccountServerInfo serverInfo;

    /* renamed from: c, reason: collision with root package name */
    public GameActivity activity;

    /* renamed from: d, reason: collision with root package name */
    public SystemAccountInterface.UpdateClientInfoCallback callback;

    /* renamed from: e, reason: collision with root package name */
    public SharedPreferences accountStorage;

    public static JSONObject doGraphRequest(String str) throws IOException {
        URLConnection uRLConnectionOpenConnection = new URL("https://graph.fb.gg/v22.0/" + str).openConnection();
        uRLConnectionOpenConnection.connect();
        InputStream inputStream = uRLConnectionOpenConnection.getInputStream();
        try {
            StringBuilder sb = new StringBuilder();
            byte[] bArr = new byte[1024];
            while (true) {
                int i2 = inputStream.read(bArr);
                if (i2 == -1) {
                    JSONObject jSONObject = new JSONObject(sb.toString());
                    inputStream.close();
                    return jSONObject;
                }
                sb.append(new String(bArr, 0, i2));
            }
        } catch (Throwable th) {
            if (inputStream != null) {
                try {
                    inputStream.close();
                } catch (Throwable th2) {
                    th.addSuppressed(th2);
                }
            }
//            throw th;
        }
        return null;
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final SystemAccountClientInfo GetClientInfo() {
        return this.clientInfo;
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final SystemAccountServerInfo GetServerInfo() {
        return this.serverInfo;
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final void Initialize(GameActivity gameActivity, SystemAccountInterface.UpdateClientInfoCallback updateClientInfoCallback) {
        this.accountStorage = gameActivity.getSharedPreferences("accounts", 0);
        this.activity = gameActivity;
        this.callback = updateClientInfoCallback;
        SystemAccountClientInfo systemAccountClientInfo = new SystemAccountClientInfo();
        this.clientInfo = systemAccountClientInfo;
        SystemAccountType systemAccountType = SystemAccountType.kSystemAccountType_Facebook;
        systemAccountClientInfo.accountType = systemAccountType;
        if (BuildConfig.SKY_SERVER_HOSTNAME.equals("live.radiance.thatgamecompany.com")) {
            this.clientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
        } else {
            this.clientInfo.state = SystemAccountClientState.kSystemAccountClientState_NotAvailable;
        }
        SystemAccountServerInfo systemAccountServerInfo = new SystemAccountServerInfo();
        this.serverInfo = systemAccountServerInfo;
        systemAccountServerInfo.type = systemAccountType;
        systemAccountServerInfo.state = SystemAccountServerState.kSystemAccountServerState_UnLinked;
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final void RefreshCredentials(SystemAccountClientRequestState systemAccountClientRequestState) {
        SignIn();
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final void SignIn() {
        Log.i("FacebookAuth", "Called SignIn()");
        if (!this.accountStorage.contains("facebookToken")) {
            this.activity.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    authenticate();
                }
            });
        } else {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    if (graphAuthorize(accountStorage.getString("facebookToken", ""))) {
                        activity.runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                authenticate();
                            }
                        });
                    }
                }
            }).start();
            Log.i("FacebookAuth", "Call Over!");
        }
    }

    @Override // com.tgc.sky.accounts.SystemAccountInterface
    public final void SignOut() {
        this.accountStorage.edit().remove("facebookToken").apply();
        SystemAccountClientInfo systemAccountClientInfo = this.clientInfo;
        systemAccountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
        this.callback.UpdateClientInfo(systemAccountClientInfo);
    }

    public final boolean graphAuthorize(String str) {
        try {
            JSONObject jSONObjectA = doGraphRequest("me?field=name&access_token=" + str);
            this.clientInfo.accountId = jSONObjectA.getString("id");
            this.clientInfo.alias = jSONObjectA.getString("name");
            SystemAccountClientInfo systemAccountClientInfo = this.clientInfo;
            systemAccountClientInfo.signature = str;
            systemAccountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedIn;
            this.accountStorage.edit().putString("facebookToken", str).commit();
            this.callback.UpdateClientInfo(this.clientInfo);
            Log.i("FacebookAuth", "graphAuthorize done");
            return true;
        } catch (Exception e7) {
            e7.printStackTrace();
            return false;
        }
    }

    private void authenticate() {
        Log.i("FacebookAuth", "Starting authentication...");
        Dialog dialog = new Dialog(this.activity);
        dialog.requestWindowFeature(1);
        dialog.setCancelable(true);
        WebView webView = new WebView(this.activity);
        webView.getSettings().setJavaScriptEnabled(true);
        webView.setLayoutParams(new ViewGroup.LayoutParams(-1, -1));
        dialog.setContentView(webView);
        final boolean[] dismissed = {true};
        dialog.setOnDismissListener(new DialogInterface.OnDismissListener() {
            @Override
            public void onDismiss(DialogInterface dialogInterface) {
                if (dismissed[0]) {
                    SystemAccountClientInfo systemAccountClientInfo = clientInfo;
                    systemAccountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
                    callback.UpdateClientInfo(systemAccountClientInfo);
                }
            }
        });
        dialog.setOnCancelListener(new DialogInterface.OnCancelListener() {
            @Override
            public void onCancel(DialogInterface dialogInterface) {
                SystemAccountClientInfo systemAccountClientInfo = clientInfo;
                systemAccountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
                callback.UpdateClientInfo(systemAccountClientInfo);
            }
        });
        String redirectUri = "https://" + BuildConfig.SKY_SERVER_HOSTNAME + "/account/auth/oauth_redirect";
        try {
            String oauthUrl = "https://www.facebook.com/v22.0/dialog/oauth?client_id=293746044767069&redirect_uri=" + URLEncoder.encode(redirectUri, "UTF-8") + "&scope=public_profile,user_friends&response_type=token";
            Log.i("FacebookAuth", "Loading URL: " + oauthUrl);
            webView.loadUrl(oauthUrl);
            webView.setWebViewClient(new WebViewClient() {
                @Override
                public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
                    String url = request.getUrl().toString();
                    if (url.startsWith(redirectUri)) {
                        final String tok = "access_token=";
                        int accessTokenIndex = url.indexOf(tok);
                        if (accessTokenIndex == -1) {
                            clientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
                            callback.UpdateClientInfo(clientInfo);
                            dismissed[0] = false;
                            dialog.dismiss();
                            return true;
                        }
                        int ampersandIndex = url.indexOf('&', accessTokenIndex);
                        final String token;
                        if (ampersandIndex != -1) {
                            token = url.substring(accessTokenIndex + tok.length(), ampersandIndex);
                        } else {
                            token = url.substring(accessTokenIndex + tok.length());
                        }
                        new Thread(new Runnable() {
                            @Override
                            public void run() {
                                if (graphAuthorize(token)) {
                                    dismissed[0] = false;
                                }
                                activity.runOnUiThread(new Runnable() {
                                    @Override
                                    public void run() {
                                        dialog.dismiss();
                                    }
                                });
                            }
                        }).start();
                        return true;
                    }
                    return false;
                }
            });
            dialog.show();
            Window window = dialog.getWindow();
            if (window != null) {
                window.setLayout(-1, -1);
            }
        } catch (UnsupportedEncodingException e7) {
            Log.e("FacebookAuth", "Encoding error: " + e7.getMessage());
        }
    }

    public boolean HasAppFriendsPermission() {
        return this.clientInfo.permissions != null && this.clientInfo.permissions.contains("user_friends");
    }

    public boolean GetAppFriendsPermission(OnPermissionCallback onPermissionCallback) {
        return false;
    }

    public interface OnPermissionCallback {
        void onCallback(boolean z, String str);
    }
}