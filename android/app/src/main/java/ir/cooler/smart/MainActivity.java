package ir.cooler.smart;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.pm.ActivityInfo;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.appcompat.app.AppCompatActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends AppCompatActivity {

    private WebView webView;

    @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // تمام صفحه، روشن نگه داشتن صفحه هنگام کار با اپ، تمام‌صفحه immersive
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        );

        setContentView(R.layout.activity_main);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            // جلوگیری از رفتن رنگ باتم‌نو به حالت روشن (سیاه می‌ماند)
            getWindow().setNavigationBarColor(0xFF000000);
        }

        webView = findViewById(R.id.webview);
        webView.setOverScrollMode(View.OVER_SCROLL_NEVER);
        webView.setHorizontalScrollBarEnabled(false);
        webView.setVerticalScrollBarEnabled(false);

        WebSettings s = webView.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowContentAccess(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        // همیشه نسخه‌ی باندل‌شده با APK را لود کن (از کش قدیمی استفاده نشود)
        s.setCacheMode(WebSettings.LOAD_DEFAULT);
        s.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
        s.setTextZoom(100);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        // اجازه به بارگذاری از file:///android_asset
        s.setAllowFileAccessFromFileURLs(true);
        s.setAllowUniversalAccessFromFileURLs(true);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            s.setAlgorithmicDarkeningAllowed(false);
        }

        WebView.setWebContentsDebuggingEnabled(false); // اگر خواستید دیباگ کنید روی true بگذارید

        // تزریق پل ارتباطی برای رفع مشکل CORS و cleartext
        webView.addJavascriptInterface(new AndroidBridge(), "AndroidBridge");

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, String url) {
                // همه‌ی لینک‌ها درون خود WebView باز شوند (در این اپ هیچ لینک خارجی نیست)
                if (url.startsWith("file:///android_asset/")) {
                    view.loadUrl(url);
                    return false;
                }
                if (url.startsWith("http://") || url.startsWith("https://")) {
                    // ارتباط با ESP32 باید از طریق Bridge انجام شود؛ اگر لینک خارجی بود، رهگیری کنیم
                    return false;
                }
                return true;
            }
        });

        webView.setWebChromeClient(new WebChromeClient());

        // لود صفحه‌ی محلی از assets
        webView.loadUrl("file:///android_asset/web/index.html");
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (webView != null) webView.onResume();
    }

    @Override
    protected void onPause() {
        if (webView != null) webView.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.stopLoading();
            webView.removeAllViews();
            webView.destroy();
            webView = null;
        }
        super.onDestroy();
    }

    // کپی کردن فایل‌های web از assets به دیتابیس در اولین اجرا اگر نیاز بود
    // (در این پروژه مستقیماً از file:///android_asset/web استفاده می‌شود، پس نیازی نیست.)
}
