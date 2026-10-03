package android.util;

/** Host-only stand-in; never packaged in hook.dex. */
public final class Log {
    public static int errors;
    public static int i(String tag, String message) { return 0; }
    public static int e(String tag, String message, Throwable failure) { errors++; return 0; }
}
