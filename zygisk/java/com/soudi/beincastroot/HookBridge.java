package com.soudi.beincastroot;

import android.content.Context;
import android.util.Log;

import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.concurrent.atomic.AtomicBoolean;

/** Small callback object used by the standalone ART hook in the root module. */
public final class HookBridge {
    private static final String TAG = "BeINCastRoot";
    private static final String PACKAGE = "ptv.bein.mena";
    private static final AtomicBoolean loggedOverride = new AtomicBoolean();

    // Set by native code immediately after installing each hook.
    public volatile Method backup;
    private final int kind;

    public HookBridge(int kind) {
        this.kind = kind;
    }

    public static native void installCast(ClassLoader appLoader);

    // LSPlant requires this exact instance-method signature.
    public Object callback(Object[] args) throws Throwable {
        Method original = backup;
        if (original == null) {
            throw new IllegalStateException("Original method unavailable for hook " + kind);
        }

        if (kind == 1) {
            Object result = invokeOriginal(original, args[0], Boolean.FALSE);
            if (loggedOverride.compareAndSet(false, true)) {
                Log.i(TAG, "Cast system output switcher option forced false");
            }
            return result;
        }

        // Preserve application initialization, including exceptions from the app.
        Object result = invokeOriginal(original, args[0], args[1]);
        try {
            Context context = (Context) args[1];
            if (PACKAGE.equals(context.getPackageName())) {
                installCast(context.getClassLoader());
            }
        } catch (Throwable failure) {
            // A failed Cast-hook installation must not replace a successful attach.
            Log.e(TAG, "Cast hook setup failed; app initialization continues", failure);
        }
        return result;
    }

    private static Object invokeOriginal(Method method, Object receiver, Object argument)
            throws Throwable {
        try {
            return method.invoke(receiver, new Object[] {argument});
        } catch (InvocationTargetException failure) {
            throw failure.getCause();
        }
    }
}
