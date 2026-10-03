package android.content;

/** Host-only stand-in; never packaged in hook.dex. */
public abstract class Context {
    public abstract String getPackageName();
    public abstract ClassLoader getClassLoader();
}
