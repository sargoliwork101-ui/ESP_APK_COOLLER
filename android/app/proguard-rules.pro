# WebView JS Bridge needs to keep its methods
-keepclassmembers class ir.cooler.smart.AndroidBridge {
    public *;
}
-keepattributes JavascriptInterface
