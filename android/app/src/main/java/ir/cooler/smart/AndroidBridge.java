package ir.cooler.smart;

import android.util.Log;
import android.webkit.JavascriptInterface;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.zip.GZIPInputStream;

/**
 * پل بومی بین وب‌ویو و اندروید.
 * متد request() به صورت همگام از سمت JS فراخوانی می‌شود، درخواست HTTP را در یک Thread پس‌زمینه
 * انجام داده و رشته‌ی پاسخ (یا متن خطای با پیشوند ERR:) را برمی‌گرداند.
 *
 * مزیت این پل نسبت به fetch درون WebView:
 *   ۱) مشکل CORS اصلاً وجود ندارد.
 *   ۲) مشکل Cleartext (http:// روی اندروید ۹+) به‌صورت مطمئن رفع شده.
 *   ۳) Timeout مشخص و مدیریت خطای بهتر.
 */
public class AndroidBridge {

    private static final String TAG = "CoolerBridge";
    private static final int CONNECT_TIMEOUT_MS = 4000;
    private static final int READ_TIMEOUT_MS = 5000;

    private final ExecutorService executor = Executors.newSingleThreadExecutor();

    @JavascriptInterface
    public String request(final String method, final String url, final String body) {
        // برای ساده‌تر شدن کد JS، درخواست را به صورت synchronously در اینجا انجام می‌دهیم
        // اما در thread جداگانه اجرا می‌کنیم تا روی UI Thread نباشد.
        try {
            return executor.submit(() -> doRequest(method, url, body)).get();
        } catch (Exception e) {
            Log.e(TAG, "request failed: " + e.getMessage(), e);
            return "ERR:" + (e.getMessage() != null ? e.getMessage() : "unknown");
        }
    }

    private String doRequest(String method, String url, String body) {
        HttpURLConnection conn = null;
        try {
            URL u = new URL(url);
            conn = (HttpURLConnection) u.openConnection();
            conn.setRequestMethod(method == null ? "GET" : method.toUpperCase());
            conn.setConnectTimeout(CONNECT_TIMEOUT_MS);
            conn.setReadTimeout(READ_TIMEOUT_MS);
            conn.setInstanceFollowRedirects(true);
            conn.setRequestProperty("Accept", "*/*");
            conn.setRequestProperty("Accept-Encoding", "gzip");
            conn.setRequestProperty("Connection", "close");

            if (body != null && body.length() > 0 && ("POST".equalsIgnoreCase(method))) {
                conn.setDoOutput(true);
                // نوع درخواست در JS تعیین شده است؛ اما در این پل ما هر POST را
                // با Content-Type = application/x-www-form-urlencoded یا application/json
                // بر اساس شروع body ارسال می‌کنیم (اگر با '{' شروع شد JSON است).
                if (body.trim().startsWith("{")) {
                    conn.setRequestProperty("Content-Type", "application/json; charset=utf-8");
                } else {
                    conn.setRequestProperty("Content-Type", "application/x-www-form-urlencoded; charset=utf-8");
                }
                byte[] data = body.getBytes(StandardCharsets.UTF_8);
                conn.setFixedLengthStreamingMode(data.length);
                OutputStream os = conn.getOutputStream();
                os.write(data);
                os.flush();
                os.close();
            } else {
                conn.setDoOutput(false);
            }

            int code = conn.getResponseCode();
            InputStream is;
            if (code >= 200 && code < 300) {
                is = conn.getInputStream();
            } else {
                InputStream es = conn.getErrorStream();
                is = es != null ? es : conn.getInputStream();
            }

            String encoding = conn.getContentEncoding();
            if ("gzip".equalsIgnoreCase(encoding)) {
                is = new GZIPInputStream(is);
            }

            String response = readAll(is);
            is.close();

            if (code < 200 || code >= 300) {
                return "ERR:" + code + " " + response;
            }
            return response;
        } catch (IOException e) {
            Log.w(TAG, "IO error: " + e.getMessage());
            return "ERR:" + e.getMessage();
        } catch (Exception e) {
            Log.e(TAG, "error: " + e.getMessage(), e);
            return "ERR:" + e.getMessage();
        } finally {
            if (conn != null) {
                try { conn.disconnect(); } catch (Exception ignored) {}
            }
        }
    }

    private static String readAll(InputStream is) throws IOException {
        if (is == null) return "";
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        byte[] buf = new byte[2048];
        int n;
        while ((n = is.read(buf)) > 0) baos.write(buf, 0, n);
        return baos.toString("UTF-8");
    }
}
