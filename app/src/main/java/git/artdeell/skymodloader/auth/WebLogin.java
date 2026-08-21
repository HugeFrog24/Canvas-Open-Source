package git.artdeell.skymodloader.auth;

import android.annotation.SuppressLint;
import android.app.Dialog;
import android.content.Intent;
import android.net.Uri;
import android.view.Window;
import android.view.WindowManager;
import android.os.Build;
import android.webkit.CookieManager;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import git.artdeell.skymodloader.net.StarwatchBlocker;

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

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public class WebLogin extends WebViewClient implements SystemAccountInterface {
    private final SystemAccountType accountType;
    private final String loginUrl;
    private final String redirectUrl;
    private Dialog dialog;
    private WebView webView;
    private SystemAccountClientInfo m_accountClientInfo;
    private SystemAccountServerInfo m_accountServerInfo;
    private GameActivity m_activity;
    private SystemAccountInterface.UpdateClientInfoCallback m_callback;
    private boolean m_signedInSuccessfully = false;

    public WebLogin(String webLoginType, String token, SystemAccountType systemAccountType) {
        this.accountType = systemAccountType;
        if (token == null) token = "";
        String host = BuildConfig.SKY_SERVER_HOSTNAME;
        if (host != null) {
            host = host.trim().replaceFirst("^https?://", "").replaceAll("/.*$", "");
        } else {
            host = "live.radiance.thatgamecompany.com";
        }
        this.loginUrl = String.format("https://%s/account/auth/oauth_signin?type=%s&token=%s", host, webLoginType, token);
        this.redirectUrl = String.format("https://%s/account/auth/oauth_redirect", host);
    }

    public WebLogin(String webLoginType, SystemAccountType systemAccountType) {
        this(webLoginType, null, systemAccountType);
    }

    @Override
    public SystemAccountClientInfo GetClientInfo() {
        return m_accountClientInfo;
    }

    @Override
    public SystemAccountServerInfo GetServerInfo() {
        return m_accountServerInfo;
    }

    public void Initialize(GameActivity gameActivity, SystemAccountInterface.UpdateClientInfoCallback updateClientInfoCallback) {
        this.m_activity = gameActivity;
        this.m_callback = updateClientInfoCallback;
        SystemAccountClientInfo systemAccountClientInfo = new SystemAccountClientInfo();
        this.m_accountClientInfo = systemAccountClientInfo;
        systemAccountClientInfo.accountType = accountType;
        if ("beta.radiance.thatgamecompany.com".equals(BuildConfig.SKY_SERVER_HOSTNAME) && systemAccountClientInfo.accountType == SystemAccountType.kSystemAccountType_Google) {
            this.m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_NotAvailable;
        } else {
            this.m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
        }
        this.m_accountClientInfo.requestState = SystemAccountClientRequestState.kSystemAccountClientRequestState_Idle;
        SystemAccountServerInfo systemAccountServerInfo = new SystemAccountServerInfo();
        this.m_accountServerInfo = systemAccountServerInfo;
        systemAccountServerInfo.type = accountType;
        systemAccountServerInfo.state = SystemAccountServerState.kSystemAccountServerState_Initializing;
        this.m_callback.UpdateClientInfo(this.m_accountClientInfo);
    }

    public void SignIn() {
        m_activity.runOnUiThread(() -> {
            m_signedInSuccessfully = false;
            m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SigningIn;
            m_callback.UpdateClientInfo(m_accountClientInfo);
            startSignIn();
        });
    }

    public void SignOut() {
        m_activity.runOnUiThread(() -> {
            m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SigningOut;
            CookieManager.getInstance().removeAllCookies((bool) -> {
                m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
                m_callback.UpdateClientInfo(m_accountClientInfo);
            });
        });
    }

    public void RefreshCredentials(SystemAccountClientRequestState systemAccountClientRequestState) {
        SignIn();
    }

    @SuppressLint("SetJavaScriptEnabled")
    public void startSignIn() {
        dialog = new Dialog(m_activity);
        dialog.setOnDismissListener(dialog1 -> {
            if (!m_signedInSuccessfully) {
                submitSignOutState();
            }
        });
        webView = new WebView(m_activity) {
            @Override
            public boolean onCheckIsTextEditor() {
                return true;
            }
        };
        dialog.setContentView(webView);
        webView.setWebViewClient(this);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setUseWideViewPort(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            CookieManager.getInstance().setAcceptThirdPartyCookies(webView, true);
        }
        if (this.accountType == SystemAccountType.kSystemAccountType_Google) {
            settings.setUserAgentString("Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Mobile Safari/537.36");
        }
        webView.loadUrl(loginUrl);
        dialog.show();
        showWebView();
    }

    @Override
    public boolean shouldOverrideUrlLoading(WebView webView, WebResourceRequest webResourceRequest) {
        return checkAndHandleRedirect(webResourceRequest.getUrl().toString());
    }

    @Override
    public boolean shouldOverrideUrlLoading(WebView webView, String url) {
        return checkAndHandleRedirect(url);
    }

    private boolean checkAndHandleRedirect(String url) {
        if (url != null && (url.startsWith(redirectUrl) || url.contains("/account/auth/oauth_redirect"))) {
            if (dialog != null) {
                dialog.hide();
            }
            new Thread(() -> processLoading(url)).start();
            return true;
        }
        return false;
    }

    @Override
    public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
        WebResourceResponse blocked = StarwatchBlocker.interceptWebViewRequest(request);
        if (blocked != null) return blocked;
        return super.shouldInterceptRequest(view, request);
    }

    private void processLoading(final String url) {
        CookieManager.getInstance().flush();
        try {
            HttpURLConnection httpURLConnection = (HttpURLConnection) new URL(url).openConnection();
            httpURLConnection.setInstanceFollowRedirects(true);
            httpURLConnection.setRequestProperty("User-Agent", "Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Mobile Safari/537.36");
            String cookies = CookieManager.getInstance().getCookie(url);
            if (cookies != null && !cookies.isEmpty()) {
                httpURLConnection.setRequestProperty("Cookie", cookies);
            }
            InputStream inputStream;
            int responseCode = httpURLConnection.getResponseCode();
            if (responseCode >= 200 && responseCode < 300) {
                inputStream = httpURLConnection.getInputStream();
            } else {
                inputStream = httpURLConnection.getErrorStream();
            }
            if (inputStream == null) {
                throw new IOException("HTTP response code: " + responseCode);
            }
            ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
            byte[] bArr = new byte[1024];
            int read;
            while ((read = inputStream.read(bArr)) != -1) {
                byteArrayOutputStream.write(bArr, 0, read);
            }
            byteArrayOutputStream.flush();
            inputStream.close();
            httpURLConnection.disconnect();
            JSONObject obj = new JSONObject(new String(byteArrayOutputStream.toByteArray(), StandardCharsets.UTF_8));
            String token = obj.optString("token", "");
            if (token.isEmpty()) {
                token = obj.optString("signature", "");
            }
            submitSignInState(obj.optString("id"), obj.optString("alias"), token);
        } catch (Exception e) {
            e.printStackTrace();
            submitSignOutState();
        }
    }

    private void submitSignOutState() {
        m_signedInSuccessfully = false;
        m_activity.runOnUiThread(() -> {
            m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedOut;
            m_callback.UpdateClientInfo(m_accountClientInfo);
        });
    }

    private void submitSignInState(final String id, final String alias, final String signature) {
        m_signedInSuccessfully = true;
        m_activity.runOnUiThread(() -> {
            if (dialog != null && dialog.isShowing()) {
                dialog.dismiss();
            }
            m_accountClientInfo.accountId = id;
            m_accountClientInfo.alias = alias;
            m_accountClientInfo.signature = signature;
            m_accountClientInfo.state = SystemAccountClientState.kSystemAccountClientState_SignedIn;
            m_callback.UpdateClientInfo(this.m_accountClientInfo);
        });
    }

    private void showWebView() {
        if (dialog == null) return;
        Window dialogWindow = dialog.getWindow();
        if (dialogWindow != null) {
            dialogWindow.setFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND, WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            dialogWindow.setLayout(WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT);
        }
    }

    @Override
    public void onPageFinished(WebView view, String url) {
        super.onPageFinished(view, url);
        showWebView();
    }
}